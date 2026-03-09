#ifndef GRAPH_WRAPPERS_STRUCTURAL_H
#define GRAPH_WRAPPERS_STRUCTURAL_H

#include "interaction/state.h"
#include <igraph.h>

// Global network properties: density, transitivity, assortativity

void *compute_igraph_density(igraph_t *graph);
void *compute_igraph_transitivity_undirected(igraph_t *graph);
void *compute_igraph_assortativity_degree(igraph_t *graph);

#endif
