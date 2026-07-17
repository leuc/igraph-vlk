/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Data-driven correctness tests for graph/dyn_leiden.
 *
 * verify_against_fresh_leiden() is the unit-test-only oracle: it recomputes
 * a fresh igraph_community_leiden_simple() partition (same CPM objective and
 * density-scaled resolution as wrappers_community.c's
 * compute_igraph_community_leiden, using dyn_leiden_resolution(dl) as the
 * exact gamma the maintainer last used) and compares CPM QUALITY against the
 * maintained membership's CPM quality — never membership directly, since
 * community ids are not comparable across independent runs. This performs a
 * full O(V+E) recompute that would defeat the point of dynamic maintenance
 * if called outside of tests, which is why it lives only here and not in
 * src/graph/dyn_leiden.c.
 *
 * igraph has no ready-made CPM quality function, so cpm_quality() below is a
 * hand-written helper using igraph's own convention (see leiden.c), cross-
 * checked against igraph_community_leiden_simple()'s own returned quality on
 * the fresh partition to confirm the convention matches.
 *
 * No benchmarking: timing/throughput belongs in a separate harness.
 */

#include "graph/dyn_leiden.h"
#include "test_utilities.h"

#include <math.h>
#include <stdlib.h>

// Q = (1/2m) * sum_c (2*w_int_c - gamma*n_c^2), igraph's CPM convention with
// node weight n_i = 1 (see leiden.c's leiden_quality). Labels need not be
// compact: maintained labels are representative vertex ids < vcount, fresh
// labels are compact cluster indices < vcount — either way a vcount-sized
// dense accumulator covers them.
static int cpm_quality(const igraph_t *g, const igraph_vector_int_t *membership, double gamma, igraph_real_t *out_q)
{
	igraph_integer_t n = igraph_vcount(g);
	igraph_integer_t ecount = igraph_ecount(g);
	if (ecount == 0) {
		*out_q = 0;
		return 1;
	}

	double *w_int = calloc((size_t)n, sizeof(double));
	igraph_integer_t *csiz = calloc((size_t)n, sizeof(igraph_integer_t));
	if (!w_int || !csiz) {
		free(w_int);
		free(csiz);
		return 0;
	}

	for (igraph_integer_t v = 0; v < n; v++)
		csiz[VECTOR(*membership)[v]]++;

	for (igraph_integer_t e = 0; e < ecount; e++) {
		igraph_integer_t from, to;
		if (igraph_edge(g, e, &from, &to) != IGRAPH_SUCCESS) {
			free(w_int);
			free(csiz);
			return 0;
		}
		igraph_integer_t cf = VECTOR(*membership)[from];
		igraph_integer_t ct = VECTOR(*membership)[to];
		if (cf == ct)
			w_int[cf] += 1.0;
	}

	double sum = 0.0;
	for (igraph_integer_t c = 0; c < n; c++)
		if (csiz[c] > 0)
			sum += 2.0 * w_int[c] - gamma * (double)csiz[c] * (double)csiz[c];

	free(w_int);
	free(csiz);
	*out_q = (igraph_real_t)(sum / (2.0 * (double)ecount));
	return 1;
}

static int verify_against_fresh_leiden(const DynLeiden *dl, const igraph_t *g, igraph_real_t tolerance)
{
	igraph_integer_t n = igraph_vcount(g);
	if (n == 0)
		return 1;

	double gamma = dyn_leiden_resolution(dl);

	const igraph_integer_t *maintained_raw = dyn_leiden_membership(dl);
	if (!maintained_raw)
		return 0;

	igraph_vector_int_t maintained;
	if (igraph_vector_int_init(&maintained, n) != IGRAPH_SUCCESS)
		return 0;
	for (igraph_integer_t i = 0; i < n; i++)
		VECTOR(maintained)[i] = maintained_raw[i];

	// CPM at the maintainer's own gamma (see dyn_leiden_choose) — both sides
	// of this comparison must use the same resolution/objective or the delta
	// is meaningless.
	igraph_real_t maintained_q = 0;
	int ok = cpm_quality(g, &maintained, gamma, &maintained_q);
	igraph_vector_int_destroy(&maintained);
	if (!ok)
		return 0;

	igraph_vector_int_t fresh;
	if (igraph_vector_int_init(&fresh, n) != IGRAPH_SUCCESS)
		return 0;
	igraph_int_t nb_clusters;
	igraph_real_t leiden_quality = 0;
	igraph_error_t code = igraph_community_leiden_simple(g, NULL, IGRAPH_LEIDEN_OBJECTIVE_CPM, gamma, 0.01, 0, -1, &fresh, &nb_clusters, &leiden_quality);
	if (code != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(&fresh);
		return 0;
	}

	// Recompute the fresh partition's CPM quality via cpm_quality(), then
	// cross-check against igraph_community_leiden_simple()'s own returned
	// quality to confirm cpm_quality()'s convention actually matches
	// igraph's (rather than silently drifting apart from it).
	igraph_real_t fresh_q = 0;
	ok = cpm_quality(g, &fresh, gamma, &fresh_q);
	igraph_vector_int_destroy(&fresh);
	if (!ok)
		return 0;
	if (fabs((double)fresh_q - (double)leiden_quality) > 1e-6) {
		fprintf(stderr, "verify_against_fresh_leiden: cpm_quality/leiden quality mismatch: %.9f vs %.9f\n", (double)fresh_q, (double)leiden_quality);
		return 0;
	}

	igraph_real_t delta = fabs((double)maintained_q - (double)fresh_q);
	if (delta > tolerance) {
		fprintf(stderr, "verify_against_fresh_leiden: gamma=%.6f maintained Q=%.6f fresh Q=%.6f delta=%.6f (tolerance %.6f, communities: maintained=%d fresh=%lld)\n", gamma, (double)maintained_q, (double)fresh_q, (double)delta, (double)tolerance, dyn_leiden_community_count(dl), (long long)nb_clusters);
		return 0;
	}
	return 1;
}

