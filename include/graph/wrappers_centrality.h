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
void *compute_igraph_authority_scores(ExecutionContext *ctx);

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

// Edge betweenness centrality
void *compute_igraph_edge_betweenness(ExecutionContext *ctx);

// Convergence degree — signed per-edge value in (-1, 1); positive = convergent,
// negative = divergent. Persists 'convergence' (string) and 'convergence-degree'
// (numeric) edge attributes as a side effect.
void *compute_igraph_convergence_degree(ExecutionContext *ctx);

// Edge trussness — highest k-truss each edge belongs to. Fails (returns NULL,
// logs igraph's error) on graphs with multi-edges or mutual directed edge pairs.
void *compute_igraph_trussness(ExecutionContext *ctx);

// Standard apply and free functions for centrality scores
void apply_centrality_scores(ExecutionContext *ctx, void *result_data);
void centrality_scores_free(void *result_data);

// CD index apply: standard sizing, plus refreshes Filter > Node for the new
// 'cd-index-type' attribute
void apply_cd_index(ExecutionContext *ctx, void *result_data);

// Standard apply for edge-based scores (min/max-normalizes onto Edge.weight,
// which drives edge alpha in the renderer — the edge-side twin of apply_centrality_scores)
void apply_edge_centrality_scores(ExecutionContext *ctx, void *result_data);

// Convergence degree apply: standard edge sizing, plus refreshes Filter > Edge
// for the new 'convergence' attribute
void apply_convergence_degree(ExecutionContext *ctx, void *result_data);

// Removes every attribute a Rank command may have cached (see the name table
// in wrappers_centrality.c), so the next Rank invocation recomputes instead of
// reapplying values that are now stale. Call after any in-place graph edit.
void centrality_clear_cached_attrs(igraph_t *graph);

#endif // GRAPH_WRAPPERS_CENTRALITY_H
