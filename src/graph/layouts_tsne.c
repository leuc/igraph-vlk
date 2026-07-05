/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#define _GNU_SOURCE

#include "app_state.h"
#include "graph/wrappers_layout.h"
#include "interaction/state.h"
#include "vulkan/renderer.h"
#include <float.h>
#include <igraph.h>
#include <igraph_constants.h>
#include <igraph_progress.h>
#include <stdio.h>
#include <stdlib.h>

// ============================================================================
// T-SNE LAYOUT (2D)
// ============================================================================
void *compute_igraph_layout_bhtsne(ExecutionContext *ctx)
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

	/*
	 * igraph_layout_tsne() parameters:
	 * @param graph     The graph to layout
	 * @param res       Output matrix (will be resized to vcount x 2)
	 * @param use_seed  Whether to use initial positions (0 = random start)
	 * @param weights   Edge weights (NULL = unit weight)
	 * @param epochs    Number of iterations (default: 1000)
	 * @param theta     Speed/accuracy trade-off (default: 0.5)
	 */
	igraph_bool_t use_seed = layout_fill_seed(ctx, result, vcount);

	igraph_error_t code = igraph_layout_bhtsne(graph, result,
											   use_seed, // use_seed
											   NULL,	 // weights: NULL = unit weight
											   1000,	 // epochs: default value
											   0.5);	 // theta: default value

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}
	return result;
}

// ============================================================================
// T-SNE LAYOUT (3D)
// ============================================================================
void *compute_igraph_layout_bhtsne_3d(ExecutionContext *ctx)
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

	/*
	 * igraph_layout_tsne_3d() parameters:
	 * @param graph     The graph to layout
	 * @param res       Output matrix (will be resized to vcount x 3)
	 * @param use_seed  Whether to use initial positions (0 = random start)
	 * @param weights   Edge weights (NULL = unit weight)
	 * @param epochs    Number of iterations (default: 1000)
	 * @param theta     Speed/accuracy trade-off (default: 0.5)
	 */
	igraph_bool_t use_seed = layout_fill_seed(ctx, result, vcount);

	igraph_error_t code = igraph_layout_bhtsne_3d(graph, result,
												  use_seed, // use_seed
												  NULL,		// weights: NULL = unit weight
												  1000,		// epochs: default value
												  0.5);		// theta: default value

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}
	return result;
}
