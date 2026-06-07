#ifndef GRAPH_WRAPPERS_CYCLES_H
#define GRAPH_WRAPPERS_CYCLES_H

#include "interaction/state.h"
#include <igraph.h>

// Cycle analysis: remove feedback arc set to make graph acyclic
void *compute_remove_feedback_arc_set(igraph_t *graph);
void apply_remove_feedback_arc_set(ExecutionContext *ctx, void *result_data);
void free_noop(void *result_data);

#endif
