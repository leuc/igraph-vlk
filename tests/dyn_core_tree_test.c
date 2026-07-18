/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Correctness tests for graph/dyn_core_tree, mirroring dyn_kcore_test.c's
 * shape: run_case() streams edges into an igraph_t exactly the way
 * graph/stream.c does (vertices added first, then one igraph_add_edges call
 * per batch, then dyn_core_tree_on_edges), validating the maintained
 * hierarchy after every batch via validate_core_tree.h's oracle and
 * structural checks. test_triangle_shape additionally hand-verifies the
 * exact tree shape through a small worked example, cross-checked against
 * dyn_kcore_test.c's own already-validated test_changed_and_max case.
 *
 * No benchmarking: timing/throughput belongs in a separate harness.
 */

#include "graph/dyn_core_tree.h"
#include "test_utilities.h"
#include "validate_core_tree.h"

#include <stdlib.h>

// Streams `edges` (flat u,v pairs) into g/ct in batches of `batch_edges`
// edges, validating after every batch.
static int run_case(const igraph_integer_t *edges, size_t n_edges, igraph_integer_t n_vertices, int batch_edges)
{
	igraph_t g;
	IGRAPH_ASSERT(igraph_empty(&g, 0, IGRAPH_UNDIRECTED) == IGRAPH_SUCCESS);
	IGRAPH_ASSERT(igraph_add_vertices(&g, n_vertices, NULL) == IGRAPH_SUCCESS);
	DynCoreTree *ct = dyn_core_tree_init(&g);
	IGRAPH_ASSERT(ct != NULL);
	validate_core_tree(ct, &g);

	igraph_vector_int_t batch;
	IGRAPH_ASSERT(igraph_vector_int_init(&batch, 0) == IGRAPH_SUCCESS);

	size_t n = n_edges / 2;
	for (size_t i = 0; i < n; i++) {
		igraph_integer_t u = edges[2 * i];
		igraph_integer_t v = edges[2 * i + 1];
		IGRAPH_ASSERT(igraph_vector_int_push_back(&batch, u) == IGRAPH_SUCCESS);
		IGRAPH_ASSERT(igraph_vector_int_push_back(&batch, v) == IGRAPH_SUCCESS);

		if (igraph_vector_int_size(&batch) / 2 >= (igraph_integer_t)batch_edges || i + 1 == n) {
			IGRAPH_ASSERT(igraph_add_edges(&g, &batch, NULL) == IGRAPH_SUCCESS);
			IGRAPH_ASSERT(dyn_core_tree_on_edges(ct, &g, &batch, NULL));
			validate_core_tree(ct, &g);
			igraph_vector_int_clear(&batch);
		}
	}

	igraph_vector_int_destroy(&batch);
	dyn_core_tree_destroy(ct);
	igraph_destroy(&g);
	return 0;
}

static int test_empty_and_singleton(void)
{
	igraph_t g;

	IGRAPH_ASSERT(igraph_empty(&g, 0, IGRAPH_UNDIRECTED) == IGRAPH_SUCCESS);
	{
		DynCoreTree *ct = dyn_core_tree_init(&g);
		IGRAPH_ASSERT(ct != NULL);
		validate_core_tree(ct, &g);
		IGRAPH_ASSERT(dyn_core_tree_node_count(ct) == 1); // just the root
		dyn_core_tree_destroy(ct);
	}
	igraph_destroy(&g);

	IGRAPH_ASSERT(igraph_empty(&g, 3, IGRAPH_UNDIRECTED) == IGRAPH_SUCCESS);
	{
		DynCoreTree *ct = dyn_core_tree_init(&g);
		IGRAPH_ASSERT(ct != NULL);
		validate_core_tree(ct, &g);
		// All three isolated vertices are direct members of the root.
		for (igraph_integer_t v = 0; v < 3; v++) {
			IGRAPH_ASSERT(dyn_core_tree_node_of(ct, v) == DYN_CORE_TREE_ROOT);
			IGRAPH_ASSERT(dyn_core_tree_level(ct, DYN_CORE_TREE_ROOT) == 0);
		}
		IGRAPH_ASSERT(dyn_core_tree_member_count(ct, DYN_CORE_TREE_ROOT) == 3);
		dyn_core_tree_destroy(ct);
	}
	igraph_destroy(&g);
	return 0;
}

