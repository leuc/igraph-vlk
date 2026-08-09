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

// Structure-linking (Hummon & Carley 1993, p.93): labels connected components of the
// surviving-tie subgraph ("the cited article of one tie is the citing article of another tie" --
// components are linked wherever ties share a node, in either direction) among nodes already
// flagged by the threshold pass in main_path_search_valued_network. Does not change which nodes
// are flagged -- labeling only, consumed by apply_main_path_selection to color each fragment
// distinctly. Returns a malloc'd node_count-sized array (component id, or -1 for a node not in
// flags) on success, NULL on allocation/init failure (component_id is best-effort/cosmetic, so
// the caller should still return the selection with component_id left NULL rather than fail the
// whole result).
static int *main_path_search_label_components(const igraph_t *graph, const igraph_vector_t *tie_frequency, double threshold, const bool *flags, uint32_t node_count)
{
	int *component_id = malloc(sizeof(int) * (node_count > 0 ? node_count : 1));
	if (!component_id)
		return NULL;
	for (uint32_t v = 0; v < node_count; v++)
		component_id[v] = -1;

	igraph_dqueue_int_t queue;
	if (igraph_dqueue_int_init(&queue, 0) != IGRAPH_SUCCESS) {
		free(component_id);
		return NULL;
	}
	igraph_vector_int_t incident;
	if (igraph_vector_int_init(&incident, 0) != IGRAPH_SUCCESS) {
		igraph_dqueue_int_destroy(&queue);
		free(component_id);
		return NULL;
	}

	int next_id = 0;
	for (uint32_t start = 0; start < node_count; start++) {
		if (!flags[start] || component_id[start] != -1)
			continue;
		component_id[start] = next_id;
		if (igraph_dqueue_int_push(&queue, (igraph_integer_t)start) != IGRAPH_SUCCESS)
			break;
		while (!igraph_dqueue_int_empty(&queue)) {
			igraph_integer_t u = igraph_dqueue_int_pop(&queue);
			igraph_neimode_t modes[2] = {IGRAPH_OUT, IGRAPH_IN};
			for (int m = 0; m < 2; m++) {
				if (igraph_incident(graph, &incident, u, modes[m], IGRAPH_LOOPS) != IGRAPH_SUCCESS)
					continue;
				igraph_integer_t n = igraph_vector_int_size(&incident);
				for (igraph_integer_t i = 0; i < n; i++) {
					igraph_integer_t e = VECTOR(incident)[i];
					double freq = VECTOR(*tie_frequency)[e];
					if (freq <= 0.0 || freq < threshold)
						continue;
					igraph_integer_t other = modes[m] == IGRAPH_OUT ? IGRAPH_TO(graph, e) : IGRAPH_FROM(graph, e);
					if (component_id[other] == -1 && flags[other]) {
						component_id[other] = next_id;
						if (igraph_dqueue_int_push(&queue, other) != IGRAPH_SUCCESS)
							goto done;
					}
				}
			}
		}
		next_id++;
	}
done:
	igraph_vector_int_destroy(&incident);
	igraph_dqueue_int_destroy(&queue);
	return component_id;
}

MainPathSelectionResult *main_path_search_valued_network(const igraph_t *graph, const igraph_vector_t *weights, uint32_t node_count, uint32_t edge_count, double threshold_fraction)
{
	igraph_vector_t tie_frequency;
	if (igraph_vector_init(&tie_frequency, edge_count) != IGRAPH_SUCCESS)
		return NULL;
	igraph_vector_null(&tie_frequency);

	igraph_vector_int_t out_edges;
	if (igraph_vector_int_init(&out_edges, 0) != IGRAPH_SUCCESS) {
		igraph_vector_destroy(&tie_frequency);
		return NULL;
	}

	igraph_rng_t *rng = igraph_rng_default();
	bool ok = true;
	for (uint32_t start = 0; start < node_count && ok; start++) {
		igraph_integer_t current = (igraph_integer_t)start;
		for (;;) {
			if (igraph_incident(graph, &out_edges, current, IGRAPH_OUT, IGRAPH_LOOPS) != IGRAPH_SUCCESS) {
				ok = false;
				break;
			}
			igraph_integer_t out_degree = igraph_vector_int_size(&out_edges);
			if (out_degree == 0)
				break;

			double total_weight = 0.0;
			for (igraph_integer_t i = 0; i < out_degree; i++)
				total_weight += VECTOR(*weights)[VECTOR(out_edges)[i]];

			double r = igraph_rng_get_unif(rng, 0.0, total_weight);
			double cumulative = 0.0;
			igraph_integer_t selected = VECTOR(out_edges)[out_degree - 1];
			for (igraph_integer_t i = 0; i < out_degree; i++) {
				igraph_integer_t e = VECTOR(out_edges)[i];
				cumulative += VECTOR(*weights)[e];
				if (r <= cumulative) {
					selected = e;
					break;
				}
			}

			VECTOR(tie_frequency)[selected] += 1.0;
			current = IGRAPH_TO(graph, selected);
		}
	}
	igraph_vector_int_destroy(&out_edges);
	if (!ok) {
		igraph_vector_destroy(&tie_frequency);
		return NULL;
	}

	// Threshold relative to the max tie frequency actually observed in this run, not to
	// node_count: on a broad/large DAG, probability mass disperses across many parallel edges
	// instead of concentrating, so the achievable max stays small regardless of graph size --
	// a fixed fraction of node_count can demand more than any edge could ever reach. Matches
	// main_path_search_multiple's tolerance_pct, which is likewise relative to the current step's
	// observed max, not to any global graph-size-derived quantity.
	double max_tie_frequency = 0.0;
	for (uint32_t e = 0; e < edge_count; e++)
		if (VECTOR(tie_frequency)[e] > max_tie_frequency)
			max_tie_frequency = VECTOR(tie_frequency)[e];
	double threshold = threshold_fraction * max_tie_frequency;
	bool *flags = calloc(node_count > 0 ? node_count : 1, sizeof(bool));
	if (!flags) {
		igraph_vector_destroy(&tie_frequency);
		return NULL;
	}
	for (uint32_t e = 0; e < edge_count; e++) {
		double freq = VECTOR(tie_frequency)[e];
		if (freq <= 0.0 || freq < threshold)
			continue;
		flags[IGRAPH_FROM(graph, e)] = true;
		flags[IGRAPH_TO(graph, e)] = true;
	}

	MainPathSelectionResult *result = main_path_search_alloc_result(&tie_frequency, flags, node_count, edge_count);
	if (result)
		result->component_id = main_path_search_label_components(graph, &tie_frequency, threshold, flags, node_count);
	free(flags);
	igraph_vector_destroy(&tie_frequency);
	return result;
}
