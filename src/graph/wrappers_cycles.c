#include "graph/wrappers_cycles.h"
#include "app_state.h"
#include "graph/graph_core.h"
#include "vulkan/renderer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// Worker: Remove feedback arc set from graph (make it acyclic)
// Modifies graph in-place, returns graph pointer on success, NULL on failure
// ============================================================================
void *compute_remove_feedback_arc_set(igraph_t *graph)
{
	if (!graph || igraph_vcount(graph) == 0)
		return NULL;

	igraph_bool_t is_dag;
	if (igraph_is_dag(graph, &is_dag) != IGRAPH_SUCCESS)
		return NULL;

	if (!is_dag) {
		igraph_vector_int_t fas;
		igraph_vector_int_init(&fas, 0);
		if (igraph_feedback_arc_set(graph, &fas, NULL, IGRAPH_FAS_APPROX_EADES) == IGRAPH_SUCCESS) {
			if (igraph_vector_int_size(&fas) > 0) {
				igraph_es_t es = igraph_ess_vector(&fas);
				igraph_delete_edges(graph, es);
				printf("Removed %d edges to make graph acyclic\n", (int)igraph_vector_int_size(&fas));
			}
		}
		igraph_vector_int_destroy(&fas);
	}

	return graph;
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

	graph_refresh_data(data);
	renderer_update_graph(&state->renderer, data);
	state->renderer.labelTreeNeedsRebuild = true;

	printf("[apply] Removed feedback arc set - %d vertices, %d edges\n", data->node_count, data->edge_count);
}

// ============================================================================
// Free: No-op — graph is owned by GraphData, not the result
// ============================================================================
void free_noop(void *result_data)
{
	(void)result_data;
}