// Hand-traced worked example, cross-checked against dyn_kcore_test.c's own
// already-passing test_changed_and_max (path 0-1, 1-2 each at coreness 1,
// closing the triangle lifts all three to coreness 2). The expected shape:
// after (0,1) and (1,2), all of {0,1,2} share ONE level-1 node (NOT two
// separate ones — the NC/merge step must fuse vertex 2's fresh node into the
// existing {0,1} node once edge (1,2) connects them into the same 1-core
// component); after (2,0) closes the triangle, that level-1 node collapses
// entirely (all members lift to level 2) and gets removed, so the final
// level-2 node sits as a DIRECT child of the root.
static int test_triangle_shape(void)
{
	igraph_t g;
	IGRAPH_ASSERT(igraph_empty(&g, 0, IGRAPH_UNDIRECTED) == IGRAPH_SUCCESS);
	IGRAPH_ASSERT(igraph_add_vertices(&g, 3, NULL) == IGRAPH_SUCCESS);
	DynCoreTree *ct = dyn_core_tree_init(&g);
	IGRAPH_ASSERT(ct != NULL);

	igraph_vector_int_t edge;
	IGRAPH_ASSERT(igraph_vector_int_init(&edge, 2) == IGRAPH_SUCCESS);

	// Edge (0,1): both isolated -> both lift to coreness 1, one shared node.
	VECTOR(edge)[0] = 0;
	VECTOR(edge)[1] = 1;
	IGRAPH_ASSERT(igraph_add_edges(&g, &edge, NULL) == IGRAPH_SUCCESS);
	IGRAPH_ASSERT(dyn_core_tree_on_edges(ct, &g, &edge, NULL));
	validate_core_tree(ct, &g);
	int node01 = dyn_core_tree_node_of(ct, 0);
	IGRAPH_ASSERT(node01 == dyn_core_tree_node_of(ct, 1));
	IGRAPH_ASSERT(dyn_core_tree_level(ct, node01) == 1);
	IGRAPH_ASSERT(dyn_core_tree_parent(ct, node01) == DYN_CORE_TREE_ROOT);

	// Edge (1,2): vertex 2 lifts to coreness 1 and must be fused into the
	// SAME node as {0,1} (they're now one connected 1-core component), not a
	// second, separate level-1 node.
	VECTOR(edge)[0] = 1;
	VECTOR(edge)[1] = 2;
	IGRAPH_ASSERT(igraph_add_edges(&g, &edge, NULL) == IGRAPH_SUCCESS);
	IGRAPH_ASSERT(dyn_core_tree_on_edges(ct, &g, &edge, NULL));
	validate_core_tree(ct, &g);
	int node012 = dyn_core_tree_node_of(ct, 0);
	IGRAPH_ASSERT(node012 == dyn_core_tree_node_of(ct, 1));
	IGRAPH_ASSERT(node012 == dyn_core_tree_node_of(ct, 2));
	IGRAPH_ASSERT(dyn_core_tree_level(ct, node012) == 1);
	IGRAPH_ASSERT(dyn_core_tree_parent(ct, node012) == DYN_CORE_TREE_ROOT);
	IGRAPH_ASSERT(dyn_core_tree_member_count(ct, node012) == 3);

	// Edge (2,0): closes the triangle, lifting all three to coreness 2. The
	// (now-empty) level-1 node is removed; the level-2 node becomes a direct
	// child of the root.
	VECTOR(edge)[0] = 2;
	VECTOR(edge)[1] = 0;
	IGRAPH_ASSERT(igraph_add_edges(&g, &edge, NULL) == IGRAPH_SUCCESS);
	IGRAPH_ASSERT(dyn_core_tree_on_edges(ct, &g, &edge, NULL));
	validate_core_tree(ct, &g);
	int final_node = dyn_core_tree_node_of(ct, 0);
	IGRAPH_ASSERT(final_node == dyn_core_tree_node_of(ct, 1));
	IGRAPH_ASSERT(final_node == dyn_core_tree_node_of(ct, 2));
	IGRAPH_ASSERT(dyn_core_tree_level(ct, final_node) == 2);
	IGRAPH_ASSERT(dyn_core_tree_parent(ct, final_node) == DYN_CORE_TREE_ROOT);
	IGRAPH_ASSERT(dyn_core_tree_member_count(ct, final_node) == 3);
	IGRAPH_ASSERT(dyn_core_tree_first_child(ct, DYN_CORE_TREE_ROOT) == final_node);
	IGRAPH_ASSERT(dyn_core_tree_next_sibling(ct, final_node) == -1); // only child

	igraph_vector_int_destroy(&edge);
	dyn_core_tree_destroy(ct);
	igraph_destroy(&g);
	return 0;
}

