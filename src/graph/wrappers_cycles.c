/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "graph/wrappers_cycles.h"
#include "app_state.h"
#include "graph/graph_core.h"
#include "vulkan/renderer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// Worker: Compute and remove feedback arc set to make graph acyclic.
// Modifies graph in-place (same pattern as compute_splc_animation).
// Returns graph pointer on success, NULL on failure.
// ============================================================================
void *compute_remove_feedback_arc_set(ExecutionContext *ctx)
{
	igraph_t *graph = &ctx->app_state->current_graph.g;
	if (!graph || igraph_vcount(graph) == 0)
		return NULL;

	if (!igraph_is_directed(graph)) {
		fprintf(stderr, "Feedback arc set requires a directed graph\n");
		return NULL;
	}

	igraph_bool_t is_dag = false;
	igraph_is_dag(graph, &is_dag);

	if (!is_dag) {
		igraph_vector_int_t fas;
		igraph_vector_int_init(&fas, 0);
		igraph_error_t ret = igraph_feedback_arc_set(graph, &fas, NULL, IGRAPH_FAS_APPROX_EADES);
		if (ret == IGRAPH_SUCCESS) {
			igraph_integer_t n_fas = igraph_vector_int_size(&fas);
			if (n_fas > 0) {
				igraph_es_t es = igraph_ess_vector(&fas);
				igraph_delete_edges(graph, es);
				printf("Removed %d edges to make graph acyclic\n", (int)n_fas);
			} else {
				printf("Graph has cycles but feedback arc set is empty\n");
			}
		} else {
			igraph_vector_int_destroy(&fas);
			fprintf(stderr, "igraph_feedback_arc_set failed\n");
			return NULL;
		}
		igraph_vector_int_destroy(&fas);
	} else {
		printf("Graph is already acyclic\n");
	}

	return graph;
}

// ============================================================================
// Apply: Refresh visualization after in-place graph modification.
// Does NOT free/copy the graph or compute a new layout.
// ============================================================================
void apply_remove_feedback_arc_set(ExecutionContext *ctx, void *result_data)
{
	if (!ctx || !ctx->app_state)
		return;
	(void)result_data;

	AppState *state = ctx->app_state;
	GraphData *data = &state->current_graph;

	graph_rebuild_edges(data);
	renderer_update_graph(&state->renderer, data);
	state->renderer.label.tree_needs_rebuild = true;

	printf("[apply] Feedback arc set processed - %d vertices, %d edges\n", data->node_count, data->edge_count);
}

// ============================================================================
// Free: No-op — graph is owned by GraphData, not the result
// ============================================================================
void free_noop(void *result_data)
{
	(void)result_data;
}
