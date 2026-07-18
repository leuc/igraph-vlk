/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Structural oracle for a DynCoreTree, mirroring validate_coreness.h's role
 * for plain coreness: checks the maintained hierarchy against both the
 * igraph_coreness() oracle and the tree's own well-formedness invariants
 * (Def. 6 of Lin et al., "Hierarchical Core Maintenance on Large Dynamic
 * Graphs", PVLDB 14(5), 2021). Aborts (via IGRAPH_ASSERT) on the first
 * violation.
 */

#ifndef VALIDATE_CORE_TREE_H
#define VALIDATE_CORE_TREE_H

#include "graph/dyn_core_tree.h"

#include <igraph.h>
#include <stdlib.h>

static void validate_core_tree(const DynCoreTree *ct, const igraph_t *g)
{
	igraph_integer_t vcount = igraph_vcount(g);

	// Oracle: the layer a vertex's node sits at must equal its actual
	// coreness — this is the maintained hierarchy's defining property.
	igraph_vector_int_t coreness;
	IGRAPH_ASSERT(igraph_vector_int_init(&coreness, vcount) == IGRAPH_SUCCESS);
	IGRAPH_ASSERT(igraph_coreness(g, &coreness, IGRAPH_ALL) == IGRAPH_SUCCESS);
	for (igraph_integer_t v = 0; v < vcount; v++) {
		int node = dyn_core_tree_node_of(ct, v);
		IGRAPH_ASSERT(node >= 0);
		IGRAPH_ASSERT(dyn_core_tree_level(ct, node) == (int)VECTOR(coreness)[v]);
	}
	igraph_vector_int_destroy(&coreness);

	if (vcount == 0)
		return;

	// Structural well-formedness: level strictly increases from parent to
	// child (root excepted), no cycles, member bookkeeping is exact, and
	// every vertex is claimed by exactly one node.
	int n = dyn_core_tree_node_count(ct);
	igraph_integer_t *claimed_by = malloc((size_t)vcount * sizeof(igraph_integer_t));
	IGRAPH_ASSERT(claimed_by != NULL);
	for (igraph_integer_t v = 0; v < vcount; v++)
		claimed_by[v] = -1;

	igraph_integer_t total_members = 0;
	for (int node = 0; node < n; node++) {
		int level = dyn_core_tree_level(ct, node);
		if (level < 0) // dead (freed/reused-pending) node slot
			continue;

		int parent = dyn_core_tree_parent(ct, node);
		if (node == DYN_CORE_TREE_ROOT) {
			IGRAPH_ASSERT(parent == -1);
			IGRAPH_ASSERT(level == 0);
		} else {
			IGRAPH_ASSERT(parent >= 0);
			IGRAPH_ASSERT(dyn_core_tree_level(ct, parent) < level);
		}

		int walk = node, steps = 0;
		while (walk != DYN_CORE_TREE_ROOT) {
			walk = dyn_core_tree_parent(ct, walk);
			IGRAPH_ASSERT(walk >= 0);
			steps++;
			IGRAPH_ASSERT(steps <= n); // would only fail on a cycle
		}

		igraph_integer_t counted = 0;
		for (igraph_integer_t v = dyn_core_tree_first_member(ct, node); v != -1; v = dyn_core_tree_next_member(ct, v)) {
			IGRAPH_ASSERT(dyn_core_tree_node_of(ct, v) == node);
			IGRAPH_ASSERT(claimed_by[v] == -1);
			claimed_by[v] = node;
			counted++;
		}
		IGRAPH_ASSERT(counted == dyn_core_tree_member_count(ct, node));
		total_members += counted;
	}
	IGRAPH_ASSERT(total_members == vcount);
	for (igraph_integer_t v = 0; v < vcount; v++)
		IGRAPH_ASSERT(claimed_by[v] == dyn_core_tree_node_of(ct, v));
	free(claimed_by);
}

#endif // VALIDATE_CORE_TREE_H
