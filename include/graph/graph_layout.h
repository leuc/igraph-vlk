#pragma once

#include "graph_types.h"

/**
 * Step the layout algorithm for the given number of iterations.
 * 
 * @param data The graph data structure
 * @param type The layout type to use
 * @param iterations Number of iterations to run
 */
void graph_layout_step(GraphData *data, LayoutType type, int iterations);

/**
 * Remove overlaps between nodes in the current layout.
 * 
 * @param data The graph data structure
 * @param layoutScale Scale factor for the layout
 */
void graph_remove_overlaps(GraphData *data, float layoutScale);
