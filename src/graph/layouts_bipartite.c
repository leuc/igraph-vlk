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
// BIPARTITE LAYOUT (3D)
// ============================================================================
void *compute_igraph_layout_bipartite(ExecutionContext *ctx)
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
	 * igraph_layout_bipartite() parameters:
	 * @param graph           The graph to layout
	 * @param types          Vertex type vector (true=top row, false=bottom row)
	 * @param res             Output matrix (will be resized to vcount x 2)
	 * @param hgap            Horizontal gap between vertices
	 * @param vgap            Vertical gap between rows
	 * @param maxiter        Maximum iterations (default: 100)
	 *
	 * We use alternating pattern as fallback when no type attribute available
	 */
	igraph_vector_bool_t types;
	if (igraph_vector_bool_init(&types, vcount) != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}

	for (igraph_integer_t i = 0; i < vcount; i++) {
		igraph_vector_bool_set(&types, i, (i % 2 == 0));
	}

	igraph_error_t code = igraph_layout_bipartite(graph, &types, result,
												  1.0, // hgap
												  1.0, // vgap
												  100  // maxiter
	);
	igraph_vector_bool_destroy(&types);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}

	if (result->ncol == 2) {
		igraph_matrix_resize(result, vcount, 3);
		for (igraph_integer_t i = 0; i < vcount; i++) {
			igraph_matrix_set(result, i, 2, 0.0);
		}
	}
	return result;
}

// ============================================================================
// BIPARTITE LAYOUT (2D)
// ============================================================================
void *compute_igraph_layout_bipartite_simple(ExecutionContext *ctx)
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

	igraph_vector_bool_t types;
	if (igraph_vector_bool_init(&types, vcount) != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}
	for (igraph_integer_t i = 0; i < vcount; i++) {
		igraph_vector_bool_set(&types, i, (i % 2 == 0));
	}

	igraph_error_t code = igraph_layout_bipartite(graph, &types, result, 1.0, 1.0, 100);
	igraph_vector_bool_destroy(&types);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}
	return result;
}