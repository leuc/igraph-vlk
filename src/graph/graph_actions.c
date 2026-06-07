#include "graph/graph_actions.h"
#include "graph/graph_core.h"
#include "graph/graph_filter.h"
#include "graph/graph_io.h"
#include "vulkan/renderer.h"
#include "vulkan/renderer_compute.h"
#include <stdio.h>
#include <string.h>

void graph_action_filter_degree(AppState *state, int min_deg)
{
	graph_filter_degree(&state->current_graph, min_deg);
	state->renderer.needsAttributeUpload = VK_TRUE;
	renderer_update_graph(&state->renderer, &state->current_graph);
}

void graph_action_filter_coreness(AppState *state, int min_core)
{
	graph_filter_coreness(&state->current_graph, min_core);
	state->renderer.needsAttributeUpload = VK_TRUE;
	renderer_update_graph(&state->renderer, &state->current_graph);
}

void graph_action_highlight_infrastructure(AppState *state)
{
	graph_highlight_infrastructure(&state->current_graph);
	state->renderer.needsAttributeUpload = VK_TRUE;
	renderer_update_graph(&state->renderer, &state->current_graph);
}

void graph_action_reset(AppState *state)
{
	graph_free_data(&state->current_graph);
	state->renderer.layoutScale = 1.0f;
	state->current_graph.props.coreness_filter = 0;

	if (graph_load_graphml(state->current_filename, &state->current_graph, LAYOUT_GRID_3D, NULL, NULL)) {
		renderer_update_graph(&state->renderer, &state->current_graph);
		state->renderer.labelTreeNeedsRebuild = true;
	}
}

void graph_action_start_splc(AppState *state)
{
	if (state->renderer.splc_active)
		return;

	igraph_t *g = &state->current_graph.g;

	if (!igraph_is_directed(g)) {
		fprintf(stderr, "SPLC requires a directed graph\n");
		return;
	}

	igraph_bool_t is_dag = false;
	igraph_is_dag(g, &is_dag);

	if (!is_dag) {
		printf("Graph has cycles; removing feedback arc set...\n");
		igraph_vector_int_t fas;
		igraph_vector_int_init(&fas, 0);
		if (igraph_feedback_arc_set(g, &fas, NULL, IGRAPH_FAS_APPROX_EADES) == IGRAPH_SUCCESS) {
			if (igraph_vector_int_size(&fas) > 0) {
				igraph_es_t es = igraph_ess_vector(&fas);
				igraph_delete_edges(g, es);
				printf("Removed %d edges to make graph acyclic\n", (int)igraph_vector_int_size(&fas));
			}
		}
		igraph_vector_int_destroy(&fas);
	}

	// Refresh graph data and upload to GPU (includes SPLC buffer init)
	state->renderer.needsAttributeUpload = VK_TRUE;
	graph_refresh_data(&state->current_graph);
	renderer_update_graph(&state->renderer, &state->current_graph);

	// Reset SPLC state so animation starts from level 0
	renderer_reset_splc(&state->renderer);
	printf("SPLC animation started (graph has %d levels)\n", state->renderer.splc_num_levels);
}
