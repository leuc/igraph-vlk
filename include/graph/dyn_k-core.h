/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GRAPH_DYN_K_CORE_H
#define GRAPH_DYN_K_CORE_H

#include <igraph/igraph.h>
#include <stdbool.h>

/* ============================================================================
 * Dynamic (streaming) k-core maintenance, insertion-only.
 *
 * Maintains the exact coreness of every vertex as edges are inserted,
 * without recomputing the full O(V+E) decomposition per change.
 *
 * Per inserted edge, only the endpoint with the smaller coreness (the root)
 * and vertices with the same coreness reachable from it through same-coreness
 * paths (the "subcore") can change, each by at most +1 [Sariyuce et al.,
 * VLDB 2013, Theorems 1/2/4]. The change is computed by optimistically
 * lifting the whole subcore and peeling back unsupported vertices — the
 * n-order H-index fixpoint of [Liu et al., VLDB 2021, Theorems 3.2/3.5]
 * restricted to the subcore, where it needs no bucket sort because values
 * are binary (K vs K+1).
 *
 * Semantics match igraph_coreness(..., IGRAPH_ALL): self-loops count twice,
 * parallel edges count with multiplicity. Directed graphs are treated as
 * undirected. Edge deletion and distance-generalized (k,h)-cores are out of
 * scope (the stream is insertion-only); see the support-counting helper in
 * dyn_k-core.c for the extension seam.
 *
 * Threading: main thread only (reads the live igraph_t; never mutates it).
 * ============================================================================ */

typedef struct DynKCore DynKCore;

/**
 * Create a maintainer bootstrapped from the current graph via one full
 * igraph_coreness() pass (O(V+E); the graph may be empty).
 * @return New handle, or NULL on allocation/igraph failure.
 */
DynKCore *dyn_kcore_init(const igraph_t *g);

/**
 * Advance the maintained coreness after a batch of edge insertions.
 * The graph g must ALREADY contain the new edges (and any new vertices);
 * new vertices start at coreness 0 and are lifted by their own edges.
 * @param new_edges Flat vector (from0,to0,from1,to1,...) of the edges just
 *                  inserted, in insertion order; NULL means "no new edges,
 *                  just sync the vertex count".
 * @param changed   If not NULL, ids of vertices whose coreness changed are
 *                  appended (duplicates possible when a vertex is lifted
 *                  more than once in the batch).
 * @return false on unrecoverable failure (the maintainer is then stale;
 *         re-create it via dyn_kcore_init), true otherwise.
 */
bool dyn_kcore_on_edges(DynKCore *kc, const igraph_t *g, const igraph_vector_int_t *new_edges, igraph_vector_int_t *changed);

/**
 * Largest coreness currently present (0 for an empty/edgeless graph).
 * Monotonically non-decreasing under insertion-only streaming.
 */
int dyn_kcore_max(const DynKCore *kc);

/**
 * Largest single-edge subcore BFS ever visited (vertices touched, not just
 * lifted), lifetime. The honest worst-case per-edge cost bound: should stay
 * small and not trend upward as the graph grows, if the maintenance is
 * actually local.
 */
int dyn_kcore_max_subcore_size(const DynKCore *kc);

/**
 * Borrowed view of the maintained coreness values, indexed by vertex id.
 * Valid until the next dyn_kcore_on_edges/dyn_kcore_destroy call.
 */
const int *dyn_kcore_values(const DynKCore *kc);

/**
 * Coreness of a single vertex (0 for out-of-range ids).
 */
int dyn_kcore_get(const DynKCore *kc, igraph_integer_t v);

/**
 * Free the maintainer (never touches the graph).
 */
void dyn_kcore_destroy(DynKCore *kc);

#endif // GRAPH_DYN_K_CORE_H
