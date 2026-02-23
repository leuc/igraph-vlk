#include "layered_sphere.h"

#include <math.h>
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// --- HILBERT CURVE LOGIC ---
void rot(int n, int *x, int *y, int rx, int ry) {
	if (ry == 0) {
		if (rx == 1) {
			*x = n - 1 - *x;
			*y = n - 1 - *y;
		}
		int t = *x;
		*x = *y;
		*y = t;
	}
}

int xy2d(int n, int x, int y) {
	int rx, ry, s, d = 0;
	for (s = n / 2; s > 0; s /= 2) {
		rx = (x & s) > 0;
		ry = (y & s) > 0;
		d += s * s * ((3 * rx) ^ ry);
		rot(s, &x, &y, rx, ry);
	}
	return d;
}

// --- STRUCTS & COMPARATORS ---
typedef struct {
	int comm_id;
	double avg_kcore;
	int node_count;
} CommData;

int compare_communities_kcore(const void *a, const void *b) {
	CommData *commA = (CommData *)a;
	CommData *commB = (CommData *)b;
	double diff = commB->avg_kcore - commA->avg_kcore;
	return (diff > 0) - (diff < 0);
}

typedef struct {
	int id;
	int community_id;
	double density;
} NodePlacement;

int compare_nodes_placement(const void *a, const void *b) {
	NodePlacement *nodeA = (NodePlacement *)a;
	NodePlacement *nodeB = (NodePlacement *)b;
	if (nodeA->community_id != nodeB->community_id) {
		return nodeA->community_id - nodeB->community_id;
	}
	double diff = nodeA->density - nodeB->density;
	return (diff > 0) - (diff < 0);
}

int compare_points(const void *a, const void *b) {
	return ((SpherePoint *)a)->hilbert_dist - ((SpherePoint *)b)->hilbert_dist;
}

// --- MATH & ENERGY HELPERS ---

int get_vector_int_max(const igraph_vector_int_t *v) {
	int max_val = 0;
	for (int i = 0; i < igraph_vector_int_size(v); i++) {
		if (VECTOR(*v)[i] > max_val)
			max_val = VECTOR(*v)[i];
	}
	return max_val == 0 ? 1 : max_val;
}

int find_closest_slot_by_hilbert(SphereGrid *grid, int target_h) {
	int low = 0, high = grid->max_slots - 1;
	while (low < high) {
		int mid = low + (high - low) / 2;
		if (grid->slots[mid].hilbert_dist < target_h)
			low = mid + 1;
		else
			high = mid;
	}
	int best_idx = low;
	if (low > 0 && abs(grid->slots[low - 1].hilbert_dist - target_h) <
					   abs(grid->slots[low].hilbert_dist - target_h)) {
		best_idx = low - 1;
	}
	return best_idx;
}

