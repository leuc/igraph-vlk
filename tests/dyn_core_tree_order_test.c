/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Correctness tests for graph/dyn_core_tree_order, the heuristic barycenter
 * rank maintainer paired with graph/dyn_core_tree. Mirrors dyn_core_tree_test.c's
 * shape: build a graph, advance the tree, then advance the order maintainer
 * on top, checking that ranks exist and that the barycenter pull actually
 * groups connected same-tree-node/parent-tree-node vertices closer together
 * than unrelated ones.
 */

#include "graph/dyn_core_tree.h"
#include "graph/dyn_core_tree_order.h"
#include "test_utilities.h"

#include <stdlib.h>

static int test_empty_and_singleton(void)
{
	igraph_t g;

	IGRAPH_ASSERT(igraph_empty(&g, 0, IGRAPH_UNDIRECTED) == IGRAPH_SUCCESS);
	{
		DynCoreTree *ct = dyn_core_tree_init(&g);
		IGRAPH_ASSERT(ct != NULL);
		DynCoreTreeOrder *dto = dyn_core_tree_order_init(&g, ct);
		IGRAPH_ASSERT(dto != NULL);
		dyn_core_tree_order_destroy(dto);
		dyn_core_tree_destroy(ct);
	}
	igraph_destroy(&g);

	IGRAPH_ASSERT(igraph_empty(&g, 3, IGRAPH_UNDIRECTED) == IGRAPH_SUCCESS);
	{
		DynCoreTree *ct = dyn_core_tree_init(&g);
		IGRAPH_ASSERT(ct != NULL);
		DynCoreTreeOrder *dto = dyn_core_tree_order_init(&g, ct);
		IGRAPH_ASSERT(dto != NULL);
		// Three disconnected/isolated vertices: each falls back to the
		// append-to-end counter, so all three ranks must be distinct.
		igraph_real_t r0 = dyn_core_tree_order_rank(dto, 0);
		igraph_real_t r1 = dyn_core_tree_order_rank(dto, 1);
		igraph_real_t r2 = dyn_core_tree_order_rank(dto, 2);
		IGRAPH_ASSERT(r0 != r1 && r1 != r2 && r0 != r2);
		dyn_core_tree_order_destroy(dto);
		dyn_core_tree_destroy(ct);
	}
	igraph_destroy(&g);
	return 0;
}

// Out-of-range / NULL handling: must not crash, must return the documented
// 0.0 sentinel.
static int test_out_of_range(void)
{
	igraph_t g;
	IGRAPH_ASSERT(igraph_empty(&g, 2, IGRAPH_UNDIRECTED) == IGRAPH_SUCCESS);
	DynCoreTree *ct = dyn_core_tree_init(&g);
	IGRAPH_ASSERT(ct != NULL);
	DynCoreTreeOrder *dto = dyn_core_tree_order_init(&g, ct);
	IGRAPH_ASSERT(dto != NULL);

	IGRAPH_ASSERT(dyn_core_tree_order_rank(dto, -1) == 0.0);
	IGRAPH_ASSERT(dyn_core_tree_order_rank(dto, 999) == 0.0);
	IGRAPH_ASSERT(dyn_core_tree_order_rank(NULL, 0) == 0.0);

	dyn_core_tree_order_destroy(dto);
	dyn_core_tree_destroy(ct);
	igraph_destroy(&g);
	return 0;
}

// Barycenter pull, exact case: A starts isolated (root). B arrives connected
// only to A; both end up coreness 1, sharing one tree node (a single edge
// between two previously-isolated vertices is exactly a 1-core). B's ONLY
// same-tree-node neighbor at rank-assignment time is A, so the barycenter
// (an average of one value) must equal A's rank exactly — not just "some
// finite number", a precise, hand-verifiable prediction. Chaining a third
// vertex C (connected only to B) checks the same exact-match property one
// hop further, confirming the mechanism isn't a one-shot fluke.
static int test_intra_pull_matches_sole_neighbor(void)
{
	igraph_t g;
	IGRAPH_ASSERT(igraph_empty(&g, 1, IGRAPH_UNDIRECTED) == IGRAPH_SUCCESS); // vertex 0 = A, isolated

	DynCoreTree *ct = dyn_core_tree_init(&g);
	IGRAPH_ASSERT(ct != NULL);
	DynCoreTreeOrder *dto = dyn_core_tree_order_init(&g, ct);
	IGRAPH_ASSERT(dto != NULL);
	igraph_real_t r_a = dyn_core_tree_order_rank(dto, 0);

	// Add B (vertex 1), connected only to A.
	IGRAPH_ASSERT(igraph_add_vertices(&g, 1, NULL) == IGRAPH_SUCCESS);
	igraph_vector_int_t batch;
	IGRAPH_ASSERT(igraph_vector_int_init(&batch, 0) == IGRAPH_SUCCESS);
	IGRAPH_ASSERT(igraph_vector_int_push_back(&batch, 0) == IGRAPH_SUCCESS);
	IGRAPH_ASSERT(igraph_vector_int_push_back(&batch, 1) == IGRAPH_SUCCESS);
	IGRAPH_ASSERT(igraph_add_edges(&g, &batch, NULL) == IGRAPH_SUCCESS);
	IGRAPH_ASSERT(dyn_core_tree_on_edges(ct, &g, &batch, NULL));
	IGRAPH_ASSERT(dyn_core_tree_order_on_update(dto, &g, ct));

	// A and B must now share one tree node (both coreness 1, connected).
	IGRAPH_ASSERT(dyn_core_tree_node_of(ct, 0) == dyn_core_tree_node_of(ct, 1));
	igraph_real_t r_b = dyn_core_tree_order_rank(dto, 1);
	IGRAPH_ASSERT(r_b == r_a);

	// Add C (vertex 2), connected only to B.
	IGRAPH_ASSERT(igraph_add_vertices(&g, 1, NULL) == IGRAPH_SUCCESS);
	igraph_vector_int_clear(&batch);
	IGRAPH_ASSERT(igraph_vector_int_push_back(&batch, 1) == IGRAPH_SUCCESS);
	IGRAPH_ASSERT(igraph_vector_int_push_back(&batch, 2) == IGRAPH_SUCCESS);
	IGRAPH_ASSERT(igraph_add_edges(&g, &batch, NULL) == IGRAPH_SUCCESS);
	IGRAPH_ASSERT(dyn_core_tree_on_edges(ct, &g, &batch, NULL));
	IGRAPH_ASSERT(dyn_core_tree_order_on_update(dto, &g, ct));

	IGRAPH_ASSERT(dyn_core_tree_node_of(ct, 1) == dyn_core_tree_node_of(ct, 2));
	igraph_real_t r_c = dyn_core_tree_order_rank(dto, 2);
	IGRAPH_ASSERT(r_c == r_b);

	igraph_vector_int_destroy(&batch);
	dyn_core_tree_order_destroy(dto);
	dyn_core_tree_destroy(ct);
	igraph_destroy(&g);
	return 0;
}

int main(void)
{
	RUN_TEST(test_empty_and_singleton);
	RUN_TEST(test_out_of_range);
	RUN_TEST(test_intra_pull_matches_sole_neighbor);

	printf("all tests passed\n");
	return EXIT_SUCCESS;
}
