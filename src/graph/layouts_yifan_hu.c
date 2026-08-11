/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#define _GNU_SOURCE

#include "app_state.h"
#include "graph/wrappers_layout.h"
#include <igraph.h>
#include <igraph_constants.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

void *compute_igraph_layout_yifan_hu(ExecutionContext *ctx)
{
	igraph_t *graph = &ctx->app_state->current_graph.g;
	if (!ctx->app_state->current_graph.graph_initialized) {
		fprintf(stderr, "[%s] Graph not initialized\n", __func__);
		return NULL;
	}

	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = IGRAPH_MALLOC(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 2) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		return NULL;
	}

	igraph_bool_t use_seed = layout_fill_seed(ctx, result, vcount);

	/*
	 * igraph_layout_yifan_hu() parameters:
	 * @param graph           The graph to layout
	 * @param res             Output matrix (will be resized to vcount x 2)
	 * @param use_seed        Whether to use initial positions
	 * @param maxiter         Maximum iterations (default: 500)
	 * @param repulsive_exponent  Repulsion exponent (-1 = auto-calculate)
	 * @param natural_length   Natural edge length (-1 = auto-calculate)
	 * @param step            Initial step size (default: 0.1)
	 * @param adaptive_cooling  Use adaptive cooling (default: true)
	 * @param tolerance       Convergence tolerance (default: 0.001)
	 * @param quadtree_scheme Barnes-Hut quadtree scheme
	 * @param max_qtree_level Maximum quadtree depth
	 * @param beautify_leaves Make leaf nodes more uniform
	 * @param weights         Edge weights (NULL = unit weight)
	 * @param minx/maxx/miny/maxy  Bounding box constraints
	 *
	 * Uses Barnes-Hut optimization for O(n log n) performance
	 * Efficient for large graphs
	 */
	igraph_int_t maxiter = 500;
	igraph_real_t repulsive_exponent = 1.0;
	igraph_real_t natural_length = -1.0;
	igraph_real_t step = 0.1;
	igraph_bool_t adaptive_cooling = 1;
	igraph_real_t tolerance = 0.001;
	igraph_quadtree_scheme_t quadtree_scheme = IGRAPH_QUADTREE_NORMAL;
	igraph_bool_t beautify_leaves = 0;

	igraph_vector_t weights;
	bool has_weights = graph_build_edge_weights(graph, &weights);

	igraph_error_t code = igraph_layout_yifan_hu(graph, result,
												 use_seed,						// use_seed
												 maxiter,						// maxiter: 500
												 repulsive_exponent,			// -1 = auto
												 natural_length,				// -1 = auto
												 step,							// step: 0.1
												 adaptive_cooling,				// adaptive cooling
												 tolerance,						// tolerance
												 quadtree_scheme,				// quadtree
												 beautify_leaves,				// beautify
												 has_weights ? &weights : NULL, // weights
												 NULL, NULL, NULL, NULL			// bounds
	);

	if (has_weights)
		igraph_vector_destroy(&weights);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}
	return result;
}

void *compute_igraph_layout_yifan_hu_3d(ExecutionContext *ctx)
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

	igraph_bool_t use_seed = layout_fill_seed(ctx, result, vcount);

	/*
	 * igraph_layout_yifan_hu_3d() parameters: same as 2D with:
	 * @param res             Output matrix (vcount x 3)
	 * @param weights         Only parameter (others inherited)
	 */
	igraph_int_t maxiter = 500;
	igraph_real_t repulsive_exponent = 1.0;
	igraph_real_t natural_length = -1.0;
	igraph_real_t step = 0.1;
	igraph_bool_t adaptive_cooling = 1;
	igraph_real_t tolerance = 0.001;
	igraph_quadtree_scheme_t quadtree_scheme = IGRAPH_QUADTREE_NORMAL;
	igraph_bool_t beautify_leaves = 0;

	igraph_vector_t weights;
	bool has_weights = graph_build_edge_weights(graph, &weights);

	igraph_error_t code = igraph_layout_yifan_hu_3d(graph, result, use_seed, maxiter, repulsive_exponent, natural_length, step, adaptive_cooling, tolerance, quadtree_scheme, beautify_leaves,
													has_weights ? &weights : NULL // weights
	);

	if (has_weights)
		igraph_vector_destroy(&weights);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}
	return result;
}
