/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GRAPH_FILTER_VISIBILITY_H
#define GRAPH_FILTER_VISIBILITY_H

#include "graph/graph_types.h"

/**
 * Reset all nodes to visible (visible = 1.0f).
 * @param data Pointer to GraphData
 */
void graph_filter_reset_visibility(GraphData *data);

/**
 * Hide nodes that do NOT match the given attribute value.
 * Nodes matching attr_name = attr_value get visible = 1.0f, others get 0.0f.
 * Only string and boolean attributes are supported (as determined by graph_analyze_filterable_attrs).
 * @param data Pointer to GraphData
 * @param attr_name Name of the vertex attribute to filter by
 * @param attr_value Value to match (string comparison; for booleans use "true"/"false")
 */
void graph_filter_by_attribute(GraphData *data, const char *attr_name, const char *attr_value);

/**
 * Reset all edges to visible (visible = 1.0f).
 * @param data Pointer to GraphData
 */
void graph_filter_reset_edge_visibility(GraphData *data);

/**
 * Hide edges that do NOT match the given attribute value.
 * Edges matching attr_name = attr_value get visible = 1.0f, others get 0.0f.
 * Only string and boolean attributes are supported (as determined by graph_detect_filterable_edge_attrs).
 * @param data Pointer to GraphData
 * @param attr_name Name of the edge attribute to filter by
 * @param attr_value Value to match (string comparison; for booleans use "true"/"false")
 */
void graph_filter_by_edge_attribute(GraphData *data, const char *attr_name, const char *attr_value);

#endif // GRAPH_FILTER_VISIBILITY_H
