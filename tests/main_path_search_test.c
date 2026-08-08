/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Hand-verified against the project's main-path-analysis reference skill
 * (~/.claude/skills/main-path-analysis/references/path-search-variants.md), which sources the
 * baseline toy DAG from Liu & Lu (2012). The key-route-divergence and tolerance graphs are
 * derived (not from any source) specifically to exercise behavior the toy graph is too small to
 * show; expected results are hand-computed and traced step by step in comments below.
 */

#include "graph/main_path_search.h"

#include <stdio.h>
#include <string.h>

static int g_failures = 0;

static void check(bool cond, const char *what)
{
	if (!cond) {
		fprintf(stderr, "main_path_search_test: FAILED: %s\n", what);
		g_failures++;
	}
}

static bool set_weighted_edges(igraph_t *graph, uint32_t edge_count, const double *weights, const double *strengths, igraph_vector_t *weights_out, igraph_vector_t *strengths_out)
{
	if (igraph_vector_init(weights_out, edge_count) != IGRAPH_SUCCESS)
		return false;
	if (igraph_vector_init(strengths_out, edge_count) != IGRAPH_SUCCESS) {
		igraph_vector_destroy(weights_out);
		return false;
	}
	for (uint32_t e = 0; e < edge_count; e++) {
		VECTOR(*weights_out)[e] = weights[e];
		VECTOR(*strengths_out)[e] = strengths[e];
	}
	(void)graph;
	return true;
}

// Checks result->flags matches exactly the node ids listed in `expected` (NULL-terminated by
// passing expected_count), regardless of order.
static void check_flags(const MainPathSelectionResult *result, const char *label, const int *expected, int expected_count, int node_count)
{
	if (!result) {
		fprintf(stderr, "main_path_search_test: FAILED: %s: result is NULL\n", label);
		g_failures++;
		return;
	}
	bool want[64] = {0};
	for (int i = 0; i < expected_count; i++)
		want[expected[i]] = true;
	for (int v = 0; v < node_count; v++) {
		if ((result->flags[v] != 0) != want[v]) {
			fprintf(stderr, "main_path_search_test: FAILED: %s: node %d flagged=%d want=%d\n", label, v, result->flags[v], want[v]);
			g_failures++;
		}
	}
}

// Baseline toy DAG (path-search-variants.md's own worked example):
//   S->A 10   A->C 3   C->T 3
//   S->B 6    B->D 9   D->T 9
// S=0 A=1 B=2 C=3 D=4 T=5
static void test_baseline_graph(void)
{
	igraph_t graph;
	if (igraph_small(&graph, 6, IGRAPH_DIRECTED, 0, 1, 1, 3, 3, 5, 0, 2, 2, 4, 4, 5, -1) != IGRAPH_SUCCESS) {
		g_failures++;
		return;
	}
	const double w[6] = {10, 3, 3, 6, 9, 9};
	igraph_vector_t weights, strengths;
	if (!set_weighted_edges(&graph, 6, w, w, &weights, &strengths)) {
		igraph_destroy(&graph);
		g_failures++;
		return;
	}

	// Local: S->A->C->T, total weight 16.
	MainPathSelectionResult *local = main_path_search_local(&graph, &weights, &strengths, 6, 6);
	const int local_expected[] = {0, 1, 3, 5};
	check_flags(local, "baseline local", local_expected, 4, 6);

	// Strengths must be copied through unchanged for every edge, regardless of selection.
	if (local) {
		bool strengths_ok = true;
		for (uint32_t e = 0; e < 6; e++)
			if (local->strengths[e] != (float)w[e])
				strengths_ok = false;
		check(strengths_ok, "baseline local strengths copied through unchanged");
	}
	main_path_cache_selection_free(local);

	// Backward local: T<-D<-B<-S, total weight 24 (matches Global here, still a valid check).
	MainPathSelectionResult *backward = main_path_search_backward_local(&graph, &weights, &strengths, 6, 6);
	const int backward_expected[] = {0, 2, 4, 5};
	check_flags(backward, "baseline backward local", backward_expected, 4, 6);
	main_path_cache_selection_free(backward);

	// Key-route seeded on the single highest-weight arc S->A (K=1): forward-local from A reaches
	// A->C->T; backward-local from S has no incoming edges. Degenerate case where the seed
	// coincides with Local's own start arc (per the skill's own disclosure).
	MainPathSelectionResult *key_route = main_path_search_key_route(&graph, &weights, &strengths, 6, 6, 1);
	const int key_route_expected[] = {0, 1, 3, 5};
	check_flags(key_route, "baseline key-route K=1", key_route_expected, 4, 6);
	main_path_cache_selection_free(key_route);

	// Network of main paths: the graph has a single source (S), so the per-source union
	// collapses to exactly the Local result -- a trivial but correct check of the union logic.
	MainPathSelectionResult *network = main_path_search_network(&graph, &weights, &strengths, 6, 6);
	const int network_expected[] = {0, 1, 3, 5};
	check_flags(network, "baseline network", network_expected, 4, 6);
	main_path_cache_selection_free(network);

	igraph_vector_destroy(&strengths);
	igraph_vector_destroy(&weights);
	igraph_destroy(&graph);
}

