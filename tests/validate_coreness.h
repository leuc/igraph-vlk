/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Structural oracle for a coreness assignment, ported from igraph's own
 * tests/unit/coreness.c. For every k from the maximum down to 0, the induced
 * subgraph on vertices with coreness >= k must have minimum degree >= k (under
 * `mode`). This is the same check igraph uses to validate igraph_coreness
 * output, so it can be reused to validate any maintained/computed coreness
 * against the definition. Aborts (via IGRAPH_ASSERT) on the first vertex that
 * violates the definition.
 */

#ifndef VALIDATE_CORNESS_H
#define VALIDATE_CORNESS_H

#include <igraph.h>

static void validate_coreness(const igraph_t *graph, const igraph_vector_int_t *coreness, igraph_neimode_t mode)
{
	igraph_integer_t i, j, min_coreness, max_coreness;
	igraph_integer_t nv = igraph_vcount(graph);
	igraph_t subgraph;
	igraph_vs_t vs;
	igraph_vector_int_t vids, degree;

	IGRAPH_ASSERT(igraph_vector_int_size(coreness) == nv);
	if (nv < 1)
		return;

	min_coreness = igraph_vector_int_min(coreness);
	max_coreness = igraph_vector_int_max(coreness);

	IGRAPH_ASSERT(min_coreness >= 0);

	igraph_vector_int_init(&vids, 0);
	igraph_vector_int_init(&degree, 0);

	for (i = max_coreness; i >= 0; i--) {
		igraph_vector_int_clear(&vids);
		for (j = 0; j < nv; j++) {
			if (VECTOR(*coreness)[j] >= i)
				igraph_vector_int_push_back(&vids, j);
		}

		igraph_vs_vector(&vs, &vids);
		igraph_induced_subgraph(graph, &subgraph, vs, IGRAPH_SUBGRAPH_AUTO);
		igraph_vs_destroy(&vs);

		igraph_degree(&subgraph, &degree, igraph_vss_all(), mode, IGRAPH_LOOPS_TWICE);
		for (j = 0; j < igraph_vcount(&subgraph); j++)
			IGRAPH_ASSERT(VECTOR(degree)[j] >= i);

		igraph_destroy(&subgraph);
	}

	igraph_vector_int_destroy(&degree);
	igraph_vector_int_destroy(&vids);
}

#endif // VALIDATE_CORNESS_H