// Stream `edges` (flat u,v pairs) into g/dl in batches of `batch_edges`
// edges, mirroring graph/stream.c, verifying against the oracle after every
// batch.
static int run_leiden_case(const igraph_integer_t *edges, size_t n_edges, igraph_integer_t n_vertices, int batch_edges, igraph_real_t tolerance)
{
	igraph_t g;
	IGRAPH_ASSERT(igraph_empty(&g, 0, IGRAPH_UNDIRECTED) == IGRAPH_SUCCESS);
	DynLeiden *dl = dyn_leiden_init(&g);
	IGRAPH_ASSERT(dl != NULL);

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
			IGRAPH_ASSERT(dyn_leiden_on_edges(dl, &g, &batch, NULL));
			IGRAPH_ASSERT(verify_against_fresh_leiden(dl, &g, tolerance));
			igraph_vector_int_clear(&batch);
		}
	}

	IGRAPH_ASSERT(dyn_leiden_community_count(dl) >= 1 && dyn_leiden_community_count(dl) <= n_vertices);

	igraph_vector_int_destroy(&batch);
	dyn_leiden_destroy(dl);
	igraph_destroy(&g);
	return 0;
}

static int test_micro_self_loops(void)
{
	// Two triangles (0-1-2, 3-4-5) joined by a bridge 2-3, plus a self-loop
	// and a parallel edge inside the first triangle.
	static const igraph_integer_t edges[] = {
		0, 1, 1, 2, 2, 0, 3, 4, 4, 5, 5, 3, 2, 3, 0, 0, 0, 1,
	};
	return run_leiden_case(edges, sizeof(edges) / sizeof(edges[0]), 6, 1, 0.05);
}

static int test_single_batch_clique(void)
{
	// K5 in one batch of fresh vertices. Density = 1.0 (fully connected) so
	// gamma hits the 3.0 ceiling; any k-vertex sub-community there scores
	// worse than k singletons (e.g. the full clique: 2*10-3*25=-55 vs 0 for
	// 5 singletons), so all-singletons is CPM-optimal. A degenerate-regime
	// guard: the maintainer must not merge densely-connected vertices when
	// gamma is this high, and both sides of the oracle should agree exactly.
	static const igraph_integer_t edges[] = {
		0, 1, 0, 2, 0, 3, 0, 4, 1, 2, 1, 3, 1, 4, 2, 3, 2, 4, 3, 4,
	};
	return run_leiden_case(edges, sizeof(edges) / sizeof(edges[0]), 5, 99, 0.05);
}

