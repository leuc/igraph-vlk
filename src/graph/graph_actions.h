#pragma once

#include <stdbool.h>
#include "app_state.h"

/**
 * Run one iteration of the current layout algorithm.
 * @param state Pointer to the application state
 */
void graph_action_update_layout(AppState *state);

/**
 * Run clustering on the current graph.
 * @param state Pointer to the application state
 */
void graph_action_run_clustering(AppState *state);

/**
 * Run centrality calculation on the current graph.
 * @param state Pointer to the application state
 */
void graph_action_run_centrality(AppState *state);

/**
 * Run a single iteration step of the layout.
 * @param state Pointer to the application state
 */
void graph_action_run_iteration(AppState *state);

/**
 * Filter nodes by degree.
 * @param state Pointer to the application state
 * @param min_deg Minimum degree threshold
 */
void graph_action_filter_degree(AppState *state, int min_deg);

/**
 * Filter nodes by coreness (k-core).
 * @param state Pointer to the application state
 * @param min_core Minimum k-core value
 */
void graph_action_filter_coreness(AppState *state, int min_core);

/**
 * Highlight infrastructure nodes in the graph.
 * @param state Pointer to the application state
 */
void graph_action_highlight_infrastructure(AppState *state);

/**
 * Reset the graph to its original state.
 * @param state Pointer to the application state
 */
void graph_action_reset(AppState *state);

/**
 * Step the background layout (OpenOrd / Layered Sphere).
 * Called each frame for incremental layout computation.
 * @param state Pointer to the application state
 * @return true if layout was updated, false otherwise
 */
bool graph_action_step_background_layout(AppState *state);

/**
 * Cycle to the next layout type.
 * @param state Pointer to the application state
 */
void graph_action_cycle_layout(AppState *state);

/**
 * Cycle to the next clustering algorithm.
 * @param state Pointer to the application state
 */
void graph_action_cycle_cluster(AppState *state);

/**
 * Cycle to the next centrality metric.
 * @param state Pointer to the application state
 */
void graph_action_cycle_centrality(AppState *state);

/**
 * Cycle to the next community arrangement mode.
 * @param state Pointer to the application state
 */
void graph_action_cycle_community_arrangement(AppState *state);

