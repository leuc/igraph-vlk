/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "graph/main_path_search.h"

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>

static MainPathSelectionResult *main_path_search_alloc_result(const igraph_vector_t *strengths, const bool *flags, uint32_t node_count, uint32_t edge_count)
{
	MainPathSelectionResult *result = calloc(1, sizeof(*result));
	if (result) {
		result->strengths = malloc(sizeof(float) * (edge_count > 0 ? edge_count : 1));
		result->flags = malloc(sizeof(int) * (node_count > 0 ? node_count : 1));
	}
	if (!result || !result->strengths || !result->flags) {
		main_path_cache_selection_free(result);
		return NULL;
	}
	result->node_count = node_count;
	result->edge_count = edge_count;
	for (uint32_t e = 0; e < edge_count; e++)
		result->strengths[e] = (float)VECTOR(*strengths)[e];
	for (uint32_t v = 0; v < node_count; v++)
		result->flags[v] = flags[v] ? 1 : 0;
	return result;
}

// Expands `flags` (already seeded with the initial active node(s) set true) outward via `mode`
// (IGRAPH_OUT for forward, IGRAPH_IN for backward): at each active node, follows incident edges
// whose weight is within tolerance_pct of that node's own best incident-edge weight
// (tolerance_pct == 0 keeps exact ties only, safe under float equality since both values are
// read from the same weights vector), flags their far endpoint, and continues from there.
// Terminates naturally: the graph is acyclic and each node is enqueued at most once (on first
// flagging).
static void main_path_search_propagate(const igraph_t *graph, const igraph_vector_t *weights, igraph_neimode_t mode, double tolerance_pct, uint32_t node_count, bool *flags)
{
	igraph_dqueue_int_t queue;
	if (igraph_dqueue_int_init(&queue, 0) != IGRAPH_SUCCESS)
		return;
	for (uint32_t v = 0; v < node_count; v++)
		if (flags[v] && igraph_dqueue_int_push(&queue, (igraph_integer_t)v) != IGRAPH_SUCCESS) {
			igraph_dqueue_int_destroy(&queue);
			return;
		}
	igraph_vector_int_t incident;
	if (igraph_vector_int_init(&incident, 0) != IGRAPH_SUCCESS) {
		igraph_dqueue_int_destroy(&queue);
		return;
	}
	while (!igraph_dqueue_int_empty(&queue)) {
		igraph_integer_t u = igraph_dqueue_int_pop(&queue);
		if (igraph_incident(graph, &incident, u, mode, IGRAPH_LOOPS) != IGRAPH_SUCCESS)
			break;
		igraph_integer_t n = igraph_vector_int_size(&incident);
		if (n == 0)
			continue;
		double best = -INFINITY;
		for (igraph_integer_t i = 0; i < n; i++) {
			double w = VECTOR(*weights)[VECTOR(incident)[i]];
			if (w > best)
				best = w;
		}
		double threshold = best - fabs(best) * (tolerance_pct / 100.0);
		for (igraph_integer_t i = 0; i < n; i++) {
			igraph_integer_t e = VECTOR(incident)[i];
			if (VECTOR(*weights)[e] < threshold)
				continue;
			igraph_integer_t other = mode == IGRAPH_OUT ? IGRAPH_TO(graph, e) : IGRAPH_FROM(graph, e);
			if (!flags[other]) {
				flags[other] = true;
				if (igraph_dqueue_int_push(&queue, other) != IGRAPH_SUCCESS)
					goto done;
			}
		}
	}
done:
	igraph_vector_int_destroy(&incident);
	igraph_dqueue_int_destroy(&queue);
}

