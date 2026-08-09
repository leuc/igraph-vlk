/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GRAPH_MAIN_PATH_SEARCH_H
#define GRAPH_MAIN_PATH_SEARCH_H

#include "graph/main_path_cache.h"
#include <igraph.h>

// The five main-path search/selection variants from Liu, J. S., & Lu, L. Y. Y. (2012),
// "An integrated approach for main path analysis: Development of the Hirsch index as an
// example," https://doi.org/10.1002/asi.21692, p. 531-532, not covered by the existing
// GPU-computed Global main path ("Global Path") / Basket selections (renderer_criticality.c +
// main_path.comp). These run CPU-side on the already-cached
// main-path-weight-{method}/main-path-strength-{method} edge attributes from Step 1 Weighting
// (own engineering choice: single greedy/linear sweeps over an already-materialized O(V+E)
// weight array, unlike weighting itself, which is the genuinely GPU-parallel-justified work),
// and are recomputed fresh on every menu invocation rather than cached as attributes.
//
// All functions assume `graph` is a directed acyclic graph (the caller's existing precondition
// for Step 1 Weighting) and that weights/strengths are sized `edge_count`, ordered by
// IGRAPH_EDGEORDER_ID (as produced by main_path_cache_load_weight_and_strength). Returns NULL
// on allocation failure.

// Local main path (p.531): among all arcs leaving any source node (in-degree 0), take the
// globally highest-weight one(s) (ties kept) as the start; from each active node, independently
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
// index, an implementation choice not specified by the source) are taken as independent seeds;
// for each seed, a forward-local search runs from its head to a sink and a backward-local search
// runs from its tail to a source (local mode only for both directions -- re-rooted global search
// is an explicit scoping choice for this task, not sourced). Result is the union of {seed} +
// forward arcs + backward arcs across all seeds; guarantees every seed arc is included by
// construction.
MainPathSelectionResult *main_path_search_key_route(const igraph_t *graph, const igraph_vector_t *weights, const igraph_vector_t *strengths, uint32_t node_count, uint32_t edge_count, int num_seeds);

// Hummon, N.P. & Carley, K. (1993), "Social networks as normal science," Social Networks 15(1),
// 71-106, https://doi.org/10.1016/0378-8733(93)90022-D, pp. 82-84, 93-94. SPLC-only by design (see
// compute_main_path_splc_valued_network in main_path.c) -- this method is intrinsically paired with
// Hummon & Doreian (1989)'s exhaustive-search-tree tie-frequency weighting (Sedgewick 1983, ch.39),
// which is what this app's SPLC Step-1 weighting already computes via a polynomial topological-order
// DP (Batagelj 2003) equivalent to that exhaustive enumeration.
//
// From every node, repeatedly choose one outgoing edge with probability proportional to its weight
// (a uniform random draw against the relative cumulative distribution of outgoing weights, p.82)
// until a sink is reached -- one sampled main path per node. Accumulates tie frequency: how many of
// the node_count sampled paths traverse each edge. Keeps only ties with tie frequency >=
// threshold_fraction * (the max tie frequency actually observed in this run) -- relative to the
// observed max, not to node_count or edge_count: on a broad/large DAG, probability mass disperses
// across many parallel edges instead of concentrating, so the achievable max stays small regardless
// of graph size, and a threshold derived from graph size rather than the observed max can end up
// unreachable by any edge. Matches main_path_search_multiple's tolerance_pct, which is likewise
// relative to the current step's own observed max. Their own case study's cutoff (>=25 of an
// observed max of 80, i.e. ~31%) is stated as arbitrary, not a rule -- exposed as a caller parameter
// here, matching this file's tolerance_pct/num_seeds parameters, and flags both endpoints of every
// surviving tie -- the union of every surviving tie's endpoints, i.e. every "main path structure"
// (p.93-94) together, since multiple simultaneously-valid structures are expected, not a bug (the
// source's own theory: several structures can coexist, "distinguished from the others by its
// terminal node(s)"). result->component_id labels which structure each flagged node belongs to
// (connected components of the surviving-tie subgraph, linked wherever ties share a node, p.93),
// -1 for unflagged nodes; consumed by apply_main_path_selection to color each structure distinctly
// rather than rendering them as one undifferentiated blob. result->strengths is tie frequency, not
// a pass-through of the input weights unlike the other search variants in this file -- tie
// frequency is the paper's own key statistic ("the tie frequency measures the size of tributaries,"
// p.83).
// Endpoint frequency (p.83) is part of the paper's Stage 2 bookkeeping but is not computed here:
// nothing in this function's output consumes it (own scoping choice, consistent with dropping
// component-id labeling above).
MainPathSelectionResult *main_path_search_valued_network(const igraph_t *graph, const igraph_vector_t *weights, uint32_t node_count, uint32_t edge_count, double threshold_fraction);

#endif
