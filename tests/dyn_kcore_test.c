/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Data-driven correctness tests for graph/dyn_k-core.
 *
 * Each case is a compact edge-list table plus the expected final coreness
 * per vertex; run_case() streams the edges into an igraph_t exactly the way
 * graph/stream.c does (vertices added first, then one igraph_add_edges call
 * per batch, then dyn_kcore_on_edges) and asserts the maintained coreness
 * matches both the expected table and the igraph_coreness oracle.
 *
 * No benchmarking: timing/throughput belongs in a separate harness.
 */

#include "graph/dyn_k-core.h"
#include "test_utilities.h"
#include "validate_coreness.h"

#include <stdlib.h>

// Unit-test-only oracle: recompute igraph_coreness(g) from scratch and diff
// against the maintained values (via the public dyn_kcore_values() view).
// This is intentionally NOT part of the runtime library: it performs a full
// O(V+E) coreness recompute that would defeat the dynamic streaming
// maintenance if called outside of tests.
static int dyn_kcore_verify(const DynKCore *kc, const igraph_t *g)
{
	if (!kc)
		return 0;

	igraph_integer_t n = igraph_vcount(g);
	if (n == 0)
		return 1;

	const int *maintained = dyn_kcore_values(kc);
	if (!maintained)
		return 0;

	igraph_vector_int_t cores;
	if (igraph_vector_int_init(&cores, n) != IGRAPH_SUCCESS)
		return 0;
	if (igraph_coreness(g, &cores, IGRAPH_ALL) != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(&cores);
		return 0;
	}

	igraph_integer_t mismatches = 0;
	for (igraph_integer_t i = 0; i < n; i++) {
		if (maintained[i] != (int)VECTOR(cores)[i]) {
			if (mismatches < 10)
				fprintf(stderr, "dyn_kcore_verify: vertex %lld maintained %d, actual %lld\n", (long long)i, maintained[i], (long long)VECTOR(cores)[i]);
			mismatches++;
		}
	}
	igraph_vector_int_destroy(&cores);

	if (mismatches > 0) {
		fprintf(stderr, "dyn_kcore_verify: %lld mismatch(es) of %lld vertices\n", (long long)mismatches, (long long)n);
		return 0;
	}
	return 1;
}

// Stream `edges` (flat u,v pairs) into g/kc in batches of `batch_edges`
// edges, mirroring graph/stream.c, then assert final coreness.
static int run_case(const igraph_integer_t *edges, size_t n_edges, const int *expected, igraph_integer_t n_vertices, int batch_edges)
{
	igraph_t g;
	IGRAPH_ASSERT(igraph_empty(&g, 0, IGRAPH_UNDIRECTED) == IGRAPH_SUCCESS);
	DynKCore *kc = dyn_kcore_init(&g);
	IGRAPH_ASSERT(kc != NULL);

	igraph_vector_int_t batch;
	IGRAPH_ASSERT(igraph_vector_int_init(&batch, 0) == IGRAPH_SUCCESS);

	size_t n = n_edges / 2;
	for (size_t i = 0; i < n; i++) {
		igraph_integer_t u = edges[2 * i];
		igraph_integer_t v = edges[2 * i + 1];
		IGRAPH_ASSERT(igraph_vector_int_push_back(&batch, u) == IGRAPH_SUCCESS);
		IGRAPH_ASSERT(igraph_vector_int_push_back(&batch, v) == IGRAPH_SUCCESS);

		if (igraph_vector_int_size(&batch) / 2 >= (igraph_integer_t)batch_edges || i + 1 == n) {
			igraph_integer_t maxid = -1;
			for (igraph_integer_t j = 0; j < igraph_vector_int_size(&batch); j++)
				if (VECTOR(batch)[j] > maxid)
					maxid = VECTOR(batch)[j];
			if (maxid >= igraph_vcount(&g))
				IGRAPH_ASSERT(igraph_add_vertices(&g, maxid + 1 - igraph_vcount(&g), NULL) == IGRAPH_SUCCESS);
			IGRAPH_ASSERT(igraph_add_edges(&g, &batch, NULL) == IGRAPH_SUCCESS);
			IGRAPH_ASSERT(dyn_kcore_on_edges(kc, &g, &batch, NULL));
			IGRAPH_ASSERT(dyn_kcore_verify(kc, &g));
			igraph_vector_int_clear(&batch);
		}
	}

	for (igraph_integer_t v = 0; v < n_vertices; v++)
		IGRAPH_ASSERT(dyn_kcore_get(kc, v) == expected[v]);

	igraph_vector_int_destroy(&batch);
	dyn_kcore_destroy(kc);
	igraph_destroy(&g);
	return 0;
}

