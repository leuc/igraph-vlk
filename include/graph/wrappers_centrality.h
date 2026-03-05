#ifndef GRAPH_WRAPPERS_CENTRALITY_H
#define GRAPH_WRAPPERS_CENTRALITY_H

#include "interaction/state.h"
#include <igraph.h>

// Pure worker functions - no UI or state dependencies
// All return igraph_vector_t* (centrality scores) on success, NULL on failure

// Degree centrality
void *compute_cent_degree(igraph_t *graph);

// Closeness centrality
void *compute_cent_closeness(igraph_t *graph);

// Betweenness centrality
void *compute_cent_betweenness(igraph_t *graph);

// Eigenvector centrality
void *compute_cent_eigenvector(igraph_t *graph);

// PageRank
void *compute_cent_pagerank(igraph_t *graph);

// HITS (Hub & Authority)
void *compute_cent_hits(igraph_t *graph);

// Harmonic centrality
void *compute_cent_harmonic(igraph_t *graph);

// Strength (weighted degree)
void *compute_cent_strength(igraph_t *graph);

// Constraint (structural hole)
void *compute_cent_constraint(igraph_t *graph);

// Standard apply and free functions for centrality scores
void apply_centrality_scores(ExecutionContext *ctx, void *result_data);
void free_centrality_scores(void *result_data);

#endif // GRAPH_WRAPPERS_CENTRALITY_H
