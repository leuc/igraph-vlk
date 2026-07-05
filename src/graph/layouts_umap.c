/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#define _GNU_SOURCE

#include "app_state.h"
#include "graph/wrappers_layout.h"
#include <igraph.h>
#include <igraph_constants.h>
#include <stdio.h>
#include <stdlib.h>

// ============================================================================
// UMAP LAYOUT (2D)
// ============================================================================
void *compute_igraph_layout_umap(ExecutionContext *ctx)
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
	 * igraph_layout_umap() parameters:
	 * @param graph           The graph to layout
	 * @param res             Output matrix (will be resized to vcount x 2)
	 * @param use_seed        Whether to use initial positions
	 * @param distances      Precomputed distances (NULL = compute from graph)
	 * @param min_dist        Minimum distance between points (default: 0.5)
	 *                       Higher = more spread, lower = more compact
	 * @param epochs         Training epochs (default: 500)
	 * @param distances_are_weights  If true, treat distances as edge weights
	 *
	 * Uniform Manifold Approximation and Projection
	 * Non-linear dimensionality reduction good for clusters
	 * EXPERIMENTAL in igraph
	 */
	igraph_real_t min_dist = 0.5;
	igraph_int_t epochs = 500;

	igraph_simplify(graph, true, true, NULL);

	igraph_error_t code = igraph_layout_umap(graph, result,
											 use_seed, // use_seed
											 NULL,	   // distances: compute from graph
											 min_dist, // min_dist: 0.5 default
											 epochs,   // epochs: 500 default
											 0		   // distances_are_weights: false
	);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}
	return result;
}

// ============================================================================
// UMAP LAYOUT (3D)
// ============================================================================
void *compute_igraph_layout_umap_3d(ExecutionContext *ctx)
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

	igraph_real_t min_dist = 0.5;
	igraph_int_t epochs = 500;

	igraph_simplify(graph, true, true, NULL);

	igraph_error_t code = igraph_layout_umap_3d(graph, result, use_seed, NULL, min_dist, epochs, 0);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}
	return result;
}