static int test_pair_triangle(void)
{
	static const igraph_integer_t edges[] = {0, 1, 1, 2, 2, 0};
	static const int expected[] = {2, 2, 2};
	return run_case(edges, sizeof(edges) / sizeof(edges[0]), expected, 3, 1);
}

static int test_single_batch_clique(void)
{
	// K5 in one batch of fresh vertices: exercises the future-edge filter.
	static const igraph_integer_t edges[] = {
		0, 1, 0, 2, 0, 3, 0, 4, 1, 2, 1, 3, 1, 4, 2, 3, 2, 4, 3, 4,
	};
	static const int expected[] = {4, 4, 4, 4, 4};
	return run_case(edges, sizeof(edges) / sizeof(edges[0]), expected, 5, 99);
}

static int test_self_loops(void)
{
	// Triangle 0-1-2-0, then a self-loop on vertex 0 (counts twice, no lift),
	// then a self-loop on a fresh isolated vertex 3 (coreness 2).
	static const igraph_integer_t edges[] = {0, 1, 1, 2, 2, 0, 0, 0, 3, 3};
	static const int expected[] = {2, 2, 2, 2};
	return run_case(edges, sizeof(edges) / sizeof(edges[0]), expected, 4, 1);
}

static int test_parallel_edges(void)
{
	// Triangle 0-1-2-0 plus a parallel 0-1 inside it: the parallel edge does
	// not raise coreness (both endpoints are already in the 2-core), so all 2.
	static const igraph_integer_t edges[] = {0, 1, 1, 2, 2, 0, 0, 1};
	static const int expected[] = {2, 2, 2};
	return run_case(edges, sizeof(edges) / sizeof(edges[0]), expected, 3, 1);
}

static int test_bootstrap(void)
{
	// Start from an existing graph, then stream more edges.
	igraph_t g;
	IGRAPH_ASSERT(igraph_small(&g, 0, IGRAPH_UNDIRECTED, 0, 1, 1, 2, 2, 0, 2, 3, 3, 4, -1) == IGRAPH_SUCCESS);
	DynKCore *kc = dyn_kcore_init(&g);
	IGRAPH_ASSERT(kc != NULL);
	IGRAPH_ASSERT(dyn_kcore_verify(kc, &g));

	static const igraph_integer_t edges[] = {3, 0, 4, 0};
	igraph_vector_int_t batch;
	IGRAPH_ASSERT(igraph_vector_int_init(&batch, 0) == IGRAPH_SUCCESS);
	for (size_t i = 0; i < sizeof(edges) / sizeof(edges[0]); i++)
		IGRAPH_ASSERT(igraph_vector_int_push_back(&batch, edges[i]) == IGRAPH_SUCCESS);
	IGRAPH_ASSERT(igraph_add_edges(&g, &batch, NULL) == IGRAPH_SUCCESS);
	IGRAPH_ASSERT(dyn_kcore_on_edges(kc, &g, &batch, NULL));
	IGRAPH_ASSERT(dyn_kcore_verify(kc, &g));
	IGRAPH_ASSERT(dyn_kcore_get(kc, 0) == 2);
	IGRAPH_ASSERT(dyn_kcore_get(kc, 4) == 2);

	igraph_vector_int_destroy(&batch);
	dyn_kcore_destroy(kc);
	igraph_destroy(&g);
	return 0;
}