static int test_two_cliques_bridge(void)
{
	// Two disjoint K6 cliques (15 edges each) joined by a single bridge edge
	// added last — exercises cross-community frontier marking and
	// refinement on a graph with real community structure.
	//
	// Under CPM this is actually a dense/degenerate-regime guard rather than
	// a community-formation case: with V=12, gamma = max(3*2E/(12*11), 0.001)
	// grows quickly with the stream and reaches 1.41 at the full E=31
	// (density = 62/132 = 0.470). A full K6 community scores
	// 2*15 - 1.41*36 = -20.7 * per-community-quality-units, worse than 6
	// singletons scoring 0 - so an all-singletons partition is CPM-optimal
	// at (or near) every prefix, and both the maintained and fresh
	// partitions should agree on that (delta near 0). This exercises "the
	// maintainer must NOT merge when gamma is too high for the graph's
	// actual density" rather than genuine community discovery (see
	// test_sparse_two_cliques for that).
	static const igraph_integer_t edges[] = {
		0, 1, 0, 2, 0, 3, 0, 4, 0, 5, 1, 2, 1, 3, 1, 4, 1, 5, 2, 3, 2, 4, 2, 5, 3, 4, 3, 5, 4, 5, 6, 7, 6, 8, 6, 9, 6, 10, 6, 11, 7, 8, 7, 9, 7, 10, 7, 11, 8, 9, 8, 10, 8, 11, 9, 10, 9, 11, 10, 11, 5, 6,
	};
	return run_leiden_case(edges, sizeof(edges) / sizeof(edges[0]), 12, 3, 0.1);
}

static int test_sparse_two_cliques(void)
{
	// Same two-K6-plus-bridge structure as test_two_cliques_bridge, but
	// padded to 60 vertices so density (and hence gamma) stays low enough
	// for CPM to actually prefer merging within each clique: with V=60,
	// V*(V-1)=3540, gamma = max(6E/3540, 0.001) rises from ~0.005 (E=3) to
	// ~0.053 (E=31, the full edge list). A full K6 community there scores
	// 2*15 - 0.053*36 = 28.1, strictly better than any split (e.g. two
	// triangles: 2*(2*3 - 0.053*9) = 11.05), while the cross-clique bridge
	// (e_ct=1 vs gamma*6*6=1.89) is correctly rejected. The CPM-optimal
	// partition is therefore exactly {0..5}, {6..11}, and 48 singletons: 50
	// communities total — a non-degenerate regime where CPM must both find
	// real community structure and stop at the bridge.
	igraph_t g;
	IGRAPH_ASSERT(igraph_empty(&g, 0, IGRAPH_UNDIRECTED) == IGRAPH_SUCCESS);
	IGRAPH_ASSERT(igraph_add_vertices(&g, 60, NULL) == IGRAPH_SUCCESS);
	DynLeiden *dl = dyn_leiden_init(&g);
	IGRAPH_ASSERT(dl != NULL);

	static const igraph_integer_t edges[] = {
		0, 1, 0, 2, 0, 3, 0, 4, 0, 5, 1, 2, 1, 3, 1, 4, 1, 5, 2, 3, 2, 4, 2, 5, 3, 4, 3, 5, 4, 5, 6, 7, 6, 8, 6, 9, 6, 10, 6, 11, 7, 8, 7, 9, 7, 10, 7, 11, 8, 9, 8, 10, 8, 11, 9, 10, 9, 11, 10, 11, 5, 6,
	};
	size_t n_edges = sizeof(edges) / sizeof(edges[0]) / 2;
	const int batch_edges = 3;

	igraph_vector_int_t batch;
	IGRAPH_ASSERT(igraph_vector_int_init(&batch, 0) == IGRAPH_SUCCESS);

	for (size_t i = 0; i < n_edges; i++) {
		IGRAPH_ASSERT(igraph_vector_int_push_back(&batch, edges[2 * i]) == IGRAPH_SUCCESS);
		IGRAPH_ASSERT(igraph_vector_int_push_back(&batch, edges[2 * i + 1]) == IGRAPH_SUCCESS);

		if (igraph_vector_int_size(&batch) / 2 >= batch_edges || i + 1 == n_edges) {
			IGRAPH_ASSERT(igraph_add_edges(&g, &batch, NULL) == IGRAPH_SUCCESS);
			IGRAPH_ASSERT(dyn_leiden_on_edges(dl, &g, &batch, NULL));
			IGRAPH_ASSERT(verify_against_fresh_leiden(dl, &g, 0.02));
			igraph_vector_int_clear(&batch);
		}
	}

	igraph_integer_t c0 = dyn_leiden_get(dl, 0);
	igraph_integer_t c6 = dyn_leiden_get(dl, 6);
	IGRAPH_ASSERT(c0 != c6);
	for (igraph_integer_t v = 1; v <= 5; v++)
		IGRAPH_ASSERT(dyn_leiden_get(dl, v) == c0);
	for (igraph_integer_t v = 7; v <= 11; v++)
		IGRAPH_ASSERT(dyn_leiden_get(dl, v) == c6);
	IGRAPH_ASSERT(dyn_leiden_community_count(dl) == 50);

	igraph_vector_int_destroy(&batch);
	dyn_leiden_destroy(dl);
	igraph_destroy(&g);
	return 0;
}

