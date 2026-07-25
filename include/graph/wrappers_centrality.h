/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GRAPH_WRAPPERS_CENTRALITY_H
#define GRAPH_WRAPPERS_CENTRALITY_H

#include "interaction/state.h"
#include <igraph.h>

// Pure worker functions - no UI or state dependencies
// All return igraph_vector_t* (centrality scores) on success, NULL on failure

// Degree centrality
void *compute_igraph_degree(ExecutionContext *ctx);

// Closeness centrality
void *compute_igraph_closeness_cutoff(ExecutionContext *ctx);

// Betweenness centrality
void *compute_igraph_betweenness(ExecutionContext *ctx);

// Eigenvector centrality
void *compute_igraph_eigenvector_centrality(ExecutionContext *ctx);

// PageRank
void *compute_igraph_pagerank(ExecutionContext *ctx);

// HITS (Hub & Authority)
void *compute_igraph_hub_and_authority_scores(ExecutionContext *ctx);

// Harmonic centrality
void *compute_igraph_harmonic_centrality(ExecutionContext *ctx);

// Coreness (k-core)
void *compute_igraph_coreness(ExecutionContext *ctx);

// Strength (weighted degree)
void *compute_igraph_strength(ExecutionContext *ctx);

// Constraint (structural hole)
void *compute_igraph_constraint(ExecutionContext *ctx);

// CD index (citation disruption) — requires a directed graph, no self-loops,
// and a 'date' vertex attribute (ISO 8601 string, e.g. "1999-07-05")
void *compute_igraph_cd_index(ExecutionContext *ctx);

// Standard apply and free functions for centrality scores
void apply_centrality_scores(ExecutionContext *ctx, void *result_data);
void centrality_scores_free(void *result_data);

#endif // GRAPH_WRAPPERS_CENTRALITY_H
