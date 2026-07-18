/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "graph/layered_sphere.h"
#include "app_state.h"
#include "graph/layered_sphere_common.h"

#include <math.h>
#ifdef _OPENMP
#include <omp.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <igraph_progress.h>
#include <igraph_step.h>

static bool layered_sphere_iterate(LayeredSphereContext *ctx, const igraph_t *ig)
{
	if (ctx->phase == PHASE_DONE)
		return false;

	int vcount = ctx->vcount;
	int hilbert_res = HILBERT_RES;

	if (ctx->phase == PHASE_INIT) {
		ctx->node_to_sphere_id = malloc(vcount * sizeof(int));
		ctx->node_to_slot_idx = malloc(vcount * sizeof(int));

		igraph_t undirected_ig;
		igraph_copy(&undirected_ig, ig);
		igraph_to_undirected(&undirected_ig, IGRAPH_TO_UNDIRECTED_COLLAPSE, NULL);

		igraph_vector_int_t coreness;
		if (igraph_vector_int_init(&coreness, vcount) != IGRAPH_SUCCESS) {
			igraph_destroy(&undirected_ig);
			return false;
		}
		igraph_coreness(&undirected_ig, &coreness, IGRAPH_ALL);

		igraph_vector_int_t membership;
		if (igraph_vector_int_init(&membership, vcount) != IGRAPH_SUCCESS) {
			igraph_vector_int_destroy(&coreness);
			igraph_destroy(&undirected_ig);
			return false;
		}

		igraph_integer_t ecount = igraph_ecount(ig);
		double graph_density = (vcount > 1) ? (2.0 * ecount) / ((double)vcount * (vcount - 1)) : 0.0;
		double cpm_resolution = fmax(graph_density * 3.0, 0.001);

		if (igraph_is_directed(ig)) {
			igraph_community_leiden(ig, NULL, NULL, NULL, cpm_resolution, 0.01, 0, -1, &membership, NULL, NULL);
		} else {
			igraph_community_leiden_simple(ig, NULL, IGRAPH_LEIDEN_OBJECTIVE_CPM, cpm_resolution, 0.01, 0, -1, &membership, NULL, NULL);
		}

		int num_communities = get_vector_int_max(&membership) + 1;

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
		qsort(comms, num_communities, sizeof(CommData), compare_communities_kcore);

		int *comm_to_sphere = malloc(num_communities * sizeof(int));
		ctx->num_spheres = bucket_communities_into_spheres(comms, num_communities, vcount, comm_to_sphere);

		for (int i = 0; i < vcount; i++)
			ctx->node_to_sphere_id[i] = comm_to_sphere[VECTOR(membership)[i]];

		free(comms);
		free(comm_to_sphere);
		igraph_vector_int_destroy(&coreness);

		igraph_vector_t transitivity;
		if (igraph_vector_init(&transitivity, vcount) != IGRAPH_SUCCESS) {
			igraph_vector_int_destroy(&membership);
			igraph_destroy(&undirected_ig);
			return false;
		}
		igraph_transitivity_local_undirected(&undirected_ig, &transitivity, igraph_vss_all(), IGRAPH_TRANSITIVITY_ZERO);

		int *intra_degree = calloc(vcount, sizeof(int));
		igraph_integer_t uecount = igraph_ecount(&undirected_ig);
		for (int e = 0; e < uecount; e++) {
			igraph_integer_t from, to;
			igraph_edge(&undirected_ig, e, &from, &to);
			if (VECTOR(membership)[from] == VECTOR(membership)[to]) {
				intra_degree[from]++;
				intra_degree[to]++;
			}
		}

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

			current_radius = sphere_radius_for(s, n_in_group, current_radius);

			if (!build_sphere_grid(&ctx->grids[s], n_in_group, current_radius, hilbert_res)) {
				free(intra_degree);
				igraph_vector_int_destroy(&membership);
				igraph_vector_destroy(&transitivity);
				return false;
			}

			int placed_count;
			NodePlacement *group_nodes = placement_order_for_sphere(ctx, s, n_in_group, &membership, &transitivity, intra_degree, &placed_count);
			seed_slots_for_sphere(ctx, s, group_nodes, placed_count);
			free(group_nodes);
		}

		free(intra_degree);
		igraph_vector_int_destroy(&membership);
		igraph_vector_destroy(&transitivity);

		ctx->phase = PHASE_INTRA_SPHERE;
		ctx->phase_iter = 0;
		return igraph_step(ctx->layout, NULL) == IGRAPH_SUCCESS;
	}

	int local_moves = 0;
	bool is_intra = (ctx->phase == PHASE_INTRA_SPHERE);
	int start_s = is_intra ? 0 : (ctx->inter_sphere_pass % 2);
	int step_s = is_intra ? 1 : 2;

	double damping_factor = fmax(0.05, 0.4 * pow(0.95, ctx->phase_iter));

#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic) reduction(+ : local_moves)
#endif
	for (int s = start_s; s < ctx->num_spheres; s += step_s) {
		for (int u = 0; u < vcount; u++) {
			if (ctx->node_to_sphere_id[u] != s)
				continue;
			int current_slot = ctx->node_to_slot_idx[u];

			int target_slot = node_hilbert_target(ig, ctx->layout, ctx, u, s, is_intra, damping_factor, hilbert_res);
			if (target_slot == current_slot)
				continue;

			try_move_node(ig, ctx->layout, ctx, u, s, target_slot, current_slot, is_intra, &local_moves);
		}
	}

	advance_phase(ctx, local_moves);
	if (ctx->phase != PHASE_DONE) {
		ctx->current_iter++;
	}

	if (igraph_step(ctx->layout, NULL) != IGRAPH_SUCCESS)
		return false;

	return (ctx->phase != PHASE_DONE);
}

void *compute_layout_layered_sphere(ExecutionContext *ctx)
{
	igraph_t *graph = &ctx->app_state->current_graph.g;
	if (!ctx->app_state->current_graph.graph_initialized) {
		fprintf(stderr, "[%s] Graph not initialized\n", __func__);
		return NULL;
	}
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = IGRAPH_MALLOC(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 3) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		return NULL;
	}

	LayeredSphereContext *ls_ctx = calloc(1, sizeof(LayeredSphereContext));
	ls_ctx->vcount = vcount;
	ls_ctx->layout = result;
	ls_ctx->phase = PHASE_INIT;
	ls_ctx->current_iter = 0;

	igraph_progress("Layered Sphere layout", 0.0, NULL);

	const double intra_weight = 50.0;
	const double inter_weight = 50.0;

	while (layered_sphere_iterate(ls_ctx, graph)) {
		double pct = 0.0;
		if (ls_ctx->phase == PHASE_INTRA_SPHERE) {
			pct = intra_weight * ((double)ls_ctx->phase_iter / MAX_INTRA_ITERS);
		} else if (ls_ctx->phase == PHASE_INTER_SPHERE) {
			pct = intra_weight + inter_weight * ((double)ls_ctx->phase_iter / MAX_INTER_ITERS);
		}
		igraph_progress("Layered Sphere layout", pct, NULL);
	}

	igraph_progress("Layered Sphere layout", 100.0, NULL);

	layered_sphere_cleanup(ls_ctx);

	return result;
}
