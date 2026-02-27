#ifndef GRAPH_CORE_H
#define GRAPH_CORE_H

#include "graph_types.h"

/* ============================================================================
 * Lifecycle Management
 * ============================================================================ */

/**
 * Initialize graph data structure with default values.
 * @param data Pointer to GraphData to initialize
 */
void graph_init(GraphData *data);

/**
 * Free all graph data including igraph structures, nodes, edges, and layouts.
 * @param data Pointer to GraphData to free
 */
void graph_free_data(GraphData *data);

/**
 * Refresh graph data structures from igraph graph.
 * This recalculates node/edge arrays from the current igraph graph state.
 * Call this after any operation that modifies the graph topology.
 * @param data Pointer to GraphData to refresh
 */
void graph_refresh_data(GraphData *data);

/**
 * Synchronize node positions from the igraph layout matrix to the Node array.
 * @param data Pointer to GraphData containing the layout and nodes
 */
void graph_sync_node_positions(GraphData *data);

#endif // GRAPH_CORE_H
