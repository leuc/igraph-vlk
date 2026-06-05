#pragma once

#include "graph_types.h"

/**
 * Perform community detection on the graph.
 *
 * @param data The graph data structure
 * @param type The clustering algorithm to use
 */
void graph_cluster(GraphData *data, ClusterType type);
