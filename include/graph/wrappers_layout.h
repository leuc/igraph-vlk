#ifndef GRAPH_WRAPPERS_LAYOUT_H
#define GRAPH_WRAPPERS_LAYOUT_H

#include <igraph.h>
#include "interaction/state.h"

// Pure worker functions - no UI or state dependencies
void* compute_lay_force_fr(igraph_t* graph);
void free_layout_matrix(void* result_data);

// Apply function - bridge to update graph state (can include UI/state headers in .c file)
void apply_layout_matrix(ExecutionContext* ctx, void* result_data);

#endif // GRAPH_WRAPPERS_LAYOUT_H
