/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GRAPH_DYN_LEIDEN_H
#define GRAPH_DYN_LEIDEN_H

#include <igraph/igraph.h>
#include <stdbool.h>

/* ============================================================================
 * Dynamic (streaming) Leiden community maintenance, insertion-only.
 *
 * Maintains an approximate Leiden CPM (Constant Potts Model) partition as
 * edges are inserted, without recomputing the full O(V+E) decomposition per
 * batch.
 * Ported from the Dynamic Frontier (DF) heuristic [Sahu 2024, "Heuristic-based
 * Dynamic Leiden"; Sahu, "A Starting Point for Dynamic Community Detection
 * with Leiden Algorithm"], the best-performing/cheapest of the ND/DS/DF
 * marking strategies compared there, with reference to the serial local-move
 * formulas in leiden-communities-openmp-dynamic's inc/leiden.hxx. The
 * reference's frontier-marking heuristic is objective-agnostic (it was
 * derived for modularity); only the delta-quality formula used to accept or
 * reject a move (dyn_leiden_choose in the .c file) is CPM's.
 *
 * Only a source edge endpoint crossing a community boundary (or an
 * intra-community edge, which just flags its community for refinement)
 * enters the frontier; local-moving then expands the frontier to a mover's
 * graph-neighbors, exactly as in the reference. Refinement and aggregation
 * are scoped to the communities actually touched this call (never a full
 * graph rescan), so cost stays proportional to the affected region rather
 * than the whole graph.
 *
 * Semantics match igraph_community_leiden_simple(..., IGRAPH_LEIDEN_OBJECTIVE_CPM,
 * resolution=max(3*density, 0.001), beta=0.01, n_iterations=1) — the same
 * objective and density-scaled resolution heuristic as the static Leiden menu
 * command (wrappers_community.c) and layered_sphere.c, with density =
 * 2*ecount/(vcount*(vcount-1)). The resolution is recomputed from the live
 * graph on every dyn_leiden_init/dyn_leiden_on_edges call, so a region left
 * untouched since the resolution last shifted keeps a partition that was
 * optimal under an older resolution — an accepted approximation, in the same
 * spirit as the frontier heuristic itself never rescanning the whole graph.
 * Unweighted (edge weight 1.0), undirected. Edge deletion is out of scope
 * (the stream is insertion-only, matching dyn_k-core.h).
 *
 * Threading: main thread only (reads the live igraph_t; never mutates it).
 * ============================================================================ */

typedef struct DynLeiden DynLeiden;

/**
 * Create a maintainer bootstrapped from the current graph via one full
 * igraph_community_leiden_simple() pass (O(V+E); the graph may be empty).
 * @return New handle, or NULL on allocation/igraph failure.
 */
DynLeiden *dyn_leiden_init(const igraph_t *g);

/**
 * Advance the maintained community membership after a batch of edge
 * insertions. The graph g must ALREADY contain the new edges (and any new
 * vertices); new vertices start as their own singleton community.
 * @param new_edges Flat vector (from0,to0,from1,to1,...) of the edges just
 *                  inserted, in insertion order; NULL means "no new edges,
 *                  just sync the vertex count".
 * @param changed   If not NULL, ids of vertices whose community changed are
 *                  appended (duplicates possible when a vertex moves more
 *                  than once in the batch, e.g. once in local-moving and
 *                  again in refinement).
 * @return false on unrecoverable failure (the maintainer is then stale;
 *         re-create it via dyn_leiden_init), true otherwise.
 */
bool dyn_leiden_on_edges(DynLeiden *dl, const igraph_t *g, const igraph_vector_int_t *new_edges, igraph_vector_int_t *changed);

/**
 * Borrowed view of the maintained community id per vertex — the id of a
 * representative member vertex of that community, stable across calls
 * unless that representative itself changes community. Valid until the next
 * dyn_leiden_on_edges/dyn_leiden_destroy call.
 */
const igraph_integer_t *dyn_leiden_membership(const DynLeiden *dl);

/**
 * Community of a single vertex (the vertex's own id if out of range, so a
 * stale/out-of-range query degrades to "its own singleton" rather than 0).
 */
igraph_integer_t dyn_leiden_get(const DynLeiden *dl, igraph_integer_t v);

/**
 * CPM resolution (gamma) used by the most recent dyn_leiden_init /
 * dyn_leiden_on_edges call: max(3 * 2*ecount/(vcount*(vcount-1)), 0.001) over
 * the live graph at that time. Lets tests/debug evaluate quality under the
 * exact gamma the maintainer optimized.
 */
double dyn_leiden_resolution(const DynLeiden *dl);

/**
 * Number of distinct communities currently maintained. O(V) (a dedup scan
 * over the membership array) — intended for the throttled debug report,
 * never the per-poll hot path.
 */
int dyn_leiden_community_count(const DynLeiden *dl);

/**
 * Largest single on_edges() frontier ever processed (vertices dequeued from
 * the local-move worklist across local-moving + refinement + aggregation),
 * lifetime. The honest worst-case per-batch cost bound: should stay small
 * and not trend upward as the graph grows, if the maintenance is actually
 * local.
 */
int dyn_leiden_max_frontier_size(const DynLeiden *dl);

/**
 * Vertices touched by the most recent on_edges() call — the per-poll
 * coverage numerator for the debug report (compare against vertex count for
 * a coverage ratio).
 */
int dyn_leiden_last_frontier_size(const DynLeiden *dl);

/**
 * Free the maintainer (never touches the graph).
 */
void dyn_leiden_destroy(DynLeiden *dl);

#endif // GRAPH_DYN_LEIDEN_H
