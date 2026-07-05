/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#define _GNU_SOURCE

#include "app_state.h"
#include "graph/wrappers_layout.h"
#include <igraph.h>
#include <stdio.h>
#include <stdlib.h>

// ============================================================================
// DRL LAYOUT (2D)
// ============================================================================
void *compute_igraph_layout_drl(ExecutionContext *ctx)
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
	 * igraph_layout_drl() parameters:
	 * @param graph           The graph to layout
	 * @param res             Output matrix (will be resized to vcount x 2)
	 * @param use_seed        Whether to use initial positions
	 * @param options         DrL configuration struct (initialized with template)
	 *                       IGRAPH_LAYOUT_DRL_DEFAULT: good balance for general graphs
	 *                       Other options: COARSEN, COARSEST, REFINE, FINAL
	 * @param weights         Edge weights (NULL = unweighted)
	 *
	 * DrL uses a multi-phase approach: liquid -> expansion -> cooldown -> crunch -> simmer
	 * Excellent for medium to large graphs with natural clusters
	 */
	igraph_layout_drl_options_t options;
	igraph_layout_drl_options_init(&options, IGRAPH_LAYOUT_DRL_DEFAULT);

	igraph_error_t code = igraph_layout_drl(graph, result,
											0,		  // use_seed
											&options, // options: default template
											NULL	  // weights: NULL = unweighted
	);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}
	return result;
}

// ============================================================================
// DRL LAYOUT (3D)
// ============================================================================
void *compute_igraph_layout_drl_3d(ExecutionContext *ctx)
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
	 * igraph_layout_drl_3d() parameters: same as 2D but res is vcount x 3
	 */
	igraph_layout_drl_options_t options;
	igraph_layout_drl_options_init(&options, IGRAPH_LAYOUT_DRL_DEFAULT);

	igraph_error_t code = igraph_layout_drl_3d(graph, result,
											   0,		 // use_seed
											   &options, // options
											   NULL		 // weights
	);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}
	return result;
}