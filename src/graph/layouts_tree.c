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
// REINGOLD-TILFORD TREE LAYOUT
// ============================================================================
// Reference: Reingold, E. and Tilford, J.:
// "Tidier drawing of trees."
// IEEE Trans. Softw. Eng., SE-7(2):223-228, 1981.
// https://doi.org/10.1109/TSE.1981.234519
void *compute_igraph_layout_reingold_tilford(ExecutionContext *ctx)
{
	igraph_t *graph = &ctx->app_state->current_graph.g;
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = IGRAPH_MALLOC(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 3) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		return NULL;
	}

	/*
	 * igraph_layout_reingold_tilford() parameters:
	 * @param graph           The graph to layout (will find spanning tree if not tree)
	 * @param res             Output matrix (will be resized to vcount x 2)
	 * @param mode            Edge traversal mode:
	 *                       IGRAPH_OUT: outgoing edges (out-tree)
	 *                       IGRAPH_IN: incoming edges (in-tree)
	 *                       IGRAPH_ALL: all edges (treat as undirected)
	 * @param roots          Root vertices (NULL = auto-select using heuristic)
	 * @param rootlevel      Level offsets for multiple roots (NULL = all level 0)
	 *
	 * Uses BFS to determine levels, centers parents above children
	 * igraph_roots_for_tree_layout() auto-selects roots based on degree
	 */
	igraph_neimode_t mode = IGRAPH_ALL;

	igraph_vector_int_t roots;
	if (igraph_vector_int_init(&roots, 0) != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}
	igraph_roots_for_tree_layout(graph, mode, &roots, IGRAPH_ROOT_CHOICE_DEGREE);

	igraph_error_t code = igraph_layout_reingold_tilford(graph, result,
														 mode,						 // mode: treat as undirected
														 vcount > 0 ? &roots : NULL, // roots: auto-selected
														 NULL						 // rootlevel: all start at 0
	);

	igraph_vector_int_destroy(&roots);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}

	if (result->ncol < 3) {
		igraph_matrix_resize(result, vcount, 3);
		for (igraph_integer_t i = 0; i < vcount; i++) {
			igraph_matrix_set(result, i, 2, 0.0);
		}
	}
	return result;
}