/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Standalone correctness + performance harness for graph/dyn_k-core.
 * Streams edge batches into an igraph_t the same way graph/stream.c does
 * (vertices added first, then one igraph_add_edges call, then
 * dyn_kcore_on_edges), and after every batch compares the maintained
 * coreness against the igraph_coreness oracle.
 */

#include "graph/dyn_k-core.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static int failures = 0;

static void check(const char *name, bool ok)
{
	printf("%-40s %s\n", name, ok ? "PASS" : "FAIL");
	if (!ok)
		failures++;
}

// ============================================================================
// Deterministic PRNG (xorshift64*)
// ============================================================================

static uint64_t rng_state = 0x9E3779B97F4A7C15ULL;

static uint64_t rng_next(void)
{
	uint64_t x = rng_state;
	x ^= x >> 12;
	x ^= x << 25;
	x ^= x >> 27;
	rng_state = x;
	return x * 0x2545F4914F6CDD1DULL;
}

static igraph_integer_t rng_below(igraph_integer_t n)
{
	return (igraph_integer_t)(rng_next() % (uint64_t)n);
}

// ============================================================================
// Batch application mimicking graph/stream.c's flow
// ============================================================================

static bool apply_batch(igraph_t *g, DynKCore *kc, const igraph_vector_int_t *edges)
{
	igraph_integer_t maxid = -1;
	igraph_integer_t n = igraph_vector_int_size(edges);
	for (igraph_integer_t i = 0; i < n; i++) {
		if (VECTOR(*edges)[i] > maxid)
			maxid = VECTOR(*edges)[i];
	}
	if (maxid >= igraph_vcount(g)) {
		if (igraph_add_vertices(g, maxid + 1 - igraph_vcount(g), NULL) != IGRAPH_SUCCESS)
			return false;
	}
	if (igraph_add_edges(g, edges, NULL) != IGRAPH_SUCCESS)
		return false;
	return dyn_kcore_on_edges(kc, g, edges);
}

// Push one edge into the pending batch; flush + verify when full.
static bool push_edge(igraph_t *g, DynKCore *kc, igraph_vector_int_t *batch, igraph_integer_t batch_edges, igraph_integer_t u, igraph_integer_t v)
{
	if (igraph_vector_int_push_back(batch, u) != IGRAPH_SUCCESS || igraph_vector_int_push_back(batch, v) != IGRAPH_SUCCESS)
		return false;
	if (igraph_vector_int_size(batch) / 2 >= batch_edges) {
		if (!apply_batch(g, kc, batch))
			return false;
		if (!dyn_kcore_verify(kc, g))
			return false;
		igraph_vector_int_clear(batch);
	}
	return true;
}

static bool flush_batch(igraph_t *g, DynKCore *kc, igraph_vector_int_t *batch)
{
	if (igraph_vector_int_size(batch) > 0) {
		if (!apply_batch(g, kc, batch))
			return false;
		if (!dyn_kcore_verify(kc, g))
			return false;
		igraph_vector_int_clear(batch);
	}
	return true;
}

// ============================================================================
// Cases
// ============================================================================

// Fixed shapes exercising the interesting transitions, one batch per step.
static bool case_micro(void)
{
	static const igraph_integer_t steps[][2] = {
		{0, 1},					// pair -> both coreness 1
		{1, 2},					// path
		{2, 0},					// close triangle -> all coreness 2
		{3, 4},					// separate pair
		{4, 5}, {5, 6}, {6, 3}, // square -> coreness 2
		{0, 0},					// self-loop on triangle vertex (counts twice, no lift)
		{7, 7},					// self-loop on fresh vertex -> coreness 2
		{0, 1},					// parallel edge inside the triangle
		{2, 8}, {8, 9}, {9, 2}, // second triangle sharing vertex 2
		{0, 3},					// bridge between components
	};

	igraph_t g;
	if (igraph_empty(&g, 0, IGRAPH_UNDIRECTED) != IGRAPH_SUCCESS)
		return false;
	DynKCore *kc = dyn_kcore_init(&g);
	bool ok = (kc != NULL);

	igraph_vector_int_t batch;
	if (ok && igraph_vector_int_init(&batch, 0) == IGRAPH_SUCCESS) {
		size_t n_steps = sizeof(steps) / sizeof(steps[0]);
		for (size_t i = 0; ok && i < n_steps; i++)
			ok = push_edge(&g, kc, &batch, 1, steps[i][0], steps[i][1]);
		igraph_vector_int_destroy(&batch);
	} else {
		ok = false;
	}

	dyn_kcore_destroy(kc);
	igraph_destroy(&g);
	return ok;
}