// The disjoint-components case: two separate triangles sharing no vertices
// must end up as two separate level-2 nodes, both direct children of the
// root (never merged with each other).
static int test_disjoint_components(void)
{
	static const igraph_integer_t edges[] = {
		0, 1, 1, 2, 2, 0, 3, 4, 4, 5, 5, 3,
	};
	igraph_t g;
	IGRAPH_ASSERT(igraph_empty(&g, 0, IGRAPH_UNDIRECTED) == IGRAPH_SUCCESS);
	IGRAPH_ASSERT(igraph_add_vertices(&g, 6, NULL) == IGRAPH_SUCCESS);
	DynCoreTree *ct = dyn_core_tree_init(&g);
	IGRAPH_ASSERT(ct != NULL);

	igraph_vector_int_t batch;
	IGRAPH_ASSERT(igraph_vector_int_init(&batch, 0) == IGRAPH_SUCCESS);
	for (size_t i = 0; i < sizeof(edges) / sizeof(edges[0]); i++)
		IGRAPH_ASSERT(igraph_vector_int_push_back(&batch, edges[i]) == IGRAPH_SUCCESS);
	IGRAPH_ASSERT(igraph_add_edges(&g, &batch, NULL) == IGRAPH_SUCCESS);
	IGRAPH_ASSERT(dyn_core_tree_on_edges(ct, &g, &batch, NULL));
	validate_core_tree(ct, &g);

	int nodeA = dyn_core_tree_node_of(ct, 0);
	int nodeB = dyn_core_tree_node_of(ct, 3);
	IGRAPH_ASSERT(nodeA != nodeB);
	IGRAPH_ASSERT(dyn_core_tree_level(ct, nodeA) == 2);
	IGRAPH_ASSERT(dyn_core_tree_level(ct, nodeB) == 2);
	IGRAPH_ASSERT(dyn_core_tree_parent(ct, nodeA) == DYN_CORE_TREE_ROOT);
	IGRAPH_ASSERT(dyn_core_tree_parent(ct, nodeB) == DYN_CORE_TREE_ROOT);
	IGRAPH_ASSERT(dyn_core_tree_member_count(ct, nodeA) == 3);
	IGRAPH_ASSERT(dyn_core_tree_member_count(ct, nodeB) == 3);

	igraph_vector_int_destroy(&batch);
	dyn_core_tree_destroy(ct);
	igraph_destroy(&g);
	return 0;
}

static int test_touched_levels_nonempty(void)
{
	igraph_t g;
	IGRAPH_ASSERT(igraph_empty(&g, 0, IGRAPH_UNDIRECTED) == IGRAPH_SUCCESS);
	IGRAPH_ASSERT(igraph_add_vertices(&g, 3, NULL) == IGRAPH_SUCCESS);
	DynCoreTree *ct = dyn_core_tree_init(&g);
	IGRAPH_ASSERT(ct != NULL);

	igraph_vector_int_t edge, touched_levels;
	IGRAPH_ASSERT(igraph_vector_int_init(&edge, 2) == IGRAPH_SUCCESS);
	IGRAPH_ASSERT(igraph_vector_int_init(&touched_levels, 0) == IGRAPH_SUCCESS);

	// Two fresh, isolated vertices both lift to coreness 1: the only tree
	// event is "create a level-1 node", so touched_levels must be exactly [1].
	VECTOR(edge)[0] = 0;
	VECTOR(edge)[1] = 1;
	IGRAPH_ASSERT(igraph_add_edges(&g, &edge, NULL) == IGRAPH_SUCCESS);
	IGRAPH_ASSERT(dyn_core_tree_on_edges(ct, &g, &edge, &touched_levels));
	IGRAPH_ASSERT(igraph_vector_int_size(&touched_levels) == 1);
	IGRAPH_ASSERT(VECTOR(touched_levels)[0] == 1);

	igraph_vector_int_clear(&touched_levels);
	VECTOR(edge)[0] = 5;
	VECTOR(edge)[1] = 6;
	IGRAPH_ASSERT(igraph_add_vertices(&g, 4, NULL) == IGRAPH_SUCCESS); // vcount 3 -> 7, so vertices 5,6 exist
	IGRAPH_ASSERT(igraph_add_edges(&g, &edge, NULL) == IGRAPH_SUCCESS);
	IGRAPH_ASSERT(dyn_core_tree_on_edges(ct, &g, &edge, &touched_levels));
	IGRAPH_ASSERT(igraph_vector_int_size(&touched_levels) == 1); // a fresh, unrelated level-1 node
	IGRAPH_ASSERT(VECTOR(touched_levels)[0] == 1);

	validate_core_tree(ct, &g);

	igraph_vector_int_destroy(&touched_levels);
	igraph_vector_int_destroy(&edge);
	dyn_core_tree_destroy(ct);
	igraph_destroy(&g);
	return 0;
}

