#include "graph/graph_actions.h"
#include "graph/graph_clustering.h"
#include "graph/graph_core.h"
#include "graph/graph_filter.h"
#include "graph/graph_io.h"
#include "vulkan/renderer.h"
#include <stdio.h>
#include <string.h>

void graph_action_run_clustering(AppState *state)
{
	graph_cluster(&state->current_graph, state->current_cluster);
	state->renderer.needsAttributeUpload = VK_TRUE;
	renderer_update_graph(&state->renderer, &state->current_graph);
}

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
	state->current_layout = LAYOUT_GRID_3D;
	state->renderer.layoutScale = 1.0f;
	state->current_graph.props.coreness_filter = 0;

	if (graph_load_graphml(state->current_filename, &state->current_graph, state->current_layout, state->node_attr, state->edge_attr) == 0) {
		renderer_update_graph(&state->renderer, &state->current_graph);
	}
}
