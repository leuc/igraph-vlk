#include "layout_openord.h"
#include <cglm/cglm.h>
#include <math.h>
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GRID_DIM 128
#define GRID_VOL (GRID_DIM * GRID_DIM * GRID_DIM)
#define VIEW_SIZE 4000.0f
#define HALF_VIEW (VIEW_SIZE * 0.5f)
#define VIEW_TO_GRID (GRID_DIM / VIEW_SIZE)
#define RADIUS 10

static float fall_off[RADIUS * 2 + 1][RADIUS * 2 + 1][RADIUS * 2 + 1];

static void init_falloff() {
	static bool initialized = false;
	if (initialized)
		return;
	for (int i = -RADIUS; i <= RADIUS; i++) {
		for (int j = -RADIUS; j <= RADIUS; j++) {
			for (int k = -RADIUS; k <= RADIUS; k++) {
				float dist = sqrtf(i * i + j * j + k * k);
				fall_off[i + RADIUS][j + RADIUS][k + RADIUS] =
					(float)((RADIUS - fminf(dist, RADIUS)) / RADIUS);
			}
		}
	}
	initialized = true;
}

const char *openord_get_stage_name(int stage_id) {
	switch (stage_id) {
	case 0:
		return "Liquid";
	case 1:
		return "Expansion";
	case 2:
		return "Cooldown";
	case 3:
		return "Crunch";
	case 4:
		return "Simmer";
	case 5:
		return "Done";
	default:
		return "Unknown";
	}
}

void openord_init(OpenOrdContext *ctx, int node_count, int grid_size) {
	memset(ctx, 0, sizeof(OpenOrdContext));
	init_falloff();

	ctx->grid_size = GRID_DIM;
	ctx->view_size = VIEW_SIZE;
	ctx->radius = (float)RADIUS;

	// Allocate density grid (can be huge, maybe use sparse if memory is issue,
	// but 128^3 is 2M floats = 8MB, OK) If user wants larger, we might need a
	// hash map. For now, 128^3 is fast.
	ctx->density_grid = calloc(GRID_VOL, sizeof(float));

	// Stages setup based on original implementation
	// Liquid
	ctx->stages[0] = (OpenOrdStage){200, 2000.0f, 2.0f, 1.0f, 0, 0, 0, 0, 0};
	// Expansion
	ctx->stages[1] = (OpenOrdStage){200, 2000.0f, 10.0f, 1.0f, 0, 0, 0, 0, 0};
	// Cooldown
	ctx->stages[2] = (OpenOrdStage){200, 2000.0f, 1.0f, 0.1f, 0, 0, 0, 0, 0};
	// Crunch
	ctx->stages[3] = (OpenOrdStage){50, 250.0f, 1.0f, 0.25f, 0, 0, 0, 0, 0};
	// Simmer
	ctx->stages[4] = (OpenOrdStage){100, 250.0f, 0.5f, 0.0f, 0, 0, 0, 0, 0};

	// Calculate cut parameters
	float edge_cut = 0.8f; // Default high cut
	float cut_length_end = 40000.0f * (1.0f - edge_cut);
	if (cut_length_end <= 1.0f)
		cut_length_end = 1.0f;
	float cut_length_start = 4.0f * cut_length_end;

	ctx->cut_end = cut_length_end;
	ctx->cut_off_length = cut_length_start;
	ctx->cut_rate = (cut_length_start - cut_length_end) / 400.0f;
	ctx->min_edges = 20.0f; // Default start

	ctx->initialized = true;
	ctx->num_threads = omp_get_max_threads();
}

void openord_cleanup(OpenOrdContext *ctx) {
	if (ctx->density_grid)
		free(ctx->density_grid);
	ctx->initialized = false;
}

static inline int get_grid_idx(float x, float y, float z) {
	int gx = (int)((x + HALF_VIEW) * VIEW_TO_GRID);
	int gy = (int)((y + HALF_VIEW) * VIEW_TO_GRID);
	int gz = (int)((z + HALF_VIEW) * VIEW_TO_GRID);

	if (gx < 0)
		gx = 0;
	if (gx >= GRID_DIM)
		gx = GRID_DIM - 1;
	if (gy < 0)
		gy = 0;
	if (gy >= GRID_DIM)
		gy = GRID_DIM - 1;
	if (gz < 0)
		gz = 0;
	if (gz >= GRID_DIM)
		gz = GRID_DIM - 1;

	return gz * GRID_DIM * GRID_DIM + gy * GRID_DIM + gx;
}

