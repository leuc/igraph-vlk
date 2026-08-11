/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GRAPH_MAIN_PATH_SEARCH_H
#define GRAPH_MAIN_PATH_SEARCH_H

#include "graph/main_path_cache.h"
#include <igraph.h>

// Main-path search variants from Liu, J. S., & Lu, L. Y. Y. (2012),
// "An integrated approach for main path analysis: Development of the Hirsch index as an
// example," https://doi.org/10.1002/asi.21692, p. 531-532, not covered by the existing
// GPU-computed Global Path and Basket selections. These run on the CPU using cached
// main-path-weight-{method}/main-path-strength-{method} edge attributes from Step 1 Weighting
// and are recomputed for each menu invocation.
// All functions assume `graph` is a directed acyclic graph (the caller's existing precondition
// for Step 1 Weighting) and that weights/strengths are sized `edge_count`, ordered by
// IGRAPH_EDGEORDER_ID (as produced by main_path_cache_load_weight_and_strength). Returns NULL
// on allocation failure.

// Local main path (p.531): among all arcs leaving any source node (in-degree 0), take the
// highest-weight one(s) (ties kept) as the start; from each active node, independently
// take its own highest-weight outgoing arc(s) (ties kept); repeat until every branch reaches a
// sink. May flag a branching subgraph, not a single simple path, when ties occur.
MainPathSelectionResult *main_path_search_local(const igraph_t *graph, const igraph_vector_t *weights, const igraph_vector_t *strengths, uint32_t node_count, uint32_t edge_count);

// Backward local main path (p.531-532): the same greedy procedure run against inward arcs,
// seeded from sink node(s) (out-degree 0) by their globally highest-weight incoming arc(s),
// walking toward sources.
MainPathSelectionResult *main_path_search_backward_local(const igraph_t *graph, const igraph_vector_t *weights, const igraph_vector_t *strengths, uint32_t node_count, uint32_t edge_count);

// Multiple main paths (p.532): Local main path, but at every greedy step (including the initial
// source-arc selection) keep all arcs within tolerance_pct of the current step's max weight
// instead of only the exact tie set. tolerance_pct=20.0 matches Liu & Lu's own case study
// (p.535), explicitly called an arbitrary parameter by the source.
MainPathSelectionResult *main_path_search_multiple(const igraph_t *graph, const igraph_vector_t *weights, const igraph_vector_t *strengths, uint32_t node_count, uint32_t edge_count, double tolerance_pct);

// Key-route main path (p.532): the num_seeds distinct highest-weight arcs (ties broken by edge
// index, an implementation tie-break) are taken as independent seeds;
// for each seed, a forward-local search runs from its head to a sink and a backward-local search
// runs from its tail to a source. The result is the union of each seed and its forward and
// backward local searches.
MainPathSelectionResult *main_path_search_key_route(const igraph_t *graph, const igraph_vector_t *weights, const igraph_vector_t *strengths, uint32_t node_count, uint32_t edge_count, int num_seeds);

// Hummon, N.P. & Carley, K. (1993), "Social networks as normal science," Social Networks 15(1),
// 71-106, https://doi.org/10.1016/0378-8733(93)90022-D, pp. 82-84, 93-94. SPLC-only by design (see
// compute_main_path_splc_valued_network in main_path.c) -- this method is intrinsically paired with
// Hummon & Doreian (1989)'s exhaustive-search-tree tie-frequency weighting (Sedgewick 1983, ch.39),
// implemented here by the equivalent topological-order DP from Batagelj (2003).
// From every node, repeatedly choose one outgoing edge with probability proportional to its weight
// (a uniform random draw against the relative cumulative distribution of outgoing weights, p.82)
// until a sink is reached -- one sampled main path per node. Accumulates tie frequency: how many of
// the node_count sampled paths traverse each edge. Keeps only ties with tie frequency >=
// threshold_fraction * the maximum tie frequency observed in the run. The paper's case-study
// cutoff, 25 of 80 (about 31%), is arbitrary. Both endpoints of surviving ties are flagged.
// result->component_id labels which structure each flagged node belongs to
// (connected components of the surviving-tie subgraph, linked wherever ties share a node, p.93),
// -1 for unflagged nodes. result->strengths stores tie frequency. Endpoint frequency (p.83) is
// not computed because it is not consumed by the selection result.
MainPathSelectionResult *main_path_search_valued_network(const igraph_t *graph, const igraph_vector_t *weights, uint32_t node_count, uint32_t edge_count, double threshold_fraction);

#endif
