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
bool graph_build_visualization(GraphData *data);

/**
 * (Re)scan vertex attributes for low-cardinality string/boolean ones and
 * populate data->filterable_attrs. Called by graph_build_visualization; call
 * directly after an in-place op adds/changes vertex attributes without a
 * full rebuild (e.g. CD index writing 'cd-index-type').
 * @param data Pointer to GraphData whose filterable_attrs should be refreshed
 */
void graph_detect_filterable_attrs(GraphData *data);

/**
 * (Re)scan edge attributes for low-cardinality string/boolean ones and
 * populate data->filterable_edge_attrs. Called by graph_build_visualization;
 * call directly after an in-place op adds/changes edge attributes without a
 * full rebuild.
 * @param data Pointer to GraphData whose filterable_edge_attrs should be refreshed
 */
void graph_detect_filterable_edge_attrs(GraphData *data);

/**
 * Rebuild only the edge array and node degrees after in-place edge changes.
 * Does NOT touch node colors, labels, sizes, or positions.
 * @param data Pointer to GraphData whose edges changed
 */
bool graph_rebuild_edges(GraphData *data);

/**
 * Try to populate current_layout from _pos vertex attributes.
 * Parses "x,y" or "x,y,z" strings into a 3-column layout matrix,
 * centers and autoscales.
 * @param data Pointer to GraphData to populate
 * @return true if _pos was found and layout populated, false otherwise
 */
bool graph_import_layout_pos(GraphData *data);

/**
 * Build an edge-id-ordered weight vector from the igraph "weight" edge attribute.
 * @param graph Graph to read the attribute from
 * @param out_weights Initialized on success (caller must igraph_vector_destroy); untouched on failure
 * @return true if the graph has a "weight" edge attribute and out_weights was populated, false otherwise
 */
bool graph_build_edge_weights(const igraph_t *graph, igraph_vector_t *out_weights);

#endif // GRAPH_CORE_H