static int test_changed_and_max(void)
{
	igraph_t g;
	IGRAPH_ASSERT(igraph_empty(&g, 0, IGRAPH_UNDIRECTED) == IGRAPH_SUCCESS);
	DynKCore *kc = dyn_kcore_init(&g);
	IGRAPH_ASSERT(kc != NULL);
	IGRAPH_ASSERT(dyn_kcore_max(kc) == 0);

	igraph_vector_int_t edges, changed;
	IGRAPH_ASSERT(igraph_vector_int_init(&edges, 0) == IGRAPH_SUCCESS);
	IGRAPH_ASSERT(igraph_vector_int_init(&changed, 0) == IGRAPH_SUCCESS);

	// Path 0-1, 1-2: edge 0-1 lifts both endpoints (2 changed); 1-2 lifts
	// only the fresh vertex 2 (1 changed). Path stays a 1-core.
	static const igraph_integer_t path[][2] = {{0, 1}, {1, 2}};
	static const igraph_integer_t path_expected_changed[] = {2, 1};
	for (size_t i = 0; i < sizeof(path) / sizeof(path[0]); i++) {
		igraph_vector_int_clear(&edges);
		IGRAPH_ASSERT(igraph_vector_int_push_back(&edges, path[i][0]) == IGRAPH_SUCCESS);
		IGRAPH_ASSERT(igraph_vector_int_push_back(&edges, path[i][1]) == IGRAPH_SUCCESS);
		igraph_integer_t maxid = path[i][0] > path[i][1] ? path[i][0] : path[i][1];
		if (maxid >= igraph_vcount(&g))
			IGRAPH_ASSERT(igraph_add_vertices(&g, maxid + 1 - igraph_vcount(&g), NULL) == IGRAPH_SUCCESS);
		IGRAPH_ASSERT(igraph_add_edges(&g, &edges, NULL) == IGRAPH_SUCCESS);
		igraph_vector_int_clear(&changed);
		IGRAPH_ASSERT(dyn_kcore_on_edges(kc, &g, &edges, &changed));
		IGRAPH_ASSERT(igraph_vector_int_size(&changed) == path_expected_changed[i]);
		IGRAPH_ASSERT(dyn_kcore_max(kc) == 1);
	}

	// Close the triangle 2-0: all three lift to coreness 2.
	igraph_vector_int_clear(&edges);
	IGRAPH_ASSERT(igraph_vector_int_push_back(&edges, 2) == IGRAPH_SUCCESS);
	IGRAPH_ASSERT(igraph_vector_int_push_back(&edges, 0) == IGRAPH_SUCCESS);
	IGRAPH_ASSERT(igraph_add_edges(&g, &edges, NULL) == IGRAPH_SUCCESS);
	igraph_vector_int_clear(&changed);
	IGRAPH_ASSERT(dyn_kcore_on_edges(kc, &g, &edges, &changed));
	IGRAPH_ASSERT(igraph_vector_int_size(&changed) == 3);
	IGRAPH_ASSERT(dyn_kcore_max(kc) == 2);
	IGRAPH_ASSERT(dyn_kcore_get(kc, 0) == 2 && dyn_kcore_get(kc, 1) == 2 && dyn_kcore_get(kc, 2) == 2);

	// Fresh disjoint pair: reported changed, max coreness unaffected.
	igraph_vector_int_clear(&edges);
	IGRAPH_ASSERT(igraph_vector_int_push_back(&edges, 3) == IGRAPH_SUCCESS);
	IGRAPH_ASSERT(igraph_vector_int_push_back(&edges, 4) == IGRAPH_SUCCESS);
	IGRAPH_ASSERT(igraph_add_vertices(&g, 5 - igraph_vcount(&g), NULL) == IGRAPH_SUCCESS);
	IGRAPH_ASSERT(igraph_add_edges(&g, &edges, NULL) == IGRAPH_SUCCESS);
	igraph_vector_int_clear(&changed);
	IGRAPH_ASSERT(dyn_kcore_on_edges(kc, &g, &edges, &changed));
	IGRAPH_ASSERT(igraph_vector_int_size(&changed) == 2);
	IGRAPH_ASSERT(dyn_kcore_max(kc) == 2);
	IGRAPH_ASSERT(dyn_kcore_get(kc, 3) == 1 && dyn_kcore_get(kc, 4) == 1);

	IGRAPH_ASSERT(dyn_kcore_verify(kc, &g));

	igraph_vector_int_destroy(&changed);
	igraph_vector_int_destroy(&edges);
	dyn_kcore_destroy(kc);
	igraph_destroy(&g);
	return 0;
}