// K5 with all 10 edges arriving in a single batch of fresh vertices.
static bool case_clique_single_batch(void)
{
	igraph_t g;
	if (igraph_empty(&g, 0, IGRAPH_UNDIRECTED) != IGRAPH_SUCCESS)
		return false;
	DynKCore *kc = dyn_kcore_init(&g);
	bool ok = (kc != NULL);

	igraph_vector_int_t batch;
	if (ok && igraph_vector_int_init(&batch, 0) == IGRAPH_SUCCESS) {
		for (igraph_integer_t u = 0; ok && u < 5; u++) {
			for (igraph_integer_t v = u + 1; ok && v < 5; v++) {
				ok = igraph_vector_int_push_back(&batch, u) == IGRAPH_SUCCESS && igraph_vector_int_push_back(&batch, v) == IGRAPH_SUCCESS;
			}
		}
		if (ok)
			ok = apply_batch(&g, kc, &batch) && dyn_kcore_verify(kc, &g);
		igraph_vector_int_destroy(&batch);
	} else {
		ok = false;
	}

	dyn_kcore_destroy(kc);
	igraph_destroy(&g);
	return ok;
}

// Long chain (a giant coreness-1 subcore), then chords closing triangles —
// the adversarial same-K case for subcore traversal.
static bool case_chain_chords(void)
{
	const igraph_integer_t len = 2000;

	igraph_t g;
	if (igraph_empty(&g, 0, IGRAPH_UNDIRECTED) != IGRAPH_SUCCESS)
		return false;
	DynKCore *kc = dyn_kcore_init(&g);
	bool ok = (kc != NULL);

	igraph_vector_int_t batch;
	if (ok && igraph_vector_int_init(&batch, 0) == IGRAPH_SUCCESS) {
		for (igraph_integer_t i = 0; ok && i + 1 < len; i++)
			ok = push_edge(&g, kc, &batch, 173, i, i + 1);
		for (igraph_integer_t i = 0; ok && i + 2 < len; i += 5)
			ok = push_edge(&g, kc, &batch, 173, i, i + 2);
		if (ok)
			ok = flush_batch(&g, kc, &batch);
		igraph_vector_int_destroy(&batch);
	} else {
		ok = false;
	}

	dyn_kcore_destroy(kc);
	igraph_destroy(&g);
	return ok;
}

// Uniform random edges over a fixed vertex range (duplicates and self-loops
// arise naturally), verified per batch.
static bool case_random_uniform(void)
{
	const igraph_integer_t n_vertices = 500;
	const igraph_integer_t n_edges = 6000;

	igraph_t g;
	if (igraph_empty(&g, 0, IGRAPH_UNDIRECTED) != IGRAPH_SUCCESS)
		return false;
	DynKCore *kc = dyn_kcore_init(&g);
	bool ok = (kc != NULL);

	igraph_vector_int_t batch;
	if (ok && igraph_vector_int_init(&batch, 0) == IGRAPH_SUCCESS) {
		for (igraph_integer_t i = 0; ok && i < n_edges; i++)
			ok = push_edge(&g, kc, &batch, 97, rng_below(n_vertices), rng_below(n_vertices));
		if (ok)
			ok = flush_batch(&g, kc, &batch);
		igraph_vector_int_destroy(&batch);
	} else {
		ok = false;
	}

	dyn_kcore_destroy(kc);
	igraph_destroy(&g);
	return ok;
}

