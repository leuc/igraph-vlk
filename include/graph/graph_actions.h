#pragma once

#include "app_state.h"
#include <stdbool.h>

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