// Flags the endpoints of the globally highest-weight arc(s), within tolerance_pct of the max,
// among all arcs leaving (mode=IGRAPH_OUT) or entering (mode=IGRAPH_IN) any node whose opposite
// degree is zero -- i.e. true sources for forward search, true sinks for backward search. This
// is the "among all arcs leaving any source, take the highest-weight one" initial step that
// Local/Multiple main path use (Liu & Lu 2012, p.531, step 1), as distinct from the per-node
// argmax main_path_search_propagate applies at every subsequent step.
static bool main_path_search_seed_from_boundary(const igraph_t *graph, const igraph_vector_t *weights, igraph_neimode_t mode, double tolerance_pct, uint32_t node_count, bool *flags)
{
	igraph_neimode_t boundary_mode = mode == IGRAPH_OUT ? IGRAPH_IN : IGRAPH_OUT;
	igraph_vector_int_t degrees;
	if (igraph_vector_int_init(&degrees, 0) != IGRAPH_SUCCESS)
		return false;
	if (igraph_degree(graph, &degrees, igraph_vss_all(), boundary_mode, IGRAPH_LOOPS) != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(&degrees);
		return false;
	}
	igraph_vector_int_t incident;
	if (igraph_vector_int_init(&incident, 0) != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(&degrees);
		return false;
	}
	double best = -INFINITY;
	for (uint32_t v = 0; v < node_count; v++) {
		if (VECTOR(degrees)[v] != 0)
			continue;
		if (igraph_incident(graph, &incident, (igraph_integer_t)v, mode, IGRAPH_LOOPS) != IGRAPH_SUCCESS)
			continue;
		igraph_integer_t n = igraph_vector_int_size(&incident);
		for (igraph_integer_t i = 0; i < n; i++) {
			double w = VECTOR(*weights)[VECTOR(incident)[i]];
			if (w > best)
				best = w;
		}
	}
	bool any = false;
	if (isfinite(best)) {
		double threshold = best - fabs(best) * (tolerance_pct / 100.0);
		for (uint32_t v = 0; v < node_count; v++) {
			if (VECTOR(degrees)[v] != 0)
				continue;
			if (igraph_incident(graph, &incident, (igraph_integer_t)v, mode, IGRAPH_LOOPS) != IGRAPH_SUCCESS)
				continue;
			igraph_integer_t n = igraph_vector_int_size(&incident);
			for (igraph_integer_t i = 0; i < n; i++) {
				igraph_integer_t e = VECTOR(incident)[i];
				if (VECTOR(*weights)[e] < threshold)
					continue;
				igraph_integer_t other = mode == IGRAPH_OUT ? IGRAPH_TO(graph, e) : IGRAPH_FROM(graph, e);
				flags[v] = true;
				flags[other] = true;
				any = true;
			}
		}
	}
	igraph_vector_int_destroy(&incident);
	igraph_vector_int_destroy(&degrees);
	return any;
}

static MainPathSelectionResult *main_path_search_local_directed(const igraph_t *graph, const igraph_vector_t *weights, const igraph_vector_t *strengths, uint32_t node_count, uint32_t edge_count, igraph_neimode_t mode, double tolerance_pct)
{
	bool *flags = calloc(node_count > 0 ? node_count : 1, sizeof(bool));
	if (!flags)
		return NULL;
	main_path_search_seed_from_boundary(graph, weights, mode, tolerance_pct, node_count, flags);
	main_path_search_propagate(graph, weights, mode, tolerance_pct, node_count, flags);
	MainPathSelectionResult *result = main_path_search_alloc_result(strengths, flags, node_count, edge_count);
	free(flags);
	return result;
}

MainPathSelectionResult *main_path_search_local(const igraph_t *graph, const igraph_vector_t *weights, const igraph_vector_t *strengths, uint32_t node_count, uint32_t edge_count)
{
	return main_path_search_local_directed(graph, weights, strengths, node_count, edge_count, IGRAPH_OUT, 0.0);
}

MainPathSelectionResult *main_path_search_backward_local(const igraph_t *graph, const igraph_vector_t *weights, const igraph_vector_t *strengths, uint32_t node_count, uint32_t edge_count)
{
	return main_path_search_local_directed(graph, weights, strengths, node_count, edge_count, IGRAPH_IN, 0.0);
}

MainPathSelectionResult *main_path_search_multiple(const igraph_t *graph, const igraph_vector_t *weights, const igraph_vector_t *strengths, uint32_t node_count, uint32_t edge_count, double tolerance_pct)
{
	return main_path_search_local_directed(graph, weights, strengths, node_count, edge_count, IGRAPH_OUT, tolerance_pct);
}

typedef struct
{
	igraph_integer_t edge;
	double weight;
} MainPathSearchWeightedEdge;

