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

// ============================================================================
// GEM LAYOUT
// ============================================================================
// Reference: Frick, A., Ludwig, A., and Mehldau, H. (1994):
// "A Fast Adaptive Layout Algorithm for Undirected Graphs."
void *compute_igraph_layout_gem(ExecutionContext *ctx)
{
	igraph_t *graph = &ctx->app_state->current_graph.g;
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = IGRAPH_MALLOC(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 2) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		return NULL;
	}

	igraph_bool_t use_seed = layout_fill_seed(ctx, result, vcount);

	/*
	 * igraph_layout_gem() parameters:
	 * @param graph           The graph to layout (edge directions ignored)
	 * @param res             Output matrix (will be resized to vcount x 2)
	 * @param use_seed        Whether to use initial positions
	 * @param maxiter         Maximum iterations (default: 40*vcount*vcount)
	 *                       Paper suggests 4*vcount*vcount, but 40*vcount*vcount more reliable
	 * @param temp_max        Maximum temperature (default: vcount)
	 * @param temp_min        Termination temperature (default: 0.1)
	 * @param temp_init       Initial temperature (default: sqrt(vcount))
	 *
	 * Fast adaptive layout using simulated annealing
	 * O(t * n * (n+e)) time complexity
	 */
	igraph_int_t maxiter = 40 * vcount * vcount;
	igraph_real_t temp_max = (igraph_real_t)vcount;
	igraph_real_t temp_min = 0.1;
	igraph_real_t temp_init = sqrt((igraph_real_t)vcount);

	igraph_error_t code = igraph_layout_gem(graph, result,
											use_seed, // use_seed
											maxiter,  // maxiter: 40*vcount^2
											temp_max, // temp_max: vcount
											temp_min, // temp_min: 0.1
											temp_init // temp_init: sqrt(vcount)
	);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}
	return result;
}
