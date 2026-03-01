#ifndef GRAPH_WRAPPERS_H
#define GRAPH_WRAPPERS_H

#include "interaction/state.h"

// Igraph wrapper functions
void wrapper_shortest_path(ExecutionContext* ctx);
void wrapper_pagerank(ExecutionContext* ctx);
void wrapper_betweenness(ExecutionContext* ctx);
void wrapper_reset_layout(ExecutionContext* ctx);
void wrapper_cycle_colors(ExecutionContext* ctx);
void wrapper_community_arrangement(ExecutionContext* ctx);

#endif // GRAPH_WRAPPERS_H