static int test_uniform_random(void)
{
	igraph_rng_seed(igraph_rng_default(), 137);

	igraph_integer_t n_vertices = 60;
	igraph_t g;
	IGRAPH_ASSERT(igraph_empty(&g, 0, IGRAPH_UNDIRECTED) == IGRAPH_SUCCESS);
	IGRAPH_ASSERT(igraph_add_vertices(&g, n_vertices, NULL) == IGRAPH_SUCCESS);
	DynLeiden *dl = dyn_leiden_init(&g);
	IGRAPH_ASSERT(dl != NULL);

	igraph_vector_int_t batch;
	IGRAPH_ASSERT(igraph_vector_int_init(&batch, 0) == IGRAPH_SUCCESS);

	for (int batch_no = 0; batch_no < 15; batch_no++) {
		igraph_vector_int_clear(&batch);
		for (int e = 0; e < 8; e++) {
			igraph_integer_t u = (igraph_integer_t)(igraph_rng_get_unif01(igraph_rng_default()) * (double)n_vertices);
			igraph_integer_t v = (igraph_integer_t)(igraph_rng_get_unif01(igraph_rng_default()) * (double)n_vertices);
			IGRAPH_ASSERT(igraph_vector_int_push_back(&batch, u) == IGRAPH_SUCCESS);
			IGRAPH_ASSERT(igraph_vector_int_push_back(&batch, v) == IGRAPH_SUCCESS);
		}
		IGRAPH_ASSERT(igraph_add_edges(&g, &batch, NULL) == IGRAPH_SUCCESS);
		IGRAPH_ASSERT(dyn_leiden_on_edges(dl, &g, &batch, NULL));
		IGRAPH_ASSERT(verify_against_fresh_leiden(dl, &g, 0.12));
	}

	IGRAPH_ASSERT(dyn_leiden_community_count(dl) >= 1 && dyn_leiden_community_count(dl) <= n_vertices);

	igraph_vector_int_destroy(&batch);
	dyn_leiden_destroy(dl);
	igraph_destroy(&g);
	return 0;
}

static int test_bootstrap(void)
{
	// Start from an existing graph (bootstrap via igraph_community_leiden_simple),
	// then stream more edges onto it.
	igraph_t g;
	IGRAPH_ASSERT(igraph_famous(&g, "zachary") == IGRAPH_SUCCESS);
	DynLeiden *dl = dyn_leiden_init(&g);
	IGRAPH_ASSERT(dl != NULL);
	IGRAPH_ASSERT(verify_against_fresh_leiden(dl, &g, 0.05));

	static const igraph_integer_t edges[] = {0, 33, 5, 33};
	igraph_vector_int_t batch;
	IGRAPH_ASSERT(igraph_vector_int_init(&batch, 0) == IGRAPH_SUCCESS);
	for (size_t i = 0; i < sizeof(edges) / sizeof(edges[0]); i++)
		IGRAPH_ASSERT(igraph_vector_int_push_back(&batch, edges[i]) == IGRAPH_SUCCESS);
	IGRAPH_ASSERT(igraph_add_edges(&g, &batch, NULL) == IGRAPH_SUCCESS);
	IGRAPH_ASSERT(dyn_leiden_on_edges(dl, &g, &batch, NULL));
	IGRAPH_ASSERT(verify_against_fresh_leiden(dl, &g, 0.05));

	igraph_vector_int_destroy(&batch);
	dyn_leiden_destroy(dl);
	igraph_destroy(&g);
	return 0;
}

static int test_changed_output(void)
{
	igraph_t g;
	IGRAPH_ASSERT(igraph_empty(&g, 0, IGRAPH_UNDIRECTED) == IGRAPH_SUCCESS);
	DynLeiden *dl = dyn_leiden_init(&g);
	IGRAPH_ASSERT(dl != NULL);

	igraph_vector_int_t edges, changed;
	IGRAPH_ASSERT(igraph_vector_int_init(&edges, 0) == IGRAPH_SUCCESS);
	IGRAPH_ASSERT(igraph_vector_int_init(&changed, 0) == IGRAPH_SUCCESS);

	// 10 vertices (not just 2): under CPM the density-scaled gamma for a
	// single edge on 2 vertices is at the 3.0 ceiling, where merging never
	// pays off (gain = 1 - 3 < 0). Padding to 10 vertices drops gamma to
	// ~0.067, so the single edge 0-1 is genuinely worth merging.
	IGRAPH_ASSERT(igraph_vector_int_push_back(&edges, 0) == IGRAPH_SUCCESS);
	IGRAPH_ASSERT(igraph_vector_int_push_back(&edges, 1) == IGRAPH_SUCCESS);
	IGRAPH_ASSERT(igraph_add_vertices(&g, 10, NULL) == IGRAPH_SUCCESS);
	IGRAPH_ASSERT(igraph_add_edges(&g, &edges, NULL) == IGRAPH_SUCCESS);
	IGRAPH_ASSERT(dyn_leiden_on_edges(dl, &g, &edges, &changed));
	IGRAPH_ASSERT(igraph_vector_int_size(&changed) >= 1);
	IGRAPH_ASSERT(dyn_leiden_get(dl, 0) == dyn_leiden_get(dl, 1));

	igraph_vector_int_destroy(&changed);
	igraph_vector_int_destroy(&edges);
	dyn_leiden_destroy(dl);
	igraph_destroy(&g);
	return 0;
}

