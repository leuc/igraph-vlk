#ifndef GRAPH_WRAPPERS_COMMUNITY_H
#define GRAPH_WRAPPERS_COMMUNITY_H

#include "interaction/state.h"
#include <igraph.h>

// Pure worker functions - no UI or state dependencies
// All return igraph_vector_int_t* (membership vector) on success, NULL on failure

// Multilevel (Louvain)
void *compute_igraph_community_multilevel(igraph_t *graph);

// Leiden
void *compute_igraph_community_leiden(igraph_t *graph);

// Walktrap
void *compute_igraph_community_walktrap(igraph_t *graph);

// Edge Betweenness (Girvan-Newman)
void *compute_igraph_community_edge_betweenness(igraph_t *graph);

// Fast Greedy
void *compute_igraph_community_fastgreedy(igraph_t *graph);

// Infomap
void *compute_igraph_community_infomap(igraph_t *graph);

// Label Propagation
void *compute_igraph_community_label_propagation(igraph_t *graph);

// Spinglass
void *compute_igraph_community_spinglass(igraph_t *graph);

// Leading Eigenvector
void *compute_igraph_community_leading_eigenvector(igraph_t *graph);

// Optimal Modularity
void *compute_igraph_community_optimal_modularity(igraph_t *graph);

// Voronoi
void *compute_igraph_community_voronoi(igraph_t *graph);

// Fluid Communities
void *compute_igraph_community_fluid_communities(igraph_t *graph);

// Standard apply and free functions for community membership
void apply_community_membership(ExecutionContext *ctx, void *result_data);
void free_community_membership(void *result_data);

#endif // GRAPH_WRAPPERS_COMMUNITY_H