// Preferential-attachment-flavoured stream: each new vertex attaches to
// endpoints sampled from the existing edge list (degree-biased).
static bool case_pref_attach(igraph_integer_t n_vertices, igraph_integer_t edges_per_vertex, igraph_integer_t batch_edges, bool verify_each_batch, double *out_dyn_seconds, igraph_integer_t *out_edges)
{
	igraph_t g;
	if (igraph_empty(&g, 0, IGRAPH_UNDIRECTED) != IGRAPH_SUCCESS)
		return false;
	DynKCore *kc = dyn_kcore_init(&g);
	bool ok = (kc != NULL);

	igraph_integer_t *endpoints = malloc(sizeof(igraph_integer_t) * (size_t)n_vertices * (size_t)edges_per_vertex * 2);
	igraph_integer_t endpoint_count = 0;
	double dyn_seconds = 0.0;
	igraph_integer_t total_edges = 0;

	igraph_vector_int_t batch;
	if (ok && endpoints && igraph_vector_int_init(&batch, 0) == IGRAPH_SUCCESS) {
		for (igraph_integer_t v = 1; ok && v < n_vertices; v++) {
			igraph_integer_t m = (v < edges_per_vertex) ? 1 : edges_per_vertex;
			for (igraph_integer_t j = 0; ok && j < m; j++) {
				igraph_integer_t target = (endpoint_count == 0) ? 0 : endpoints[rng_below(endpoint_count)];
				endpoints[endpoint_count++] = v;
				endpoints[endpoint_count++] = target;
				ok = igraph_vector_int_push_back(&batch, v) == IGRAPH_SUCCESS && igraph_vector_int_push_back(&batch, target) == IGRAPH_SUCCESS;
				total_edges++;
			}
			if (ok && igraph_vector_int_size(&batch) / 2 >= batch_edges) {
				igraph_integer_t maxid = -1;
				for (igraph_integer_t i = 0; i < igraph_vector_int_size(&batch); i++)
					if (VECTOR(batch)[i] > maxid)
						maxid = VECTOR(batch)[i];
				if (maxid >= igraph_vcount(&g))
					ok = igraph_add_vertices(&g, maxid + 1 - igraph_vcount(&g), NULL) == IGRAPH_SUCCESS;
				if (ok)
					ok = igraph_add_edges(&g, &batch, NULL) == IGRAPH_SUCCESS;
				if (ok) {
					struct timespec t0, t1;
					clock_gettime(CLOCK_MONOTONIC, &t0);
					ok = dyn_kcore_on_edges(kc, &g, &batch);
					clock_gettime(CLOCK_MONOTONIC, &t1);
					dyn_seconds += (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) * 1e-9;
				}
				if (ok && verify_each_batch)
					ok = dyn_kcore_verify(kc, &g);
				igraph_vector_int_clear(&batch);
			}
		}
		if (ok)
			ok = flush_batch(&g, kc, &batch);
		if (ok)
			ok = dyn_kcore_verify(kc, &g); // final full check either way
		igraph_vector_int_destroy(&batch);
	} else {
		ok = false;
	}

	free(endpoints);
	if (out_dyn_seconds)
		*out_dyn_seconds = dyn_seconds;
	if (out_edges)
		*out_edges = total_edges;
	dyn_kcore_destroy(kc);
	igraph_destroy(&g);
	return ok;
}