static void update_density(OpenOrdContext *ctx, vec3 pos, float sign) {
	int gx = (int)((pos[0] + HALF_VIEW) * VIEW_TO_GRID);
	int gy = (int)((pos[1] + HALF_VIEW) * VIEW_TO_GRID);
	int gz = (int)((pos[2] + HALF_VIEW) * VIEW_TO_GRID);

	for (int k = -RADIUS; k <= RADIUS; k++) {
		for (int j = -RADIUS; j <= RADIUS; j++) {
			for (int i = -RADIUS; i <= RADIUS; i++) {
				int dx = gx + i;
				int dy = gy + j;
				int dz = gz + k;

				if (dx >= 0 && dx < GRID_DIM && dy >= 0 && dy < GRID_DIM &&
					dz >= 0 && dz < GRID_DIM) {
					int idx = dz * GRID_DIM * GRID_DIM + dy * GRID_DIM + dx;
#pragma omp atomic
					ctx->density_grid[idx] +=
						sign * fall_off[i + RADIUS][j + RADIUS][k + RADIUS];
				}
			}
		}
	}
}

static float get_density(OpenOrdContext *ctx, vec3 pos) {
	int gx = (int)((pos[0] + HALF_VIEW) * VIEW_TO_GRID);
	int gy = (int)((pos[1] + HALF_VIEW) * VIEW_TO_GRID);
	int gz = (int)((pos[2] + HALF_VIEW) * VIEW_TO_GRID);

	if (gx < 0 || gx >= GRID_DIM || gy < 0 || gy >= GRID_DIM || gz < 0 ||
		gz >= GRID_DIM)
		return 10000.0f;

	int idx = gz * GRID_DIM * GRID_DIM + gy * GRID_DIM + gx;
	return ctx->density_grid[idx]; // Simplified, originally squared but linear
								   // is often fine for 3D repulsion
}

static float compute_energy(OpenOrdContext *ctx, GraphData *g, int node_idx,
							vec3 pos) {
	float energy = 0.0f;
	float attraction = ctx->stages[ctx->stage_id].attraction;
	float attraction_factor = attraction * attraction * attraction *
							  attraction * 2e-2f; // From original

	igraph_vector_int_t neighbors;
	igraph_vector_int_init(&neighbors, 0);
	igraph_neighbors(&g->g, &neighbors, node_idx, IGRAPH_ALL);

	for (int i = 0; i < igraph_vector_int_size(&neighbors); i++) {
		int neighbor_idx = VECTOR(neighbors)[i];
		vec3 diff;
		glm_vec3_sub(pos, g->nodes[neighbor_idx].position, diff);
		float dist_sq = glm_vec3_norm2(diff);

		if (ctx->stage_id < 2)
			dist_sq *= dist_sq; // Higher power for liquid/expansion
		if (ctx->stage_id == 0)
			dist_sq *= dist_sq; // Liquid

		// Edge weight is 1.0f for now, can be fetched if exists
		float weight = g->edges ? g->edges[i].size : 1.0f;

		energy += weight * attraction_factor * dist_sq;
	}
	igraph_vector_int_destroy(&neighbors);

	energy += get_density(ctx, pos);
	return energy;
}

static void solve_analytic(OpenOrdContext *ctx, GraphData *g, int node_idx,
						   vec3 out_pos) {
	vec3 centroid = {0, 0, 0};
	float total_weight = 0.0f;

	igraph_vector_int_t neighbors;
	igraph_vector_int_init(&neighbors, 0);
	igraph_neighbors(&g->g, &neighbors, node_idx, IGRAPH_ALL);

	for (int i = 0; i < igraph_vector_int_size(&neighbors); i++) {
		int n_idx = VECTOR(neighbors)[i];
		float weight = 1.0f; // Simplified
		total_weight += weight;
		glm_vec3_muladds(g->nodes[n_idx].position, weight, centroid);
	}
	igraph_vector_int_destroy(&neighbors);

	if (total_weight > 0.0f) {
		glm_vec3_divs(centroid, total_weight, centroid);
		float damping = 1.0f - ctx->stages[ctx->stage_id].damping_mult;
		vec3 current = {g->nodes[node_idx].position[0],
						g->nodes[node_idx].position[1],
						g->nodes[node_idx].position[2]};
		glm_vec3_lerp(centroid, current, damping, out_pos);
	} else {
		glm_vec3_copy(g->nodes[node_idx].position, out_pos);
	}
}

