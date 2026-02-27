#ifndef GRAPH_FILTER_H
#define GRAPH_FILTER_H

#include "graph_types.h"

/**
 * Filter nodes by degree - removes nodes with degree less than min_degree.
 * @param data Pointer to GraphData
 * @param min_degree Minimum degree threshold
 */
void graph_filter_degree(GraphData *data, int min_degree);

/**
 * Filter nodes by coreness (k-core decomposition).
 * @param data Pointer to GraphData
 * @param min_coreness Minimum coreness threshold
 */
void graph_filter_coreness(GraphData *data, int min_coreness);

/**
 * Highlight infrastructure nodes (articulation points and bridges).
 * @param data Pointer to GraphData
 */
void graph_highlight_infrastructure(GraphData *data);

/**
 * Generate hub positions using k-means clustering on edge midpoints.
 * @param data Pointer to GraphData
 * @param num_hubs Number of hubs to generate
 */
void graph_generate_hubs(GraphData *data, int num_hubs);

#endif // GRAPH_FILTER_H
