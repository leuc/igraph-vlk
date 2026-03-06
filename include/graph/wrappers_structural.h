#ifndef GRAPH_WRAPPERS_STRUCTURAL_H
#define GRAPH_WRAPPERS_STRUCTURAL_H

#include "interaction/state.h"
#include <igraph.h>

// Global network properties: density, transitivity, assortativity

void *compute_ana_glob_dens(igraph_t *graph);
void *compute_ana_glob_trans(igraph_t *graph);
void *compute_ana_glob_assort(igraph_t *graph);

#endif
