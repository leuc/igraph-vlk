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
// Worker: Check if graph is acyclic, fail if not
// ============================================================================
void *compute_remove_feedback_arc_set(igraph_t *graph)
{
	if (!graph || igraph_vcount(graph) == 0)
		return NULL;

	igraph_bool_t is_dag;
	if (igraph_is_dag(graph, &is_dag) != IGRAPH_SUCCESS)
		return NULL;

	if (!is_dag) {
		fprintf(stderr, "Graph has cycles, cannot make acyclic\n");
		return NULL;
	}

	printf("Graph is already acyclic\n");
	return NULL;
}

// ============================================================================
// Apply: Refresh visualization after in-place graph modification.
// Does NOT free/copy the graph or compute a new layout.
// ============================================================================
void apply_remove_feedback_arc_set(ExecutionContext *ctx, void *result_data)
{
	if (!ctx || !ctx->app_state || !ctx->current_graph || !result_data)
		return;
	(void)result_data;

	AppState *state = ctx->app_state;
	GraphData *data = &state->current_graph;

	graph_rebuild_edges(data);
	renderer_update_graph(&state->renderer, data);
	state->renderer.label.tree_needs_rebuild = true;

	printf("[apply] Removed feedback arc set - %d vertices, %d edges\n", data->node_count, data->edge_count);
}

// ============================================================================
// Free: No-op — graph is owned by GraphData, not the result
// ============================================================================
void free_noop(void *result_data)
{
	(void)result_data;
}
