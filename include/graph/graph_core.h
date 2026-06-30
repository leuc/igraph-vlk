/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GRAPH_CORE_H
#define GRAPH_CORE_H

#include "graph_types.h"

/* ============================================================================
 * Lifecycle Management
 * ============================================================================ */

/**
 * Free all graph data including igraph structures, nodes, edges, and layouts.
 * @param data Pointer to GraphData to free
 */
void graph_free_data(GraphData *data);

/**
 * Build the entire visualization arrays (nodes + edges) from the igraph graph.
 * Call after loading a new graph, generating a new graph, or filtering vertices.
 * Node colors are randomized, positions are synced from the layout matrix.
 * @param data Pointer to GraphData to build
 */
void graph_build_visualization(GraphData *data);

/**
 * Rebuild only the edge array and node degrees after in-place edge changes.
 * Does NOT touch node colors, labels, sizes, or positions.
 * @param data Pointer to GraphData whose edges changed
 */
void graph_rebuild_edges(GraphData *data);

/**
 * Try to populate current_layout from _pos vertex attributes.
 * Parses "x,y" or "x,y,z" strings into a 3-column layout matrix,
 * centers and autoscales.
 * @param data Pointer to GraphData to populate
 * @return true if _pos was found and layout populated, false otherwise
 */
bool graph_import_layout_pos(GraphData *data);

#endif // GRAPH_CORE_H