// Key-route divergence graph: a dangling shortcut M->N carries the single highest weight in the
// graph (20) yet lies on neither the local path (S->A->C->T=16) nor the global path
// (S->B->D->T=24; S->M->N->T=21 is not global-optimal either). S=0 A=1 B=2 C=3 D=4 T=5 M=6 N=7.
static void test_key_route_divergence_graph(void)
{
	igraph_t graph;
	if (igraph_small(&graph, 8, IGRAPH_DIRECTED, 0, 1, 1, 3, 3, 5, 0, 2, 2, 4, 4, 5, 0, 6, 6, 7, 7, 5, -1) != IGRAPH_SUCCESS) {
		g_failures++;
		return;
	}
	const double w[9] = {10, 3, 3, 6, 9, 9, 0.5, 20, 0.5};
	igraph_vector_t weights, strengths;
	if (!set_weighted_edges(&graph, 9, w, w, &weights, &strengths)) {
		igraph_destroy(&graph);
		g_failures++;
		return;
	}

	// Key-route seeded on M->N (the single highest-weight arc, K=1): forward-local from N reaches
	// N->T; backward-local from M reaches S->M. Result {S,M,N,T} is distinct from both Local
	// {S,A,C,T} and Global {S,B,D,T}, proving the "seed always included" guarantee does something.
	MainPathSelectionResult *key_route = main_path_search_key_route(&graph, &weights, &strengths, 8, 9, 1);
	const int expected[] = {0, 5, 6, 7};
	check_flags(key_route, "divergence key-route K=1", expected, 4, 8);
	main_path_cache_selection_free(key_route);

	igraph_vector_destroy(&strengths);
	igraph_vector_destroy(&weights);
	igraph_destroy(&graph);
}

// Tolerance graph: near-tied top two source arcs (10 vs 9.5). S=0 A=1 B=2 C=3 D=4 T=5.
//   S->A 10   A->C 5   C->T 1
//   S->B 9.5  B->D 5   D->T 1
static void test_multiple_tolerance_graph(void)
{
	igraph_t graph;
	if (igraph_small(&graph, 6, IGRAPH_DIRECTED, 0, 1, 1, 3, 3, 5, 0, 2, 2, 4, 4, 5, -1) != IGRAPH_SUCCESS) {
		g_failures++;
		return;
	}
	const double w[6] = {10, 5, 1, 9.5, 5, 1};
	igraph_vector_t weights, strengths;
	if (!set_weighted_edges(&graph, 6, w, w, &weights, &strengths)) {
		igraph_destroy(&graph);
		g_failures++;
		return;
	}

	// At 20% tolerance (threshold = current max x 0.8 at each step): step 1 threshold = 8.0,
	// S->B=9.5 >= 8.0 is kept alongside S->A; every downstream step has a single unambiguous arc,
	// so nothing is pruned. All 6 arcs survive -- every node is flagged.
	MainPathSelectionResult *kept = main_path_search_multiple(&graph, &weights, &strengths, 6, 6, 20.0);
	const int kept_expected[] = {0, 1, 2, 3, 4, 5};
	check_flags(kept, "tolerance 20% keeps both branches", kept_expected, 6, 6);
	main_path_cache_selection_free(kept);

	// At a stricter 4% tolerance (threshold = 10 x 0.96 = 9.6 > 9.5): S->B is dropped at step 1,
	// only S->A survives, and the branch continues A->C->T. B and D are excluded entirely (B's
	// only incoming arc was the dropped S->B). Uses 4%, not 5%, to avoid a boundary-equality edge
	// case against 9.5.
	MainPathSelectionResult *pruned = main_path_search_multiple(&graph, &weights, &strengths, 6, 6, 4.0);
	const int pruned_expected[] = {0, 1, 3, 5};
	check_flags(pruned, "tolerance 4% drops the near-tied branch", pruned_expected, 4, 6);
	main_path_cache_selection_free(pruned);

	igraph_vector_destroy(&strengths);
	igraph_vector_destroy(&weights);
	igraph_destroy(&graph);
}

int main(void)
{
	test_baseline_graph();
	test_key_route_divergence_graph();
	test_multiple_tolerance_graph();

	if (g_failures != 0) {
		fprintf(stderr, "main_path_search_test: %d failures\n", g_failures);
		return 1;
	}
	printf("main_path_search_test: all checks passed\n");
	return 0;
}
