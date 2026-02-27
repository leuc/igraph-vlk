#pragma once

#include "graph_types.h"

/**
 * Perform community detection on the graph.
 * 
 * @param data The graph data structure
 * @param type The clustering algorithm to use
 */
void graph_cluster(GraphData *data, ClusterType type);

/**
 * Apply visual arrangement to communities.
 * This arranges nodes within each community using various layouts.
 * 
 * @param data The graph data structure
 * @param mode The arrangement mode to use
 */
void graph_apply_community_arrangement(GraphData *data, CommunityArrangementMode mode);