// ---------------------------------------------------------
// DELTA CALCULATION: INTRA-SPHERE (Euclidean Squared Distance)
// ---------------------------------------------------------
double calculate_move_delta_intra(const igraph_t *ig,
								  const igraph_matrix_t *layout,
								  LayeredSphereContext *ctx, int u,
								  int target_sphere_s, int target_slot_k) {
	int v = ctx->grids[target_sphere_s].slot_occupant[target_slot_k];
	double current_score = 0.0, potential_score = 0.0;

	double tx = ctx->grids[target_sphere_s].slots[target_slot_k].x;
	double ty = ctx->grids[target_sphere_s].slots[target_slot_k].y;
	double tz = ctx->grids[target_sphere_s].slots[target_slot_k].z;

	int u_slot = ctx->node_to_slot_idx[u];
	double ux = ctx->grids[target_sphere_s].slots[u_slot].x;
	double uy = ctx->grids[target_sphere_s].slots[u_slot].y;
	double uz = ctx->grids[target_sphere_s].slots[u_slot].z;

	igraph_vector_int_t neis;
	igraph_vector_int_init(&neis, 0);
	igraph_incident(ig, &neis, u, IGRAPH_ALL);
	for (int i = 0; i < igraph_vector_int_size(&neis); i++) {
		igraph_integer_t from, to;
		igraph_edge(ig, VECTOR(neis)[i], &from, &to);
		int neighbor = (from == u) ? to : from;

		if (neighbor == v ||
			ctx->node_to_sphere_id[neighbor] != target_sphere_s)
			continue;

		double nx = MATRIX(*layout, neighbor, 0),
			   ny = MATRIX(*layout, neighbor, 1),
			   nz = MATRIX(*layout, neighbor, 2);
		current_score += (ux - nx) * (ux - nx) + (uy - ny) * (uy - ny) +
						 (uz - nz) * (uz - nz);
		potential_score += (tx - nx) * (tx - nx) + (ty - ny) * (ty - ny) +
						   (tz - nz) * (tz - nz);
	}
	igraph_vector_int_destroy(&neis);

	if (v != -1) {
		igraph_vector_int_init(&neis, 0);
		igraph_incident(ig, &neis, v, IGRAPH_ALL);
		for (int i = 0; i < igraph_vector_int_size(&neis); i++) {
			igraph_integer_t from, to;
			igraph_edge(ig, VECTOR(neis)[i], &from, &to);
			int neighbor = (from == v) ? to : from;
			if (neighbor == u ||
				ctx->node_to_sphere_id[neighbor] != target_sphere_s)
				continue;

			double nx = MATRIX(*layout, neighbor, 0),
				   ny = MATRIX(*layout, neighbor, 1),
				   nz = MATRIX(*layout, neighbor, 2);
			current_score += (tx - nx) * (tx - nx) + (ty - ny) * (ty - ny) +
							 (tz - nz) * (tz - nz);
			potential_score += (ux - nx) * (ux - nx) + (uy - ny) * (uy - ny) +
							   (uz - nz) * (uz - nz);
		}
		igraph_vector_int_destroy(&neis);
	}
	return potential_score - current_score;
}

// ---------------------------------------------------------
// DELTA CALCULATION: INTER-SPHERE (Angular Dot-Product)
// ---------------------------------------------------------
double calculate_move_delta_inter(const igraph_t *ig,
								  const igraph_matrix_t *layout,
								  LayeredSphereContext *ctx, int u,
								  int target_sphere_s, int target_slot_k) {
	int v = ctx->grids[target_sphere_s].slot_occupant[target_slot_k];
	double current_score = 0.0, potential_score = 0.0;
	double r = ctx->grids[target_sphere_s].radius;

	double tx = ctx->grids[target_sphere_s].slots[target_slot_k].x / r;
	double ty = ctx->grids[target_sphere_s].slots[target_slot_k].y / r;
	double tz = ctx->grids[target_sphere_s].slots[target_slot_k].z / r;

	int u_slot = ctx->node_to_slot_idx[u];
	double ux = ctx->grids[target_sphere_s].slots[u_slot].x / r;
	double uy = ctx->grids[target_sphere_s].slots[u_slot].y / r;
	double uz = ctx->grids[target_sphere_s].slots[u_slot].z / r;

	igraph_vector_int_t neis;
	igraph_vector_int_init(&neis, 0);
	igraph_incident(ig, &neis, u, IGRAPH_ALL);
	for (int i = 0; i < igraph_vector_int_size(&neis); i++) {
		igraph_integer_t from, to;
		igraph_edge(ig, VECTOR(neis)[i], &from, &to);
		int neighbor = (from == u) ? to : from;
		if (neighbor == v)
			continue;

		double nx = MATRIX(*layout, neighbor, 0),
			   ny = MATRIX(*layout, neighbor, 1),
			   nz = MATRIX(*layout, neighbor, 2);
		double n_len = sqrt(nx * nx + ny * ny + nz * nz);
		if (n_len < 0.001)
			continue;
		nx /= n_len;
		ny /= n_len;
		nz /= n_len;

		current_score += (1.0 - (ux * nx + uy * ny + uz * nz));
		potential_score += (1.0 - (tx * nx + ty * ny + tz * nz));
	}
	igraph_vector_int_destroy(&neis);

	if (v != -1) {
		igraph_vector_int_init(&neis, 0);
		igraph_incident(ig, &neis, v, IGRAPH_ALL);
		for (int i = 0; i < igraph_vector_int_size(&neis); i++) {
			igraph_integer_t from, to;
			igraph_edge(ig, VECTOR(neis)[i], &from, &to);
			int neighbor = (from == v) ? to : from;
			if (neighbor == u)
				continue;

			double nx = MATRIX(*layout, neighbor, 0),
				   ny = MATRIX(*layout, neighbor, 1),
				   nz = MATRIX(*layout, neighbor, 2);
			double n_len = sqrt(nx * nx + ny * ny + nz * nz);
			if (n_len < 0.001)
				continue;
			nx /= n_len;
			ny /= n_len;
			nz /= n_len;

			current_score += (1.0 - (tx * nx + ty * ny + tz * nz));
			potential_score += (1.0 - (ux * nx + uy * ny + uz * nz));
		}
		igraph_vector_int_destroy(&neis);
	}
	return potential_score - current_score;
}

