/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GRAPH_IO_H
#define GRAPH_IO_H

#include "graph_types.h"
#include <igraph.h>

/**
 * Auto-detect file format by extension (.graphml or .gml) and load.
 * @param filename Path to the graph file
 * @param data Pointer to GraphData to populate
 * @param node_attr Name of the node attribute to use for sizing (or NULL for default)
 * @param edge_attr Name of the edge attribute to use for sizing (or NULL for default)
 * @return true on success, false on failure
 */
bool graph_load(const char *filename, GraphData *data, const char *node_attr, const char *edge_attr);

/**
 * Load a GraphML file.
 */
bool graph_load_graphml(const char *filename, GraphData *data, const char *node_attr, const char *edge_attr);

/**
 * Load a GML file.
 */
bool graph_load_gml(const char *filename, GraphData *data, const char *node_attr, const char *edge_attr);

/**
 * Read GML from an open FILE stream into a raw igraph_t.
 * All igraph_read_graph_gml calls must go through this function.
 * @return true on success, false on failure
 */
bool graph_read_gml(igraph_t *graph, FILE *fp);

#endif // GRAPH_IO_H