// Regression test for the id-reuse hazard touched_levels was introduced to
// fix: within a single dyn_core_tree_on_edges call, a node freed by one edge
// (e.g. a merged-away or emptied node) can have its slot immediately reused
// by alloc_node for a LATER edge in the same batch. Reporting raw node ids
// would let a caller who inspects them after the call read the wrong,
// reincarnated node's level for anything that got freed mid-batch. This
// drives the exact triangle-closing sequence from test_triangle_shape — which
// is known (hand-traced there) to free and reuse a node slot mid-batch — but
// as ONE batched dyn_core_tree_on_edges call instead of three separate calls,
// so the reuse actually happens WITHIN the touched_levels collection window,
// and asserts every reported level is a real, valid level for this graph
// (never a stale/reincarnated value) with both intermediate (1) and final (2)
// levels represented.
static int test_touched_levels_survive_same_batch_reuse(void)
{
	igraph_t g;
	IGRAPH_ASSERT(igraph_empty(&g, 0, IGRAPH_UNDIRECTED) == IGRAPH_SUCCESS);
	IGRAPH_ASSERT(igraph_add_vertices(&g, 3, NULL) == IGRAPH_SUCCESS);
	DynCoreTree *ct = dyn_core_tree_init(&g);
	IGRAPH_ASSERT(ct != NULL);

	static const igraph_integer_t edges[] = {0, 1, 1, 2, 2, 0};
	igraph_vector_int_t batch, touched_levels;
	IGRAPH_ASSERT(igraph_vector_int_init(&batch, 0) == IGRAPH_SUCCESS);
	IGRAPH_ASSERT(igraph_vector_int_init(&touched_levels, 0) == IGRAPH_SUCCESS);
	for (size_t i = 0; i < sizeof(edges) / sizeof(edges[0]); i++)
		IGRAPH_ASSERT(igraph_vector_int_push_back(&batch, edges[i]) == IGRAPH_SUCCESS);
	IGRAPH_ASSERT(igraph_add_edges(&g, &batch, NULL) == IGRAPH_SUCCESS);
	IGRAPH_ASSERT(dyn_core_tree_on_edges(ct, &g, &batch, &touched_levels));

	bool saw_level_1 = false, saw_level_2 = false;
	igraph_integer_t n = igraph_vector_int_size(&touched_levels);
	IGRAPH_ASSERT(n > 0);
	for (igraph_integer_t i = 0; i < n; i++) {
		int lvl = (int)VECTOR(touched_levels)[i];
		IGRAPH_ASSERT(lvl == 1 || lvl == 2); // never a stale/reincarnated value
		if (lvl == 1)
			saw_level_1 = true;
		if (lvl == 2)
			saw_level_2 = true;
	}
	IGRAPH_ASSERT(saw_level_1); // the transient level-1 grouping
	IGRAPH_ASSERT(saw_level_2); // the final triangle-closing lift

	// The final tree shape must still match test_triangle_shape's hand-traced
	// result regardless of how many nodes were created/freed/reused getting
	// there: all three vertices in one level-2 node, direct child of root.
	int final_node = dyn_core_tree_node_of(ct, 0);
	IGRAPH_ASSERT(final_node == dyn_core_tree_node_of(ct, 1));
	IGRAPH_ASSERT(final_node == dyn_core_tree_node_of(ct, 2));
	IGRAPH_ASSERT(dyn_core_tree_level(ct, final_node) == 2);
	IGRAPH_ASSERT(dyn_core_tree_parent(ct, final_node) == DYN_CORE_TREE_ROOT);
	validate_core_tree(ct, &g);

	igraph_vector_int_destroy(&touched_levels);
	igraph_vector_int_destroy(&batch);
	dyn_core_tree_destroy(ct);
	igraph_destroy(&g);
	return 0;
}

