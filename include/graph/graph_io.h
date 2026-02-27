#ifndef GRAPH_IO_H
#define GRAPH_IO_H

#include "graph_types.h"

/**
 * Load a GraphML file into the graph data structure.
 * @param filename Path to the GraphML file
 * @param data Pointer to GraphData to populate
 * @param layout_type Initial layout type to use
 * @param node_attr Name of the node attribute to use for sizing (or NULL for default)
 * @param edge_attr Name of the edge attribute to use for sizing (or NULL for default)
 * @return 0 on success, -1 on failure
 */
int graph_load_graphml(const char *filename, GraphData *data,
					   LayoutType layout_type, const char *node_attr,
					   const char *edge_attr);

#endif // GRAPH_IO_H
