/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "graph/dyn_k-core.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// State
//
// All per-vertex arrays are flat and grown by doubling; the scratch arrays
// (visit_stamp/evict_stamp/support) are epoch-stamped: an entry is valid only
// when its stamp equals the current epoch, so "clearing" them per traversal
// is a single counter increment instead of an O(V) memset.
// ============================================================================

struct DynKCore
{
	int *core;				 // coreness per vertex
	int *visit_stamp;		 // == epoch: vertex is in the current subcore
	int *evict_stamp;		 // == epoch: vertex was peeled from the current subcore
	int *support;			 // valid while visit_stamp == epoch
	igraph_integer_t *list;	 // collected subcore members (doubles as BFS queue)
	igraph_integer_t *queue; // peel cascade queue
	igraph_integer_t vcount;
	igraph_integer_t capacity;
	int epoch;
	igraph_vector_int_t neis; // reusable neighbor buffer
};

// ============================================================================
// Capacity / vertex-count sync
// ============================================================================

static bool dyn_kcore_sync_vcount(DynKCore *kc, const igraph_t *g)
{
	igraph_integer_t n = igraph_vcount(g);
	if (n <= kc->vcount)
		return true;

	if (n > kc->capacity) {
		igraph_integer_t cap = kc->capacity ? kc->capacity : 64;
		while (cap < n)
			cap *= 2;

		int *core = realloc(kc->core, sizeof(int) * cap);
		int *vs = realloc(kc->visit_stamp, sizeof(int) * cap);
		int *es = realloc(kc->evict_stamp, sizeof(int) * cap);
		int *sup = realloc(kc->support, sizeof(int) * cap);
		igraph_integer_t *list = realloc(kc->list, sizeof(igraph_integer_t) * cap);
		igraph_integer_t *queue = realloc(kc->queue, sizeof(igraph_integer_t) * cap);
		if (core)
			kc->core = core;
		if (vs)
			kc->visit_stamp = vs;
		if (es)
			kc->evict_stamp = es;
		if (sup)
			kc->support = sup;
		if (list)
			kc->list = list;
		if (queue)
			kc->queue = queue;
		if (!core || !vs || !es || !sup || !list || !queue) {
			fprintf(stderr, "dyn_kcore: realloc to capacity %lld failed\n", (long long)cap);
			return false;
		}
		kc->capacity = cap;
	}

	// New vertices: coreness 0, stamps invalidated (epoch starts at 1)
	memset(kc->core + kc->vcount, 0, sizeof(int) * (size_t)(n - kc->vcount));
	memset(kc->visit_stamp + kc->vcount, 0, sizeof(int) * (size_t)(n - kc->vcount));
	memset(kc->evict_stamp + kc->vcount, 0, sizeof(int) * (size_t)(n - kc->vcount));
	kc->vcount = n;
	return true;
}

// ============================================================================
// Support counting (the one place that defines core semantics; also the
// extension seam for (k,h)-cores, where 1-hop neighbors would become h-hop).
// Matches igraph_coreness: IGRAPH_ALL, self-loops twice, multiplicity kept.
// ============================================================================

static bool fetch_neighbors(DynKCore *kc, const igraph_t *g, igraph_integer_t w)
{
	if (igraph_neighbors(g, &kc->neis, w, IGRAPH_ALL, IGRAPH_LOOPS, true) != IGRAPH_SUCCESS) {
		fprintf(stderr, "dyn_kcore: igraph_neighbors failed for vertex %lld\n", (long long)w);
		return false;
	}
	return true;
}

// ============================================================================
// Single-edge insertion maintenance.
//
// Only the root's subcore (same-coreness region reachable through
// same-coreness vertices) can change, each vertex by at most +1
// [Sariyuce 2013, Thms 1/2/4]. We lift the whole subcore optimistically to
// K+1 and peel back every vertex without K+1 supporters — the binary-valued
// H-index fixpoint [Liu 2021, Thms 3.2/3.5], needing only a plain queue.
// ============================================================================

static bool process_insert(DynKCore *kc, const igraph_t *g, igraph_integer_t u, igraph_integer_t v)
{
	igraph_integer_t root = (kc->core[u] <= kc->core[v]) ? u : v;
	int k = kc->core[root];

	if (kc->epoch == INT_MAX) {
		memset(kc->visit_stamp, 0, sizeof(int) * (size_t)kc->vcount);
		memset(kc->evict_stamp, 0, sizeof(int) * (size_t)kc->vcount);
		kc->epoch = 0;
	}
	kc->epoch++;
	int epoch = kc->epoch;

	// Subcore BFS from the root over core==k vertices; support(w) counts
	// adjacency entries with core >= k (core > k: settled higher-core
	// neighbor; core == k: subcore member, optimistically lifted).
	igraph_integer_t list_len = 0;
	kc->visit_stamp[root] = epoch;
	kc->list[list_len++] = root;
	for (igraph_integer_t head = 0; head < list_len; head++) {
		igraph_integer_t w = kc->list[head];
		if (!fetch_neighbors(kc, g, w))
			return false;
		int s = 0;
		igraph_integer_t deg = igraph_vector_int_size(&kc->neis);
		for (igraph_integer_t i = 0; i < deg; i++) {
			igraph_integer_t x = VECTOR(kc->neis)[i];
			if (kc->core[x] >= k)
				s++;
			if (kc->core[x] == k && kc->visit_stamp[x] != epoch) {
				kc->visit_stamp[x] = epoch;
				kc->list[list_len++] = x;
			}
		}
		kc->support[w] = s;
	}

	// Peel: evict every subcore vertex without k+1 supporters, cascading.
	igraph_integer_t queue_len = 0;
	for (igraph_integer_t i = 0; i < list_len; i++) {
		igraph_integer_t w = kc->list[i];
		if (kc->support[w] <= k) {
			kc->evict_stamp[w] = epoch;
			kc->queue[queue_len++] = w;
		}
	}
	for (igraph_integer_t head = 0; head < queue_len; head++) {
		igraph_integer_t w = kc->queue[head];
		if (!fetch_neighbors(kc, g, w))
			return false;
		igraph_integer_t deg = igraph_vector_int_size(&kc->neis);
		for (igraph_integer_t i = 0; i < deg; i++) {
			igraph_integer_t x = VECTOR(kc->neis)[i];
			if (kc->core[x] == k && kc->visit_stamp[x] == epoch && kc->evict_stamp[x] != epoch) {
				if (--kc->support[x] <= k) {
					kc->evict_stamp[x] = epoch;
					kc->queue[queue_len++] = x;
				}
			}
		}
	}

	// Lift survivors.
	for (igraph_integer_t i = 0; i < list_len; i++) {
		igraph_integer_t w = kc->list[i];
		if (kc->evict_stamp[w] != epoch)
			kc->core[w] = k + 1;
	}
	return true;
}