static int main_path_search_compare_weighted_edge_desc(const void *a, const void *b)
{
	const MainPathSearchWeightedEdge *wa = a;
	const MainPathSearchWeightedEdge *wb = b;
	if (wa->weight > wb->weight)
		return -1;
	if (wa->weight < wb->weight)
		return 1;
	if (wa->edge < wb->edge)
		return -1;
	if (wa->edge > wb->edge)
		return 1;
	return 0;
}

MainPathSelectionResult *main_path_search_key_route(const igraph_t *graph, const igraph_vector_t *weights, const igraph_vector_t *strengths, uint32_t node_count, uint32_t edge_count, int num_seeds)
{
	size_t padded_nodes = node_count > 0 ? node_count : 1;
	bool *result_flags = calloc(padded_nodes, sizeof(bool));
	bool *fwd_flags = calloc(padded_nodes, sizeof(bool));
	bool *bwd_flags = calloc(padded_nodes, sizeof(bool));
	MainPathSearchWeightedEdge *sorted = malloc(sizeof(*sorted) * (edge_count > 0 ? edge_count : 1));
	if (!result_flags || !fwd_flags || !bwd_flags || !sorted) {
		free(result_flags);
		free(fwd_flags);
		free(bwd_flags);
		free(sorted);
		return NULL;
	}
	for (uint32_t e = 0; e < edge_count; e++) {
		sorted[e].edge = (igraph_integer_t)e;
		sorted[e].weight = VECTOR(*weights)[e];
	}
	qsort(sorted, edge_count, sizeof(*sorted), main_path_search_compare_weighted_edge_desc);

	int seeds = num_seeds < 0 ? 0 : num_seeds;
	if ((uint32_t)seeds > edge_count)
		seeds = (int)edge_count;
	for (int i = 0; i < seeds; i++) {
		igraph_integer_t e = sorted[i].edge;
		igraph_integer_t u = IGRAPH_FROM(graph, e);
		igraph_integer_t v = IGRAPH_TO(graph, e);
		result_flags[u] = true;
		result_flags[v] = true;
		fwd_flags[v] = true;
		bwd_flags[u] = true;
	}
	free(sorted);

	main_path_search_propagate(graph, weights, IGRAPH_OUT, 0.0, node_count, fwd_flags);
	main_path_search_propagate(graph, weights, IGRAPH_IN, 0.0, node_count, bwd_flags);
	for (uint32_t v = 0; v < node_count; v++)
		result_flags[v] = result_flags[v] || fwd_flags[v] || bwd_flags[v];

	MainPathSelectionResult *result = main_path_search_alloc_result(strengths, result_flags, node_count, edge_count);
	free(result_flags);
	free(fwd_flags);
	free(bwd_flags);
	return result;
}

MainPathSelectionResult *main_path_search_network(const igraph_t *graph, const igraph_vector_t *weights, const igraph_vector_t *strengths, uint32_t node_count, uint32_t edge_count)
{
	bool *flags = calloc(node_count > 0 ? node_count : 1, sizeof(bool));
	if (!flags)
		return NULL;
	igraph_vector_int_t in_degrees;
	if (igraph_vector_int_init(&in_degrees, 0) != IGRAPH_SUCCESS) {
		free(flags);
		return NULL;
	}
	if (igraph_degree(graph, &in_degrees, igraph_vss_all(), IGRAPH_IN, IGRAPH_LOOPS) != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(&in_degrees);
		free(flags);
		return NULL;
	}
	for (uint32_t v = 0; v < node_count; v++)
		if (VECTOR(in_degrees)[v] == 0)
			flags[v] = true;
	igraph_vector_int_destroy(&in_degrees);

	// Reachability-from-a-set equals the union of reachability-from-each-element, so seeding
	// every source at once and running a single propagation pass is equivalent to (and cheaper
	// than) unioning one independent per-source Local search per source node.
	main_path_search_propagate(graph, weights, IGRAPH_OUT, 0.0, node_count, flags);

	MainPathSelectionResult *result = main_path_search_alloc_result(strengths, flags, node_count, edge_count);
	free(flags);
	return result;
}