// Firehose-shaped stream at scale: components grow like reply threads —
// mostly brand-new vertices attaching to a random member of the current
// thread (root of the insertion is the new coreness-0 vertex: O(1)), plus
// occasional closure edges between existing members of the same thread
// (subcore bounded by thread size). This mirrors real streaming workloads;
// uniform-coreness graphs (the BA pathology in Sariyuce et al. §6.2, where
// the subcore approaches the whole graph) are exercised for correctness at
// small scale by the pref-attach case instead.
static bool case_firehose_perf(igraph_integer_t n_vertices, igraph_integer_t thread_size, igraph_integer_t batch_edges, double *out_dyn_seconds, igraph_integer_t *out_edges)
{
	igraph_t g;
	if (igraph_empty(&g, 0, IGRAPH_UNDIRECTED) != IGRAPH_SUCCESS)
		return false;
	DynKCore *kc = dyn_kcore_init(&g);
	bool ok = (kc != NULL);

	double dyn_seconds = 0.0;
	igraph_integer_t total_edges = 0;
	igraph_integer_t thread_start = 0;
	igraph_integer_t next_vid = 1; // vertex 0 roots the first thread

	igraph_vector_int_t batch;
	if (ok && igraph_vector_int_init(&batch, 0) == IGRAPH_SUCCESS) {
		while (ok && next_vid < n_vertices) {
			if (next_vid - thread_start >= thread_size)
				thread_start = next_vid++; // start a fresh thread root
			if (next_vid >= n_vertices)
				break;

			igraph_integer_t u, v;
			if (rng_below(20) == 0 && next_vid - thread_start >= 3) {
				// closure edge inside the current thread
				u = thread_start + rng_below(next_vid - thread_start);
				v = thread_start + rng_below(next_vid - thread_start);
			} else {
				// new vertex replies to a random member of the thread
				u = next_vid++;
				v = thread_start + rng_below(u - thread_start);
			}
			ok = igraph_vector_int_push_back(&batch, u) == IGRAPH_SUCCESS && igraph_vector_int_push_back(&batch, v) == IGRAPH_SUCCESS;
			total_edges++;

			if (ok && igraph_vector_int_size(&batch) / 2 >= batch_edges) {
				if (next_vid > igraph_vcount(&g))
					ok = igraph_add_vertices(&g, next_vid - igraph_vcount(&g), NULL) == IGRAPH_SUCCESS;
				if (ok)
					ok = igraph_add_edges(&g, &batch, NULL) == IGRAPH_SUCCESS;
				if (ok) {
					struct timespec t0, t1;
					clock_gettime(CLOCK_MONOTONIC, &t0);
					ok = dyn_kcore_on_edges(kc, &g, &batch);
					clock_gettime(CLOCK_MONOTONIC, &t1);
					dyn_seconds += (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) * 1e-9;
				}
				igraph_vector_int_clear(&batch);
			}
		}
		if (ok && igraph_vector_int_size(&batch) > 0) {
			if (next_vid > igraph_vcount(&g))
				ok = igraph_add_vertices(&g, next_vid - igraph_vcount(&g), NULL) == IGRAPH_SUCCESS;
			if (ok)
				ok = igraph_add_edges(&g, &batch, NULL) == IGRAPH_SUCCESS;
			if (ok)
				ok = dyn_kcore_on_edges(kc, &g, &batch);
			igraph_vector_int_clear(&batch);
		}
		if (ok)
			ok = dyn_kcore_verify(kc, &g); // one full oracle check at the end
		igraph_vector_int_destroy(&batch);
	} else {
		ok = false;
	}

	if (out_dyn_seconds)
		*out_dyn_seconds = dyn_seconds;
	if (out_edges)
		*out_edges = total_edges;
	dyn_kcore_destroy(kc);
	igraph_destroy(&g);
	return ok;
}

// Bootstrap on an existing graph, then continue streaming.
static bool case_bootstrap(void)
{
	igraph_t g;
	if (igraph_small(&g, 0, IGRAPH_UNDIRECTED, 0, 1, 1, 2, 2, 0, 2, 3, 3, 4, -1) != IGRAPH_SUCCESS)
		return false;
	DynKCore *kc = dyn_kcore_init(&g);
	bool ok = (kc != NULL) && dyn_kcore_verify(kc, &g);

	igraph_vector_int_t batch;
	if (ok && igraph_vector_int_init(&batch, 0) == IGRAPH_SUCCESS) {
		ok = push_edge(&g, kc, &batch, 1, 3, 0); // close a square through the triangle
		if (ok)
			ok = push_edge(&g, kc, &batch, 1, 4, 0); // and a pentagon
		igraph_vector_int_destroy(&batch);
	} else {
		ok = false;
	}

	dyn_kcore_destroy(kc);
	igraph_destroy(&g);
	return ok;
}

// ============================================================================
// Main
// ============================================================================

int main(void)
{
	setvbuf(stdout, NULL, _IOLBF, 0);

	check("micro shapes (per-edge batches)", case_micro());
	check("K5 in a single batch", case_clique_single_batch());
	check("chain + chords (same-K adversarial)", case_chain_chords());
	check("uniform random multigraph", case_random_uniform());
	check("pref-attach stream (verified batches)", case_pref_attach(3000, 3, 61, true, NULL, NULL));
	check("bootstrap from existing graph", case_bootstrap());

	// Perf: 500k vertices in thread-shaped components, batches of 5000,
	// oracle check at the end.
	double dyn_seconds = 0.0;
	igraph_integer_t total_edges = 0;
	bool perf_ok = case_firehose_perf(500000, 400, 5000, &dyn_seconds, &total_edges);
	check("perf 500k-vertex firehose stream", perf_ok);
	if (perf_ok) {
		printf("  maintenance: %lld edges in %.3f s (%.0f edges/s, %.2f us/edge)\n", (long long)total_edges, dyn_seconds, (double)total_edges / dyn_seconds, dyn_seconds * 1e6 / (double)total_edges);
	}

	if (failures == 0) {
		printf("all tests passed\n");
		return EXIT_SUCCESS;
	}
	printf("%d test(s) FAILED\n", failures);
	return EXIT_FAILURE;
}