static int test_random_multigraph(void)
{
	// Same fixed edge table as dyn_kcore_test.c's test_random_multigraph
	// (duplicates and self-loops included), validated purely against the
	// oracle + structural invariants here rather than a hardcoded shape.
	static const igraph_integer_t edges[] = {
		0, 1, 1, 2, 2, 0, 0, 1, 3, 3, 4, 5, 5, 4, 4, 6, 6, 7, 7, 4, 0, 4, 2, 7, 8, 8, 1, 9, 9, 1, 3, 3, 5, 5,
	};
	return run_case(edges, sizeof(edges) / sizeof(edges[0]), 10, 3);
}

// Validate against the same graph families dyn_kcore_test.c's
// test_coreness_oracle uses, for consistency with that suite's coverage.
static int test_oracle_family(void)
{
	igraph_t g;

	igraph_rng_seed(igraph_rng_default(), 137);

	igraph_full(&g, 5, IGRAPH_UNDIRECTED, IGRAPH_NO_LOOPS);
	{
		DynCoreTree *ct = dyn_core_tree_init(&g);
		IGRAPH_ASSERT(ct != NULL);
		validate_core_tree(ct, &g);
		dyn_core_tree_destroy(ct);
	}
	igraph_destroy(&g);

	igraph_full(&g, 5, IGRAPH_UNDIRECTED, IGRAPH_LOOPS);
	{
		DynCoreTree *ct = dyn_core_tree_init(&g);
		IGRAPH_ASSERT(ct != NULL);
		validate_core_tree(ct, &g);
		dyn_core_tree_destroy(ct);
	}
	igraph_destroy(&g);

	igraph_full(&g, 5, IGRAPH_DIRECTED, IGRAPH_NO_LOOPS);
	{
		DynCoreTree *ct = dyn_core_tree_init(&g);
		IGRAPH_ASSERT(ct != NULL);
		validate_core_tree(ct, &g);
		dyn_core_tree_destroy(ct);
	}
	igraph_destroy(&g);

	igraph_famous(&g, "zachary");
	{
		DynCoreTree *ct = dyn_core_tree_init(&g);
		IGRAPH_ASSERT(ct != NULL);
		validate_core_tree(ct, &g);
		dyn_core_tree_destroy(ct);
	}
	igraph_destroy(&g);

	// Zachary with random loops and multi-edges added (exercises the
	// multi-layer/skip-level bucket path via self-loops), 20 repetitions.
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
			DynCoreTree *ct = dyn_core_tree_init(&g);
			IGRAPH_ASSERT(ct != NULL);
			validate_core_tree(ct, &g);
			dyn_core_tree_destroy(ct);
		}
		igraph_destroy(&g);
	}

	igraph_grg_game(&g, 100, 0.2, /* torus = */ 0, /* x = */ 0, /* y = */ 0);
	{
		DynCoreTree *ct = dyn_core_tree_init(&g);
		IGRAPH_ASSERT(ct != NULL);
		validate_core_tree(ct, &g);
		dyn_core_tree_destroy(ct);
	}
	igraph_destroy(&g);

	return 0;
}

int main(void)
{
	RUN_TEST(test_empty_and_singleton);
	RUN_TEST(test_triangle_shape);
	RUN_TEST(test_disjoint_components);
	RUN_TEST(test_touched_levels_nonempty);
	RUN_TEST(test_touched_levels_survive_same_batch_reuse);
	RUN_TEST(test_random_multigraph);
	RUN_TEST(test_oracle_family);

	printf("all tests passed\n");
	return EXIT_SUCCESS;
}
