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

// Network of main paths (p.531, "one path per source ... merged into a single subnetwork";
// restated in Liu, Lu & Ho 2019, https://doi.org/10.1007/s11192-019-03034-x): the union of
// independent Local main path searches, one seeded at each source node (in-degree 0) in the
// graph, using that source's own best outgoing arc(s) rather than a single graph-wide winner.
MainPathSelectionResult *main_path_search_network(const igraph_t *graph, const igraph_vector_t *weights, const igraph_vector_t *strengths, uint32_t node_count, uint32_t edge_count);

#endif