bool openord_step(OpenOrdContext *ctx, GraphData *graph) {
	if (ctx->stage_id >= 5)
		return false;

	OpenOrdStage *stage = &ctx->stages[ctx->stage_id];

	// Check transitions
	if (ctx->current_iter >= stage->iterations) {
		ctx->stage_id++;
		ctx->current_iter = 0;
		if (ctx->stage_id >= 5)
			return false;
		stage = &ctx->stages[ctx->stage_id];

		// Between stage logic (resetting parameters)
		if (ctx->stage_id == 2) { // Cooldown
			ctx->min_edges = 12.0f;
		} else if (ctx->stage_id == 3) { // Crunch
			ctx->cut_off_length = ctx->cut_end;
			ctx->min_edges = 1.0f;
		} else if (ctx->stage_id == 4) { // Simmer
			ctx->min_edges = 99.0f;		 // No cuts
		}
	}

	// Update logic
	float temp = stage->temperature;
	float jump = 0.01f * temp;

	// Only update nodes if not simmer (simmer just fine tunes)

#pragma omp parallel for schedule(dynamic)
	for (int i = 0; i < graph->node_count; i++) {
		// Remove from density (thread safe? No, need atomic or separate pass)
		// Original code does sequential updates per processor chunk.
		// We can do approximate density updates or use atomics in density grid
		// (which we added).

		vec3 old_pos;
		glm_vec3_copy(graph->nodes[i].position, old_pos);
		update_density(ctx, old_pos, -1.0f); // Atomic inside

		vec3 analytic_pos;
		solve_analytic(ctx, graph, i, analytic_pos);

		vec3 random_pos;
		float r1 = ((float)rand() / RAND_MAX) - 0.5f;
		float r2 = ((float)rand() / RAND_MAX) - 0.5f;
		float r3 = ((float)rand() / RAND_MAX) - 0.5f;
		random_pos[0] = analytic_pos[0] + r1 * jump;
		random_pos[1] = analytic_pos[1] + r2 * jump;
		random_pos[2] = analytic_pos[2] + r3 * jump;

		float e1 = compute_energy(ctx, graph, i, analytic_pos);
		float e2 = compute_energy(ctx, graph, i, random_pos);

		if (e2 < e1) {
			glm_vec3_copy(random_pos, graph->nodes[i].position);
		} else {
			glm_vec3_copy(analytic_pos, graph->nodes[i].position);
		}

		update_density(ctx, graph->nodes[i].position, 1.0f); // Atomic inside
	}

	// Per-iteration updates
	if (ctx->stage_id == 1) { // Expansion
		if (stage->attraction > 1)
			stage->attraction -= 0.05f;
		if (ctx->min_edges > 12)
			ctx->min_edges -= 0.05f;
		ctx->cut_off_length -= ctx->cut_rate;
		if (stage->damping_mult > 0.1f)
			stage->damping_mult -= 0.005f;
	} else if (ctx->stage_id == 2) { // Cooldown
		if (stage->temperature > 50)
			stage->temperature -= 10.0f;
		if (ctx->cut_off_length > ctx->cut_end)
			ctx->cut_off_length -= ctx->cut_rate * 2.0f;
		if (ctx->min_edges > 1)
			ctx->min_edges -= 0.2f;
	} else if (ctx->stage_id == 4) { // Simmer
		if (stage->temperature > 50)
			stage->temperature -= 2.0f;
	}

	ctx->current_iter++;
	ctx->total_iters++;

	// Sync to layout matrix for other parts of app
	for (int i = 0; i < graph->node_count; i++) {
		MATRIX(graph->current_layout, i, 0) = graph->nodes[i].position[0];
		MATRIX(graph->current_layout, i, 1) = graph->nodes[i].position[1];
		if (igraph_matrix_ncol(&graph->current_layout) > 2)
			MATRIX(graph->current_layout, i, 2) = graph->nodes[i].position[2];
	}

	return true;
}
