#include "ui/hud.h"
#include "vulkan/renderer_ui.h"
#include "graph/layered_sphere.h"
#include "graph/layout_openord.h"
#include <stdio.h>
#include <string.h>

/**
 * Layout type names for UI display.
 */
static const char *layout_names[] = {
    "Fruchterman-Reingold",
    "Kamada-Kawai",
    "Random",
    "Sphere",
    "Grid",
    "UMAP",
    "DrL",
    "OpenOrd",
    "Layered Sphere"
};

/**
 * Cluster algorithm names for UI display.
 */
static const char *cluster_names[] = {
    "Fast Greedy", "Walktrap", "Label Propagation",
    "Multilevel", "Leiden"
};

/**
 * Centrality metric names for UI display.
 */
static const char *centrality_names[] = {
    "PageRank",    "Hubs",      "Authorities", "Betweenness", "Degree",
    "Closeness",   "Harmonic",  "Eigenvector", "Strength",    "Constraint"
};

/**
 * Community arrangement mode names for UI display.
 */
static const char *comm_arrangement_names[] = {
    "None",
    "Kececi 2D",
    "Kececi Tetra 3D",
    "Compact Ortho 2D",
    "Compact Ortho 3D"
};

void ui_hud_init(void) {
    // No initialization needed currently
}

void ui_hud_update(AppState *state, float fps) {
    char stage_info[64] = "";

    // Get stage info for OpenOrd layout
    if (state->current_layout == LAYOUT_OPENORD_3D && state->current_graph.openord) {
        snprintf(stage_info, sizeof(stage_info), " [%s:%d]",
                 openord_get_stage_name(state->current_graph.openord->stage_id),
                 state->current_graph.openord->current_iter);
    }
    // Get stage info for Layered Sphere layout
    else if (state->current_layout == LAYOUT_LAYERED_SPHERE &&
             state->current_graph.layered_sphere) {
        snprintf(stage_info, sizeof(stage_info), " [%s:%d]",
                 layered_sphere_get_stage_name(state->current_graph.layered_sphere),
                 state->current_graph.layered_sphere->current_iter);
    }

    char buf[1024];
    snprintf(buf, sizeof(buf),
             "[L]ayout:%s%s [Y]SubGraph:%s [I]terate [C]ommunity:%s Str[u]cture:%s "
             "[T]ext:%s [N]ode:%d [E]dge:%d Filter:1-9 [K]Core:%d "
             "[R]eset [H]ide FPS:%.1f",
             layout_names[state->current_layout], stage_info,
             comm_arrangement_names[state->current_comm_arrangement],
             cluster_names[state->current_cluster],
             centrality_names[state->current_centrality],
             state->renderer.showLabels ? "ON" : "OFF",
             state->current_graph.props.node_count,
             state->current_graph.props.edge_count,
             state->current_graph.props.coreness_filter,
             fps);

    renderer_update_ui(&state->renderer, buf);
}

