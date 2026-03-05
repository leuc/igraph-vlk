#ifndef GRAPH_WRAPPERS_COMMUNITY_H
#define GRAPH_WRAPPERS_COMMUNITY_H

#include "interaction/state.h"
#include <igraph.h>

// Pure worker functions - no UI or state dependencies
// All return igraph_vector_int_t* (membership vector) on success, NULL on failure

// Multilevel (Louvain)
void *compute_com_multilevel(igraph_t *graph);

// Leiden
void *compute_com_leiden(igraph_t *graph);

// Walktrap
void *compute_com_walktrap(igraph_t *graph);

// Edge Betweenness (Girvan-Newman)
void *compute_com_edge_betweenness(igraph_t *graph);

// Fast Greedy
void *compute_com_fastgreedy(igraph_t *graph);

// Infomap
void *compute_com_infomap(igraph_t *graph);

// Label Propagation
void *compute_com_label_prop(igraph_t *graph);

// Spinglass
void *compute_com_spinglass(igraph_t *graph);

// Leading Eigenvector
void *compute_com_leading_eigenvector(igraph_t *graph);

// Optimal Modularity
void *compute_com_optimal_modularity(igraph_t *graph);

// Voronoi
void *compute_com_voronoi(igraph_t *graph);

// Fluid Communities
void *compute_com_fluid(igraph_t *graph);

// Standard apply and free functions for community membership
void apply_community_membership(ExecutionContext *ctx, void *result_data);
void free_community_membership(void *result_data);

#endif // GRAPH_WRAPPERS_COMMUNITY_H
