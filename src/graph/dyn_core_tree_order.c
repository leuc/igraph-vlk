/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "graph/dyn_core_tree_order.h"

#include <stdio.h>
#include <stdlib.h>

// Weights for blending a vertex's intra-tree-node (same-sphere) neighbor
// barycenter against its parent-tree-node (adjacent, outer-sphere) neighbor
// barycenter. Which should dominate is an open visual-tuning question, not
// an algorithmic one; kept as named constants rather than a hardcoded 0.5/0.5
// so they're easy to find and retune independently.
#define DYN_CT_ORDER_INTRA_WEIGHT 0.5
#define DYN_CT_ORDER_INTER_WEIGHT 0.5

struct DynCoreTreeOrder
{
	double *vertex_rank;			  // per-vertex maintained rank, meaningful only within a vertex's current tree node (or against that node's parent)
	igraph_integer_t vcount;		  // vcount as of the last successful assign_ranks call
	igraph_integer_t vertex_capacity; // capacity of vertex_rank[]
	double next_append_rank;		  // monotonically increasing fallback rank for a vertex with no yet-ranked same-node/parent-node neighbor (e.g. a disconnected arrival)
};

static bool ensure_vertex_capacity(DynCoreTreeOrder *dto, igraph_integer_t n)
{
	if (n <= dto->vertex_capacity)
		return true;
	igraph_integer_t cap = dto->vertex_capacity ? dto->vertex_capacity : 64;
	while (cap < n)
		cap *= 2;
	double *grown = realloc(dto->vertex_rank, sizeof(double) * (size_t)cap);
	if (!grown) {
		fprintf(stderr, "dyn_core_tree_order: realloc vertex_rank to capacity %lld failed\n", (long long)cap);
		return false;
	}
	dto->vertex_rank = grown;
	dto->vertex_capacity = cap;
	return true;
}

// Computes and stores a rank for every vertex in [old_vcount, new_vcount), as
// a weighted barycenter of already-ranked same-tree-node neighbors (intra-
// sphere pull) and already-ranked parent-tree-node neighbors (inter-sphere
// pull). Processes vertices in ascending id order so a neighbor u < v within
// this same new range is already ranked by the time v is processed; a
// neighbor u > v in this same range is treated as not-yet-ranked and
// skipped, an accepted order-dependent approximation of the barycenter
// heuristic. Falls back to an append-to-end counter when a vertex has no
// qualifying neighbor at all (e.g. a disconnected arrival).
static bool assign_ranks(DynCoreTreeOrder *dto, const igraph_t *g, const DynCoreTree *ct, igraph_integer_t old_vcount, igraph_integer_t new_vcount)
{
	if (!ensure_vertex_capacity(dto, new_vcount))
		return false;

	igraph_vector_int_t neis;
	if (igraph_vector_int_init(&neis, 0) != IGRAPH_SUCCESS) {
		fprintf(stderr, "dyn_core_tree_order: allocation failed\n");
		return false;
	}

	for (igraph_integer_t v = old_vcount; v < new_vcount; v++) {
		int t = dyn_core_tree_node_of(ct, v);
		int p = (t >= 0) ? dyn_core_tree_parent(ct, t) : -1;

		double intra_sum = 0.0, inter_sum = 0.0;
		int intra_n = 0, inter_n = 0;

		if (igraph_incident(g, &neis, v, IGRAPH_ALL, IGRAPH_NO_LOOPS) == IGRAPH_SUCCESS) {
			igraph_integer_t m = igraph_vector_int_size(&neis);
			for (igraph_integer_t i = 0; i < m; i++) {
				igraph_integer_t from, to;
				if (igraph_edge(g, VECTOR(neis)[i], &from, &to) != IGRAPH_SUCCESS)
					continue;
				igraph_integer_t u = (from == v) ? to : from;
				if (u >= v)
					continue; // not yet ranked this batch (or self via a loop igraph_incident let through)

				int nu = dyn_core_tree_node_of(ct, u);
				if (nu == t) {
					intra_sum += dto->vertex_rank[u];
					intra_n++;
				} else if (p >= 0 && nu == p) {
					inter_sum += dto->vertex_rank[u];
					inter_n++;
				}
			}
		}

		double rank;
		if (intra_n > 0 && inter_n > 0)
			rank = DYN_CT_ORDER_INTRA_WEIGHT * (intra_sum / intra_n) + DYN_CT_ORDER_INTER_WEIGHT * (inter_sum / inter_n);
		else if (intra_n > 0)
			rank = intra_sum / intra_n;
		else if (inter_n > 0)
			rank = inter_sum / inter_n;
		else
			rank = dto->next_append_rank++;

		dto->vertex_rank[v] = rank;
	}

	igraph_vector_int_destroy(&neis);
	dto->vcount = new_vcount;
	return true;
}

DynCoreTreeOrder *dyn_core_tree_order_init(const igraph_t *g, const DynCoreTree *ct)
{
	DynCoreTreeOrder *dto = calloc(1, sizeof(DynCoreTreeOrder));
	if (!dto) {
		fprintf(stderr, "dyn_core_tree_order_init: allocation failed\n");
		return NULL;
	}
	if (!assign_ranks(dto, g, ct, 0, igraph_vcount(g))) {
		dyn_core_tree_order_destroy(dto);
		return NULL;
	}
	return dto;
}

bool dyn_core_tree_order_on_update(DynCoreTreeOrder *dto, const igraph_t *g, const DynCoreTree *ct)
{
	if (!dto)
		return false;
	igraph_integer_t vcount = igraph_vcount(g);
	if (vcount <= dto->vcount)
		return true;
	return assign_ranks(dto, g, ct, dto->vcount, vcount);
}

igraph_real_t dyn_core_tree_order_rank(const DynCoreTreeOrder *dto, igraph_integer_t v)
{
	if (!dto || v < 0 || v >= dto->vcount)
		return 0.0;
	return dto->vertex_rank[v];
}

void dyn_core_tree_order_destroy(DynCoreTreeOrder *dto)
{
	if (!dto)
		return;
	free(dto->vertex_rank);
	free(dto);
}