// --- MAIN LIFECYCLE ---

void layered_sphere_init(LayeredSphereContext *ctx, int node_count) {
	memset(ctx, 0, sizeof(LayeredSphereContext));
	ctx->initialized = true;
	ctx->phase = PHASE_INIT;
	ctx->grids = NULL;
	ctx->node_to_sphere_id = NULL;
	ctx->node_to_slot_idx = NULL;
}

void layered_sphere_cleanup(LayeredSphereContext *ctx) {
	if (ctx->initialized) {
		if (ctx->node_to_sphere_id)
			free(ctx->node_to_sphere_id);
		if (ctx->node_to_slot_idx)
			free(ctx->node_to_slot_idx);
		if (ctx->grids) {
			for (int s = 0; s < ctx->num_spheres; s++) {
				if (ctx->grids[s].slots)
					free(ctx->grids[s].slots);
				if (ctx->grids[s].slot_occupant)
					free(ctx->grids[s].slot_occupant);
			}
			free(ctx->grids);
		}
	}
	ctx->initialized = false;
}

bool layered_sphere_step(LayeredSphereContext *ctx, GraphData *graph) {
	if (!ctx->initialized || ctx->phase == PHASE_DONE)
		return false;

	igraph_t *ig = &graph->g;
	int vcount = graph->node_count;
	int ecount = igraph_ecount(ig);
	int hilbert_res = 32768;

	if (ctx->phase == PHASE_INIT) {
		ctx->node_to_sphere_id = malloc(vcount * sizeof(int));
		ctx->node_to_slot_idx = malloc(vcount * sizeof(int));
		ctx->node_to_comm_id = malloc(vcount * sizeof(int));

		// --- NEW: Create a temporary undirected clone for analysis ---
		igraph_t undirected_ig;
		igraph_copy(&undirected_ig, ig);
		// Collapse directed edges (A->B and B->A become a single A-B edge).
		// NULL drops attributes.
		igraph_to_undirected(&undirected_ig, IGRAPH_TO_UNDIRECTED_COLLAPSE,
							 NULL);

		// 1. Coreness & Community Detection (Using the undirected clone)
		igraph_vector_int_t coreness;
		igraph_vector_int_init(&coreness, vcount);
		igraph_coreness(&undirected_ig, &coreness, IGRAPH_ALL);

		igraph_vector_int_t membership;
		igraph_vector_int_init(&membership, vcount);
		double graph_density =
			(vcount > 1) ? (2.0 * ecount) / ((double)vcount * (vcount - 1))
						 : 0.0;
		double cpm_resolution = fmax(graph_density * 3.0, 0.001);

		igraph_community_leiden(&undirected_ig, NULL, NULL, cpm_resolution,
								0.01, true, 2, &membership, NULL, NULL);

		int num_communities = get_vector_int_max(&membership) + 1;
		printf("[DEBUG] Leiden CPM Resolution %.4f. Found %d communities.\n",
			   cpm_resolution, num_communities);

		// 2. Aggregate Community Metrics
		CommData *comms = calloc(num_communities, sizeof(CommData));
		for (int i = 0; i < vcount; i++) {
			int c = VECTOR(membership)[i];
			comms[c].comm_id = c;
			comms[c].avg_kcore += VECTOR(coreness)[i];
			comms[c].node_count++;
		}
		for (int i = 0; i < num_communities; i++) {
			if (comms[i].node_count > 0)
				comms[i].avg_kcore /= comms[i].node_count;
		}
		qsort(comms, num_communities, sizeof(CommData),
			  compare_communities_kcore);

		// 3. Dynamic Sphere Generation (With Strict Nucleus)

		// The "Nucleus" (Sphere 0) is sized exactly to fit the single densest
		// community.
		int nucleus_capacity = comms[0].node_count;

		// The outer shells grow quadratically, based on the remaining
		// population
		int remaining_nodes = vcount - nucleus_capacity;
		int base_capacity = (int)fmax(15.0, remaining_nodes * 0.015);

		int current_sphere = 0;
		int current_load = 0;
		int *comm_to_sphere = malloc(num_communities * sizeof(int));

		for (int i = 0; i < num_communities; i++) {
			int c_size = comms[i].node_count;
			if (c_size == 0)
				continue;

			// Evaluate how much room the current sphere has
			int sphere_capacity;
			if (current_sphere == 0) {
				sphere_capacity = nucleus_capacity; // Strictly locked to Comm 0
			} else {
				// Outer spheres grow quadratically: C = Base * s^2
				sphere_capacity =
					base_capacity * current_sphere * current_sphere;
			}

			// If adding this community overflows the sphere (and it's not the
			// first one added to it)
			if (current_load > 0 && current_load + c_size > sphere_capacity) {
				current_sphere++;
				current_load = 0; // Reset load for the new sphere
			}

			comm_to_sphere[comms[i].comm_id] = current_sphere;
			current_load += c_size;
		}

		ctx->num_spheres = current_sphere + 1;
		printf("[DEBUG] Generated %d spheres. Nucleus is strictly %d nodes. "
			   "Shell base: %d\n",
			   ctx->num_spheres, nucleus_capacity, base_capacity);

		for (int i = 0; i < vcount; i++)
			ctx->node_to_sphere_id[i] = comm_to_sphere[VECTOR(membership)[i]];

		free(comms);
		free(comm_to_sphere);
		igraph_vector_int_destroy(&coreness);

		// 4. Build Sparse Grids & Assign Hilbert

		igraph_vector_t transitivity;
		igraph_vector_init(&transitivity, vcount);
		// Use the undirected clone here too!
		igraph_transitivity_local_undirected(&undirected_ig, &transitivity,
											 igraph_vss_all(),
											 IGRAPH_TRANSITIVITY_ZERO);

		// --- NEW: Destroy the temporary undirected clone ---
		igraph_destroy(&undirected_ig);

		ctx->grids = calloc(ctx->num_spheres, sizeof(SphereGrid));
		double current_radius = 0.0;

		for (int s = 0; s < ctx->num_spheres; s++) {
			int n_in_group = 0;
			for (int i = 0; i < vcount; i++)
				if (ctx->node_to_sphere_id[i] == s)
					n_in_group++;
			if (n_in_group == 0)
				continue;

			double required_area = n_in_group * 40.0;
			double needed_r = sqrt(required_area / (4.0 * M_PI));

			if (s == 0) {
				// THE NUCLEUS: No gap. It sits exactly at its mathematically
				// needed radius We floor it at 5.0 so a tiny 3-node community
				// doesn't collapse into a singularity
				current_radius = fmax(5.0, needed_r);
			} else {
				// THE SHELLS: Add a logarithmic structural gap from the
				// previous sphere
				double log_gap = 8.0 + (20.0 / log2(s + 2.0));

				// Radius must clear the previous sphere + gap, OR fit its own
				// nodes (whichever is bigger)
				current_radius = fmax(current_radius + log_gap, needed_r);
			}

			ctx->grids[s].radius = current_radius;

			// MATH: Surface Area Proportional Grid Scaling
			int M_s = (int)fmin(
				100000,
				fmax(n_in_group * 3.0,
					 (4.0 * M_PI * current_radius * current_radius) / 20.0));
			ctx->grids[s].max_slots = M_s;
			ctx->grids[s].slot_occupant = malloc(M_s * sizeof(int));
			ctx->grids[s].slots = malloc(M_s * sizeof(SpherePoint));
			for (int k = 0; k < M_s; k++)
				ctx->grids[s].slot_occupant[k] = -1;

			// Generate dense Fibonacci grid
			for (int i = 0; i < M_s; i++) {
				double phi = acos(1.0 - 2.0 * (i + 0.5) / M_s);
				double theta = M_PI * (1.0 + sqrt(5.0)) * i;
				ctx->grids[s].slots[i].x =
					current_radius * cos(theta) * sin(phi);
				ctx->grids[s].slots[i].y =
					current_radius * sin(theta) * sin(phi);
				ctx->grids[s].slots[i].z = current_radius * cos(phi);
				double nx = fmod(theta, 2 * M_PI) / (2 * M_PI);
				if (nx < 0)
					nx += 1.0;
				ctx->grids[s].slots[i].hilbert_dist =
					xy2d(hilbert_res, (int)(nx * (hilbert_res - 1)),
						 (int)((phi / M_PI) * (hilbert_res - 1)));
			}
			qsort(ctx->grids[s].slots, M_s, sizeof(SpherePoint),
				  compare_points);

			NodePlacement *group_nodes =
				malloc(n_in_group * sizeof(NodePlacement));
			int g_idx = 0;
			for (int i = 0; i < vcount; i++) {
				if (ctx->node_to_sphere_id[i] == s) {
					group_nodes[g_idx].id = i;
					group_nodes[g_idx].community_id = VECTOR(membership)[i];
					group_nodes[g_idx].density = VECTOR(transitivity)[i];
					g_idx++;
				}
			}
			qsort(group_nodes, n_in_group, sizeof(NodePlacement),
				  compare_nodes_placement);

			int step = M_s / n_in_group;
			for (int i = 0; i < n_in_group; i++) {
				int nid = group_nodes[i].id;
				int sid = fmin(M_s - 1, i * step);
				ctx->grids[s].slot_occupant[sid] = nid;
				ctx->node_to_slot_idx[nid] = sid;
				MATRIX(graph->current_layout, nid, 0) =
					ctx->grids[s].slots[sid].x;
				MATRIX(graph->current_layout, nid, 1) =
					ctx->grids[s].slots[sid].y;
				MATRIX(graph->current_layout, nid, 2) =
					ctx->grids[s].slots[sid].z;
			}
			free(group_nodes);
		}

		for (int i = 0; i < vcount; i++) {
			graph->nodes[i].position[0] = MATRIX(graph->current_layout, i, 0);
			graph->nodes[i].position[1] = MATRIX(graph->current_layout, i, 1);
			graph->nodes[i].position[2] = MATRIX(graph->current_layout, i, 2);
		}

		igraph_vector_int_destroy(&membership);
		igraph_vector_destroy(&transitivity);

		ctx->phase = PHASE_INTRA_SPHERE;
		ctx->phase_iter = 0;
		return true;
	}

	// ---------------------------------------------------------
	// BARYCENTER ENGINE (With Hilbert Damping)
	// ---------------------------------------------------------
	int local_moves = 0;
	bool is_intra = (ctx->phase == PHASE_INTRA_SPHERE);
	int start_s = is_intra ? 0 : (ctx->inter_sphere_pass % 2);
	int step_s = is_intra ? 1 : 2;

	// Decrease step size as iterations progress to guarantee convergence
	double damping_factor = fmax(0.05, 0.4 * pow(0.95, ctx->phase_iter));

#pragma omp parallel for schedule(dynamic) reduction(+ : local_moves)
	for (int s = start_s; s < ctx->num_spheres; s += step_s) {
		double radius = ctx->grids[s].radius;

		for (int u = 0; u < vcount; u++) {
			if (ctx->node_to_sphere_id[u] != s)
				continue;
			int current_slot = ctx->node_to_slot_idx[u];

			double bx = 0, by = 0, bz = 0;
			int neighbor_count = 0;

			igraph_vector_int_t neis;
			igraph_vector_int_init(&neis, 0);
			igraph_incident(ig, &neis, u, IGRAPH_ALL);

			for (int j = 0; j < igraph_vector_int_size(&neis); j++) {
				igraph_integer_t from, to;
				igraph_edge(ig, VECTOR(neis)[j], &from, &to);
				int neighbor = (from == u) ? to : from;
				int n_sphere = ctx->node_to_sphere_id[neighbor];

				if (is_intra) {
					if (n_sphere != s)
						continue;
					bx += MATRIX(graph->current_layout, neighbor, 0);
					by += MATRIX(graph->current_layout, neighbor, 1);
					bz += MATRIX(graph->current_layout, neighbor, 2);
				} else {
					double nx = MATRIX(graph->current_layout, neighbor, 0);
					double ny = MATRIX(graph->current_layout, neighbor, 1);
					double nz = MATRIX(graph->current_layout, neighbor, 2);
					double n_len = sqrt(nx * nx + ny * ny + nz * nz);
					if (n_len > 0.001) {
						bx += (nx / n_len);
						by += (ny / n_len);
						bz += (nz / n_len);
					}
				}
				neighbor_count++;
			}
			igraph_vector_int_destroy(&neis);

			if (neighbor_count == 0)
				continue;

			double len = sqrt(bx * bx + by * by + bz * bz);
			if (len < 0.0001)
				continue;
			bx = (bx / len) * radius;
			by = (by / len) * radius;
			bz = (bz / len) * radius;

			double phi = acos(bz / radius);
			double theta = atan2(by, bx);
			if (theta < 0)
				theta += 2 * M_PI;

			int target_h = xy2d(hilbert_res,
								(int)((theta / (2 * M_PI)) * (hilbert_res - 1)),
								(int)((phi / M_PI) * (hilbert_res - 1)));

			// Apply Damping to prevent thrashing
			int current_h = ctx->grids[s].slots[current_slot].hilbert_dist;
			int total_h = hilbert_res * hilbert_res;
			int h_delta = target_h - current_h;

			if (h_delta > total_h / 2)
				h_delta -= total_h;
			if (h_delta < -total_h / 2)
				h_delta += total_h;

			int damped_h = current_h + (int)(h_delta * damping_factor);
			if (damped_h < 0)
				damped_h += total_h;
			if (damped_h >= total_h)
				damped_h -= total_h;

			int target_slot =
				find_closest_slot_by_hilbert(&ctx->grids[s], damped_h);
			if (target_slot == current_slot)
				continue;

			double delta;
			if (is_intra)
				delta = calculate_move_delta_intra(ig, &graph->current_layout,
												   ctx, u, s, target_slot);
			else
				delta = calculate_move_delta_inter(ig, &graph->current_layout,
												   ctx, u, s, target_slot);

			if (delta < -0.001) {
				int v = ctx->grids[s].slot_occupant[target_slot];

				MATRIX(graph->current_layout, u, 0) =
					ctx->grids[s].slots[target_slot].x;
				MATRIX(graph->current_layout, u, 1) =
					ctx->grids[s].slots[target_slot].y;
				MATRIX(graph->current_layout, u, 2) =
					ctx->grids[s].slots[target_slot].z;
				ctx->grids[s].slot_occupant[target_slot] = u;
				ctx->node_to_slot_idx[u] = target_slot;

				if (v != -1) {
					MATRIX(graph->current_layout, v, 0) =
						ctx->grids[s].slots[current_slot].x;
					MATRIX(graph->current_layout, v, 1) =
						ctx->grids[s].slots[current_slot].y;
					MATRIX(graph->current_layout, v, 2) =
						ctx->grids[s].slots[current_slot].z;
					ctx->grids[s].slot_occupant[current_slot] = v;
					ctx->node_to_slot_idx[v] = current_slot;

					graph->nodes[v].position[0] =
						MATRIX(graph->current_layout, v, 0);
					graph->nodes[v].position[1] =
						MATRIX(graph->current_layout, v, 1);
					graph->nodes[v].position[2] =
						MATRIX(graph->current_layout, v, 2);
				} else {
					ctx->grids[s].slot_occupant[current_slot] = -1;
				}

				graph->nodes[u].position[0] =
					MATRIX(graph->current_layout, u, 0);
				graph->nodes[u].position[1] =
					MATRIX(graph->current_layout, u, 1);
				graph->nodes[u].position[2] =
					MATRIX(graph->current_layout, u, 2);

				local_moves++;
			}
		}
	}

	if (ctx->phase == PHASE_INTRA_SPHERE) {
		printf("[DEBUG] Phase 1 (Intra) Iter %d: %d Barycenter Moves (Damping: "
			   "%.2f)\n",
			   ctx->phase_iter, local_moves, damping_factor);
		if (local_moves == 0 || ctx->phase_iter > 50) {
			ctx->phase = PHASE_INTER_SPHERE;
			ctx->phase_iter = 0;
		} else {
			ctx->phase_iter++;
		}
	} else if (ctx->phase == PHASE_INTER_SPHERE) {
		printf("[DEBUG] Phase 2 (Inter) Iter %d (%s): %d Barycenter Moves "
			   "(Damping: %.2f)\n",
			   ctx->phase_iter,
			   (ctx->inter_sphere_pass % 2 == 0) ? "Even" : "Odd", local_moves,
			   damping_factor);

		ctx->inter_sphere_pass++;
		if ((local_moves == 0 && (ctx->inter_sphere_pass % 2 == 0)) ||
			ctx->phase_iter > 100) {
			ctx->phase = PHASE_DONE;
			return false;
		}
		ctx->phase_iter++;
	}

	ctx->current_iter++;
	return true;
}

const char *layered_sphere_get_stage_name(LayeredSphereContext *ctx) {
	static char stage_name[128];
	if (!ctx || !ctx->initialized)
		return "Layered Sphere - Uninitialized";

	switch (ctx->phase) {
	case PHASE_INIT:
		return "Phase 0: Dynamic Capacity Generation";
	case PHASE_INTRA_SPHERE:
		snprintf(stage_name, sizeof(stage_name),
				 "Phase 1: Intra-Sphere Damped Descent (%d Spheres)",
				 ctx->num_spheres);
		return stage_name;
	case PHASE_INTER_SPHERE:
		snprintf(stage_name, sizeof(stage_name),
				 "Phase 2: Inter-Sphere Damped Alignment (%s Pairs)",
				 (ctx->inter_sphere_pass % 2 == 0) ? "Even" : "Odd");
		return stage_name;
	case PHASE_DONE:
		return "Optimization Complete";
	default:
		return "Unknown Phase";
	}
}