static int test_pref_attachment_locality(void)
{
	// Locality regression: the largest single-batch frontier should stay a
	// small fraction of the graph, not trend toward the full vertex count,
	// if maintenance is genuinely local (catches an accidental reversion to
	// an O(V) scan).
	igraph_rng_seed(igraph_rng_default(), 42);

	igraph_t full;
	IGRAPH_ASSERT(igraph_barabasi_game(&full, 400, 1.0, 2, NULL, 0, 1.0, IGRAPH_UNDIRECTED, IGRAPH_BARABASI_PSUMTREE, NULL) == IGRAPH_SUCCESS);

	igraph_t g;
	IGRAPH_ASSERT(igraph_empty(&g, 0, IGRAPH_UNDIRECTED) == IGRAPH_SUCCESS);
	DynLeiden *dl = dyn_leiden_init(&g);
	IGRAPH_ASSERT(dl != NULL);

	igraph_integer_t total_edges = igraph_ecount(&full);
	igraph_integer_t total_vertices = igraph_vcount(&full);
	igraph_vector_int_t batch;
	IGRAPH_ASSERT(igraph_vector_int_init(&batch, 0) == IGRAPH_SUCCESS);

	const igraph_integer_t batch_size = 8;
	for (igraph_integer_t e = 0; e < total_edges; e++) {
		igraph_integer_t from, to;
		IGRAPH_ASSERT(igraph_edge(&full, e, &from, &to) == IGRAPH_SUCCESS);
		IGRAPH_ASSERT(igraph_vector_int_push_back(&batch, from) == IGRAPH_SUCCESS);
		IGRAPH_ASSERT(igraph_vector_int_push_back(&batch, to) == IGRAPH_SUCCESS);

		if (igraph_vector_int_size(&batch) / 2 >= batch_size || e + 1 == total_edges) {
			igraph_integer_t maxid = -1;
			for (igraph_integer_t j = 0; j < igraph_vector_int_size(&batch); j++)
				if (VECTOR(batch)[j] > maxid)
					maxid = VECTOR(batch)[j];
			if (maxid >= igraph_vcount(&g))
				IGRAPH_ASSERT(igraph_add_vertices(&g, maxid + 1 - igraph_vcount(&g), NULL) == IGRAPH_SUCCESS);
			IGRAPH_ASSERT(igraph_add_edges(&g, &batch, NULL) == IGRAPH_SUCCESS);
			IGRAPH_ASSERT(dyn_leiden_on_edges(dl, &g, &batch, NULL));
			igraph_vector_int_clear(&batch);
		}
	}

	IGRAPH_ASSERT(verify_against_fresh_leiden(dl, &g, 0.1));

	int max_frontier = dyn_leiden_max_frontier_size(dl);
	fprintf(stderr, "test_pref_attachment_locality: max_frontier=%d of %lld vertices\n", max_frontier, (long long)total_vertices);
	IGRAPH_ASSERT(max_frontier < total_vertices / 2);

	igraph_vector_int_destroy(&batch);
	dyn_leiden_destroy(dl);
	igraph_destroy(&g);
	igraph_destroy(&full);
	return 0;
}

int main(void)
{
	RUN_TEST(test_micro_self_loops);
	RUN_TEST(test_single_batch_clique);
	RUN_TEST(test_two_cliques_bridge);
	RUN_TEST(test_sparse_two_cliques);
	RUN_TEST(test_uniform_random);
	RUN_TEST(test_bootstrap);
	RUN_TEST(test_changed_output);
	RUN_TEST(test_pref_attachment_locality);

	printf("all tests passed\n");
	return EXIT_SUCCESS;
}
