#include "graph_actions.h"
#include "graph/graph_core.h"
#include "graph/graph_layout.h"
#include "graph/graph_clustering.h"
#include "graph/graph_analysis.h"
#include "graph/graph_filter.h"
#include "graph/graph_io.h"
#include "graph/layout_openord.h"
#include "vulkan/renderer.h"
#include "vulkan/animation_manager.h"
#include <stdio.h>
#include <string.h>

void graph_action_update_layout(AppState *state) {
    graph_layout_step(&state->current_graph, state->current_layout, 50);
    renderer_update_graph(&state->renderer, &state->current_graph);
}

void graph_action_run_clustering(AppState *state) {
    graph_cluster(&state->current_graph, state->current_cluster);
    renderer_update_graph(&state->renderer, &state->current_graph);
}

void graph_action_run_centrality(AppState *state) {
    graph_calculate_centrality(&state->current_graph, state->current_centrality);
    renderer_update_graph(&state->renderer, &state->current_graph);
}

void graph_action_run_iteration(AppState *state) {
    graph_layout_step(&state->current_graph, state->current_layout, 1);
    renderer_update_graph(&state->renderer, &state->current_graph);
}

void graph_action_filter_degree(AppState *state, int min_deg) {
    graph_filter_degree(&state->current_graph, min_deg);
    renderer_update_graph(&state->renderer, &state->current_graph);
}

void graph_action_filter_coreness(AppState *state, int min_core) {
    graph_filter_coreness(&state->current_graph, min_core);
    renderer_update_graph(&state->renderer, &state->current_graph);
}

void graph_action_highlight_infrastructure(AppState *state) {
    graph_highlight_infrastructure(&state->current_graph);
    renderer_update_graph(&state->renderer, &state->current_graph);
}

void graph_action_reset(AppState *state) {
    graph_free_data(&state->current_graph);
    state->current_layout = LAYOUT_OPENORD_3D;
    state->renderer.layoutScale = 1.0f;
    state->current_graph.props.coreness_filter = 0;

    if (graph_load_graphml(state->current_filename, &state->current_graph,
                           state->current_layout,
                           state->node_attr, state->edge_attr) == 0) {
        renderer_update_graph(&state->renderer, &state->current_graph);
    }
}

bool graph_action_step_background_layout(AppState *state) {
    if (state->current_layout == LAYOUT_OPENORD_3D &&
        state->current_graph.openord &&
        state->current_graph.openord->stage_id < 5) {
        graph_layout_step(&state->current_graph, state->current_layout, 1);
        renderer_update_graph(&state->renderer, &state->current_graph);
        return true;
    }
    else if (state->current_layout == LAYOUT_LAYERED_SPHERE &&
             state->current_graph.layered_sphere) {
        graph_layout_step(&state->current_graph, state->current_layout, 1);
        renderer_update_graph(&state->renderer, &state->current_graph);
        return true;
    }
    return false;
}

void graph_action_cycle_layout(AppState *state) {
    state->current_layout = (state->current_layout + 1) % LAYOUT_COUNT;
    graph_action_update_layout(state);
}

void graph_action_cycle_cluster(AppState *state) {
    state->current_cluster = (state->current_cluster + 1) % CLUSTER_COUNT;
    graph_action_run_clustering(state);
}

void graph_action_cycle_centrality(AppState *state) {
    state->current_centrality = (state->current_centrality + 1) % CENTRALITY_COUNT;
    graph_action_run_centrality(state);
}

void graph_action_cycle_community_arrangement(AppState *state) {
    state->current_comm_arrangement = (state->current_comm_arrangement + 1) %
                                       COMMUNITY_ARRANGEMENT_COUNT;
    if (state->current_comm_arrangement == COMMUNITY_ARRANGEMENT_NONE) {
        graph_action_update_layout(state);
    } else {
        graph_apply_community_arrangement(&state->current_graph,
                                          state->current_comm_arrangement);
        renderer_update_graph(&state->renderer, &state->current_graph);
    }
}

