#ifndef GRAPH_ANALYSIS_H
#define GRAPH_ANALYSIS_H

#include "graph_types.h"

/**
 * Calculate centrality metrics for the graph.
 * @param data Pointer to GraphData
 * @param type The type of centrality to calculate
 */
void graph_calculate_centrality(GraphData *data, CentralityType type);

#endif // GRAPH_ANALYSIS_H