// ============================================================================
// Public API
// ============================================================================

DynKCore *dyn_kcore_init(const igraph_t *g)
{
	DynKCore *kc = calloc(1, sizeof(DynKCore));
	if (!kc) {
		fprintf(stderr, "dyn_kcore_init: allocation failed\n");
		return NULL;
	}
	if (igraph_vector_int_init(&kc->neis, 0) != IGRAPH_SUCCESS) {
		free(kc);
		return NULL;
	}

	if (!dyn_kcore_sync_vcount(kc, g)) {
		dyn_kcore_destroy(kc);
		return NULL;
	}

	if (kc->vcount > 0) {
		igraph_vector_int_t cores;
		if (igraph_vector_int_init(&cores, kc->vcount) != IGRAPH_SUCCESS) {
			dyn_kcore_destroy(kc);
			return NULL;
		}
		if (igraph_coreness(g, &cores, IGRAPH_ALL) != IGRAPH_SUCCESS) {
			fprintf(stderr, "dyn_kcore_init: igraph_coreness failed\n");
			igraph_vector_int_destroy(&cores);
			dyn_kcore_destroy(kc);
			return NULL;
		}
		for (igraph_integer_t i = 0; i < kc->vcount; i++)
			kc->core[i] = (int)VECTOR(cores)[i];
		igraph_vector_int_destroy(&cores);
	}
	return kc;
}

bool dyn_kcore_on_edges(DynKCore *kc, const igraph_t *g, const igraph_vector_int_t *new_edges)
{
	if (!kc)
		return false;
	if (!dyn_kcore_sync_vcount(kc, g))
		return false;
	if (!new_edges)
		return true;

	igraph_integer_t n = igraph_vector_int_size(new_edges) / 2;
	for (igraph_integer_t i = 0; i < n; i++) {
		igraph_integer_t u = VECTOR(*new_edges)[2 * i];
		igraph_integer_t v = VECTOR(*new_edges)[2 * i + 1];
		if (u < 0 || u >= kc->vcount || v < 0 || v >= kc->vcount) {
			fprintf(stderr, "dyn_kcore: edge (%lld,%lld) out of range, skipping\n", (long long)u, (long long)v);
			continue;
		}
		// A self-loop counts twice toward degree (igraph_coreness semantics),
		// so it can raise its vertex's coreness by up to 2 — one extra pass
		// covers that; the peel makes unneeded passes no-ops.
		int passes = (u == v) ? 2 : 1;
		for (int p = 0; p < passes; p++) {
			if (!process_insert(kc, g, u, v))
				return false;
		}
	}
	return true;
}

const int *dyn_kcore_values(const DynKCore *kc)
{
	return kc ? kc->core : NULL;
}

int dyn_kcore_get(const DynKCore *kc, igraph_integer_t v)
{
	if (!kc || v < 0 || v >= kc->vcount)
		return 0;
	return kc->core[v];
}

bool dyn_kcore_verify(const DynKCore *kc, const igraph_t *g)
{
	if (!kc)
		return false;
	igraph_integer_t n = igraph_vcount(g);
	if (n != kc->vcount) {
		fprintf(stderr, "dyn_kcore_verify: vcount mismatch (maintained %lld, graph %lld)\n", (long long)kc->vcount, (long long)n);
		return false;
	}
	if (n == 0)
		return true;

	igraph_vector_int_t cores;
	if (igraph_vector_int_init(&cores, n) != IGRAPH_SUCCESS)
		return false;
	if (igraph_coreness(g, &cores, IGRAPH_ALL) != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(&cores);
		return false;
	}

	igraph_integer_t mismatches = 0;
	for (igraph_integer_t i = 0; i < n; i++) {
		if (kc->core[i] != (int)VECTOR(cores)[i]) {
			if (mismatches < 10)
				fprintf(stderr, "dyn_kcore_verify: vertex %lld maintained %d, actual %lld\n", (long long)i, kc->core[i], (long long)VECTOR(cores)[i]);
			mismatches++;
		}
	}
	igraph_vector_int_destroy(&cores);

	if (mismatches > 0) {
		fprintf(stderr, "dyn_kcore_verify: %lld mismatch(es) of %lld vertices\n", (long long)mismatches, (long long)n);
		return false;
	}
	return true;
}

void dyn_kcore_destroy(DynKCore *kc)
{
	if (!kc)
		return;
	igraph_vector_int_destroy(&kc->neis);
	free(kc->core);
	free(kc->visit_stamp);
	free(kc->evict_stamp);
	free(kc->support);
	free(kc->list);
	free(kc->queue);
	free(kc);
}