static int test_random_multigraph(void)
{
	// Fixed (deterministic) edge table with duplicates and self-loops;
	// verified against the igraph_coreness oracle only (no timing).
	static const igraph_integer_t edges[] = {
		0, 1, 1, 2, 2, 0, 0, 1, 3, 3, 4, 5, 5, 4, 4, 6, 6, 7, 7, 4, 0, 4, 2, 7, 8, 8, 1, 9, 9, 1, 3, 3, 5, 5,
	};
	static const int expected[] = {2, 2, 2, 4, 2, 2, 2, 2, 2, 2};
	return run_case(edges, sizeof(edges) / sizeof(edges[0]), expected, 10, 3);
}

// Validate the maintained coreness against the EXACT same graphs and the
// EXACT same structural oracle (validate_coreness) that igraph's own
// tests/unit/coreness.c uses for igraph_coreness. The dyn_k-core maintainer is
// undirected-only (IGRAPH_ALL), so directed graphs are validated under
// IGRAPH_ALL too — matching the maintenance's documented semantics.
static int test_coreness_oracle(void)
{
	igraph_t g;

	igraph_rng_seed(igraph_rng_default(), 137);

	// Empty and singleton graph.
	igraph_empty(&g, 0, IGRAPH_UNDIRECTED);
	{
		DynKCore *kc = dyn_kcore_init(&g);
		IGRAPH_ASSERT(kc != NULL);
		igraph_vector_int_t c;
		igraph_vector_int_init(&c, 0);
		IGRAPH_ASSERT(igraph_coreness(&g, &c, IGRAPH_ALL) == IGRAPH_SUCCESS);
		IGRAPH_ASSERT(dyn_kcore_verify(kc, &g));
		validate_coreness(&g, &c, IGRAPH_ALL);
		igraph_vector_int_destroy(&c);
		dyn_kcore_destroy(kc);
	}
	igraph_destroy(&g);

	igraph_empty(&g, 1, IGRAPH_UNDIRECTED);
	{
		DynKCore *kc = dyn_kcore_init(&g);
		IGRAPH_ASSERT(kc != NULL);
		igraph_vector_int_t c;
		igraph_vector_int_init(&c, 0);
		IGRAPH_ASSERT(igraph_coreness(&g, &c, IGRAPH_ALL) == IGRAPH_SUCCESS);
		IGRAPH_ASSERT(dyn_kcore_verify(kc, &g));
		validate_coreness(&g, &c, IGRAPH_ALL);
		igraph_vector_int_destroy(&c);
		dyn_kcore_destroy(kc);
	}
	igraph_destroy(&g);

	// Simple full graph.
	igraph_full(&g, 5, IGRAPH_UNDIRECTED, IGRAPH_NO_LOOPS);
	{
		DynKCore *kc = dyn_kcore_init(&g);
		IGRAPH_ASSERT(kc != NULL);
		igraph_vector_int_t c;
		igraph_vector_int_init(&c, 0);
		IGRAPH_ASSERT(igraph_coreness(&g, &c, IGRAPH_ALL) == IGRAPH_SUCCESS);
		IGRAPH_ASSERT(dyn_kcore_verify(kc, &g));
		validate_coreness(&g, &c, IGRAPH_ALL);
		igraph_vector_int_destroy(&c);
		dyn_kcore_destroy(kc);
	}
	igraph_destroy(&g);

	// Full graph with loops.
	igraph_full(&g, 5, IGRAPH_UNDIRECTED, IGRAPH_LOOPS);
	{
		DynKCore *kc = dyn_kcore_init(&g);
		IGRAPH_ASSERT(kc != NULL);
		igraph_vector_int_t c;
		igraph_vector_int_init(&c, 0);
		IGRAPH_ASSERT(igraph_coreness(&g, &c, IGRAPH_ALL) == IGRAPH_SUCCESS);
		IGRAPH_ASSERT(dyn_kcore_verify(kc, &g));
		validate_coreness(&g, &c, IGRAPH_ALL);
		igraph_vector_int_destroy(&c);
		dyn_kcore_destroy(kc);
	}
	igraph_destroy(&g);

	// Full directed graph.
	igraph_full(&g, 5, IGRAPH_DIRECTED, IGRAPH_NO_LOOPS);
	{
		DynKCore *kc = dyn_kcore_init(&g);
		IGRAPH_ASSERT(kc != NULL);
		igraph_vector_int_t c;
		igraph_vector_int_init(&c, 0);
		IGRAPH_ASSERT(igraph_coreness(&g, &c, IGRAPH_ALL) == IGRAPH_SUCCESS);
		IGRAPH_ASSERT(dyn_kcore_verify(kc, &g));
		validate_coreness(&g, &c, IGRAPH_ALL);
		igraph_vector_int_destroy(&c);
		dyn_kcore_destroy(kc);
	}
	igraph_destroy(&g);

	// Full directed graph with loops.
	igraph_full(&g, 5, IGRAPH_DIRECTED, IGRAPH_LOOPS);
	{
		DynKCore *kc = dyn_kcore_init(&g);
		IGRAPH_ASSERT(kc != NULL);
		igraph_vector_int_t c;
		igraph_vector_int_init(&c, 0);
		IGRAPH_ASSERT(igraph_coreness(&g, &c, IGRAPH_ALL) == IGRAPH_SUCCESS);
		IGRAPH_ASSERT(dyn_kcore_verify(kc, &g));
		validate_coreness(&g, &c, IGRAPH_ALL);
		igraph_vector_int_destroy(&c);
		dyn_kcore_destroy(kc);
	}
	igraph_destroy(&g);

	// Zachary karate club.
	igraph_famous(&g, "zachary");
	{
		DynKCore *kc = dyn_kcore_init(&g);
		IGRAPH_ASSERT(kc != NULL);
		igraph_vector_int_t c;
		igraph_vector_int_init(&c, 0);
		IGRAPH_ASSERT(igraph_coreness(&g, &c, IGRAPH_ALL) == IGRAPH_SUCCESS);
		IGRAPH_ASSERT(dyn_kcore_verify(kc, &g));
		validate_coreness(&g, &c, IGRAPH_ALL);
		igraph_vector_int_destroy(&c);
		dyn_kcore_destroy(kc);
	}
	igraph_destroy(&g);

	// Zachary karate club, randomly directed edges (trimmed).
	igraph_famous(&g, "zachary");
	igraph_to_directed(&g, IGRAPH_TO_DIRECTED_MUTUAL);
	{
		igraph_vector_int_t to_remove;
		igraph_vector_int_init(&to_remove, 0);
		igraph_integer_t n = igraph_ecount(&g);
		for (igraph_integer_t i = 0; i < n; i++)
			if (igraph_rng_get_unif01(igraph_rng_default()) < 0.2)
				igraph_vector_int_push_back(&to_remove, i);
		igraph_delete_edges(&g, igraph_ess_vector(&to_remove));
		igraph_vector_int_destroy(&to_remove);
	}
	{
		DynKCore *kc = dyn_kcore_init(&g);
		IGRAPH_ASSERT(kc != NULL);
		igraph_vector_int_t c;
		igraph_vector_int_init(&c, 0);
		IGRAPH_ASSERT(igraph_coreness(&g, &c, IGRAPH_ALL) == IGRAPH_SUCCESS);
		IGRAPH_ASSERT(dyn_kcore_verify(kc, &g));
		validate_coreness(&g, &c, IGRAPH_ALL);
		igraph_vector_int_destroy(&c);
		dyn_kcore_destroy(kc);
	}
	igraph_destroy(&g);

	// Zachary karate club with random loops and multi-edges (20 repetitions,
	// matching igraph's coreness.c).
	for (int i = 0; i < 20; i++) {
		igraph_famous(&g, "zachary");
		igraph_integer_t nv = igraph_vcount(&g);
		igraph_vector_int_t extra;
		igraph_vector_int_init(&extra, 0);
		for (igraph_integer_t v = 0; v < nv; v++)
			if (igraph_rng_get_unif01(igraph_rng_default()) < 0.5) {
				igraph_vector_int_push_back(&extra, v);
				igraph_vector_int_push_back(&extra, v);
			}
		igraph_integer_t ne = igraph_ecount(&g);
		for (igraph_integer_t e = 0; e < ne; e++)
			if (igraph_rng_get_unif01(igraph_rng_default()) < 0.2) {
				igraph_integer_t from, to;
				igraph_edge(&g, e, &from, &to);
				igraph_vector_int_push_back(&extra, from);
				igraph_vector_int_push_back(&extra, to);
			}
		igraph_add_edges(&g, &extra, NULL);
		igraph_vector_int_destroy(&extra);
		{
			DynKCore *kc = dyn_kcore_init(&g);
			IGRAPH_ASSERT(kc != NULL);
			igraph_vector_int_t c;
			igraph_vector_int_init(&c, 0);
			IGRAPH_ASSERT(igraph_coreness(&g, &c, IGRAPH_ALL) == IGRAPH_SUCCESS);
			IGRAPH_ASSERT(dyn_kcore_verify(kc, &g));
			validate_coreness(&g, &c, IGRAPH_ALL);
			igraph_vector_int_destroy(&c);
			dyn_kcore_destroy(kc);
		}
		igraph_destroy(&g);
	}

	// Geometric random graph.
	igraph_grg_game(&g, 100, 0.2, /* torus = */ 0, /* x = */ 0, /* y = */ 0);
	{
		DynKCore *kc = dyn_kcore_init(&g);
		IGRAPH_ASSERT(kc != NULL);
		igraph_vector_int_t c;
		igraph_vector_int_init(&c, 0);
		IGRAPH_ASSERT(igraph_coreness(&g, &c, IGRAPH_ALL) == IGRAPH_SUCCESS);
		IGRAPH_ASSERT(dyn_kcore_verify(kc, &g));
		validate_coreness(&g, &c, IGRAPH_ALL);
		igraph_vector_int_destroy(&c);
		dyn_kcore_destroy(kc);
	}
	igraph_destroy(&g);

	return 0;
}

int main(void)
{
	RUN_TEST(test_pair_triangle);
	RUN_TEST(test_single_batch_clique);
	RUN_TEST(test_self_loops);
	RUN_TEST(test_parallel_edges);
	RUN_TEST(test_bootstrap);
	RUN_TEST(test_changed_and_max);
	RUN_TEST(test_random_multigraph);
	RUN_TEST(test_coreness_oracle);

	printf("all tests passed\n");
	return EXIT_SUCCESS;
}
