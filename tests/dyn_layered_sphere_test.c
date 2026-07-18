/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Data-driven correctness tests for graph/dyn_layered_sphere.
 *
 * DynLayeredSphere exposes no internal accessors (only init/on_update/
 * destroy — see graph/dyn_layered_sphere.h), so these tests check the one
 * thing that IS externally observable: the caller-owned layout matrix. A
 * vertex's position is always written as exactly its occupied slot's
 * precomputed point (see dyn_ls_recompute in src/graph/dyn_layered_sphere.c),
 * so "no two vertices share a slot on the same sphere" is externally
 * equivalent to "no two vertices at the same radius occupy the same
 * position" — that's what check_placement_invariants verifies, without
 * needing to reach into the maintainer's internals.
 *
 * fixture_add_batch mirrors graph/stream.c's graph_stream_poll: vertices
 * added first, one igraph_add_edges call per batch, then
 * dyn_core_tree_on_edges / dyn_leiden_on_edges / dyn_layered_sphere_on_update
 * in that order, threading touched_levels/community_changed through to the
 * layout exactly as stream.c does.
 *
 * No benchmarking: timing/throughput belongs in a separate harness.
 */

#include "graph/dyn_core_tree.h"
#include "graph/dyn_layered_sphere.h"
#include "graph/dyn_leiden.h"
#include "test_utilities.h"

#include <math.h>
#include <stdlib.h>

typedef struct
{
	igraph_t g;
	DynCoreTree *ct;
	DynLeiden *dl;
	DynLayeredSphere *dls;
	igraph_matrix_t layout;
} Fixture;

static int fixture_init(Fixture *f)
{
	igraph_set_attribute_table(&igraph_cattribute_table); // required for dyn_layered_sphere's VAN()/igraph_cattribute_has_attr() timestamp lookup; the real app sets this once globally (main.c/stream.c/graph_io.c)
	if (igraph_empty(&f->g, 0, IGRAPH_UNDIRECTED) != IGRAPH_SUCCESS)
		return 0;
	if (igraph_matrix_init(&f->layout, 0, 3) != IGRAPH_SUCCESS)
		return 0;
	f->ct = dyn_core_tree_init(&f->g);
	f->dl = dyn_leiden_init(&f->g);
	f->dls = dyn_layered_sphere_init(&f->g, f->ct, NULL, f->dl ? dyn_leiden_membership(f->dl) : NULL, &f->layout);
	return f->ct && f->dl && f->dls;
}

static void fixture_destroy(Fixture *f)
{
	dyn_layered_sphere_destroy(f->dls);
	dyn_leiden_destroy(f->dl);
	dyn_core_tree_destroy(f->ct);
	igraph_matrix_destroy(&f->layout);
	igraph_destroy(&f->g);
}

static int fixture_add_batch(Fixture *f, const igraph_integer_t *edges, size_t n_edge_ints, igraph_integer_t n_vertices_needed)
{
	igraph_integer_t old_vcount = igraph_vcount(&f->g);
	if (n_vertices_needed > old_vcount)
		IGRAPH_ASSERT(igraph_add_vertices(&f->g, n_vertices_needed - old_vcount, NULL) == IGRAPH_SUCCESS);
	igraph_integer_t new_vcount = igraph_vcount(&f->g);
	if (new_vcount > old_vcount)
		IGRAPH_ASSERT(igraph_matrix_add_rows(&f->layout, new_vcount - old_vcount) == IGRAPH_SUCCESS);

	igraph_vector_int_t batch;
	IGRAPH_ASSERT(igraph_vector_int_init(&batch, 0) == IGRAPH_SUCCESS);
	for (size_t i = 0; i < n_edge_ints; i++)
		IGRAPH_ASSERT(igraph_vector_int_push_back(&batch, edges[i]) == IGRAPH_SUCCESS);
	bool has_edges = n_edge_ints > 0;
	if (has_edges)
		IGRAPH_ASSERT(igraph_add_edges(&f->g, &batch, NULL) == IGRAPH_SUCCESS);

	igraph_vector_int_t touched_levels, community_changed;
	IGRAPH_ASSERT(igraph_vector_int_init(&touched_levels, 0) == IGRAPH_SUCCESS);
	IGRAPH_ASSERT(igraph_vector_int_init(&community_changed, 0) == IGRAPH_SUCCESS);

	IGRAPH_ASSERT(dyn_core_tree_on_edges(f->ct, &f->g, has_edges ? &batch : NULL, &touched_levels));
	IGRAPH_ASSERT(dyn_leiden_on_edges(f->dl, &f->g, has_edges ? &batch : NULL, &community_changed));
	IGRAPH_ASSERT(dyn_layered_sphere_on_update(f->dls, &f->g, f->ct, &touched_levels, NULL, dyn_leiden_membership(f->dl), &community_changed, &f->layout));

	igraph_vector_int_destroy(&community_changed);
	igraph_vector_int_destroy(&touched_levels);
	igraph_vector_int_destroy(&batch);
	return 1;
}

// Every placed vertex must have a finite, nonzero-magnitude position, and no
// two vertices at (approximately) the same radius may occupy (approximately)
// the same position.
static int check_placement_invariants(const igraph_matrix_t *layout)
{
	igraph_integer_t n = igraph_matrix_nrow(layout);
	for (igraph_integer_t i = 0; i < n; i++) {
		double xi = MATRIX(*layout, i, 0), yi = MATRIX(*layout, i, 1), zi = MATRIX(*layout, i, 2);
		double ri = sqrt(xi * xi + yi * yi + zi * zi);
		if (isnan(ri) || isinf(ri) || ri <= 0.0) {
			fprintf(stderr, "check_placement_invariants: vertex %lld has invalid radius %.6f\n", (long long)i, ri);
			return 0;
		}
		for (igraph_integer_t j = i + 1; j < n; j++) {
			double xj = MATRIX(*layout, j, 0), yj = MATRIX(*layout, j, 1), zj = MATRIX(*layout, j, 2);
			double rj = sqrt(xj * xj + yj * yj + zj * zj);
			if (fabs(ri - rj) > 1e-6)
				continue;
			double dx = xi - xj, dy = yi - yj, dz = zi - zj;
			double dist = sqrt(dx * dx + dy * dy + dz * dz);
			if (dist < 1e-6) {
				fprintf(stderr, "check_placement_invariants: vertices %lld and %lld collide at radius %.6f\n", (long long)i, (long long)j, ri);
				return 0;
			}
		}
	}
	return 1;
}

// Number of distinct sphere radii represented in the layout — an externally
// observable proxy for "how many spheres are in use", since every vertex on
// the same sphere shares that sphere's exact radius.
static int count_distinct_radii(const igraph_matrix_t *layout)
{
	igraph_integer_t n = igraph_matrix_nrow(layout);
	double radii[256];
	int count = 0;
	for (igraph_integer_t i = 0; i < n && count < 256; i++) {
		double x = MATRIX(*layout, i, 0), y = MATRIX(*layout, i, 1), z = MATRIX(*layout, i, 2);
		double r = sqrt(x * x + y * y + z * z);
		bool found = false;
		for (int k = 0; k < count; k++)
			if (fabs(radii[k] - r) < 1e-6) {
				found = true;
				break;
			}
		if (!found)
			radii[count++] = r;
	}
	return count;
}

static int test_triangle(void)
{
	Fixture f;
	IGRAPH_ASSERT(fixture_init(&f));

	static const igraph_integer_t edges[] = {0, 1, 1, 2, 2, 0};
	IGRAPH_ASSERT(fixture_add_batch(&f, edges, sizeof(edges) / sizeof(edges[0]), 3));
	IGRAPH_ASSERT(check_placement_invariants(&f.layout));
	// Not asserting an exact sphere/community count here: at this density
	// DynLeiden's density-scaled CPM resolution (see dyn_leiden.c) can just
	// as validly keep all three vertices singleton as merge them, and either
	// outcome must still produce a valid placement. Sphere COUNT, unlike
	// before, no longer depends on Leiden at all (only on distinct coreness
	// levels), but this test only has one level (2) either way.

	fixture_destroy(&f);
	return 0;
}

// Two distinct coreness levels — a closed triangle (coreness 2) plus several
// isolated vertices (coreness 0) — must land in exactly two spheres. Sphere
// assignment is now a strict 1:1 map from populated coreness level to sphere
// (see dyn_layered_sphere.h), so this is guaranteed by construction rather
// than by exceeding some capacity threshold the way the old capacity-packed
// bucketing needed (and unlike test_triangle, this is deterministic
// regardless of how DynLeiden partitions communities, since sphere count no
// longer depends on community structure at all).
static int test_distinct_levels_open_multiple_spheres(void)
{
	Fixture f;
	IGRAPH_ASSERT(fixture_init(&f));

	static const igraph_integer_t triangle[] = {0, 1, 1, 2, 2, 0};
	IGRAPH_ASSERT(fixture_add_batch(&f, triangle, sizeof(triangle) / sizeof(triangle[0]), 3));
	IGRAPH_ASSERT(fixture_add_batch(&f, NULL, 0, 10)); // 7 more isolated (coreness-0) vertices

	IGRAPH_ASSERT(check_placement_invariants(&f.layout));
	IGRAPH_ASSERT(count_distinct_radii(&f.layout) == 2); // level 2 (triangle) + level 0 (isolated)

	fixture_destroy(&f);
	return 0;
}

// Two cliques of very different size (and therefore very different coreness)
// land on different spheres; bridging them with one edge afterward (which
// may or may not make DynLeiden merge their communities, and may or may not
// change either clique's coreness) must not break placement invariants
// either way.
static int test_two_disparate_communities(void)
{
	Fixture f;
	IGRAPH_ASSERT(fixture_init(&f));

	static const igraph_integer_t k3[] = {0, 1, 1, 2, 2, 0};
	IGRAPH_ASSERT(fixture_add_batch(&f, k3, sizeof(k3) / sizeof(k3[0]), 3));

	igraph_integer_t k10[90];
	int k = 0;
	for (igraph_integer_t i = 3; i < 13; i++)
		for (igraph_integer_t j = i + 1; j < 13; j++) {
			k10[k++] = i;
			k10[k++] = j;
		}
	IGRAPH_ASSERT(fixture_add_batch(&f, k10, (size_t)k, 13));
	IGRAPH_ASSERT(check_placement_invariants(&f.layout));

	static const igraph_integer_t bridge[] = {0, 3};
	IGRAPH_ASSERT(fixture_add_batch(&f, bridge, sizeof(bridge) / sizeof(bridge[0]), 13));
	IGRAPH_ASSERT(check_placement_invariants(&f.layout));

	fixture_destroy(&f);
	return 0;
}

// Brand-new vertices with no edges yet (isolated, zero coreness, singleton
// community) must still get a valid, non-colliding placement.
static int test_isolated_new_vertices(void)
{
	Fixture f;
	IGRAPH_ASSERT(fixture_init(&f));

	IGRAPH_ASSERT(fixture_add_batch(&f, NULL, 0, 5));
	IGRAPH_ASSERT(check_placement_invariants(&f.layout));

	fixture_destroy(&f);
	return 0;
}

// Bootstrap from a non-empty graph (the batch/one-shot init path), then
// keep streaming — mirrors dyn_kcore_test.c's test_bootstrap.
static int test_bootstrap_then_stream(void)
{
	igraph_set_attribute_table(&igraph_cattribute_table);
	igraph_t g;
	IGRAPH_ASSERT(igraph_small(&g, 0, IGRAPH_UNDIRECTED, 0, 1, 1, 2, 2, 0, 2, 3, 3, 4, -1) == IGRAPH_SUCCESS);

	DynCoreTree *ct = dyn_core_tree_init(&g);
	DynLeiden *dl = dyn_leiden_init(&g);
	igraph_matrix_t layout;
	IGRAPH_ASSERT(igraph_matrix_init(&layout, igraph_vcount(&g), 3) == IGRAPH_SUCCESS);
	DynLayeredSphere *dls = dyn_layered_sphere_init(&g, ct, NULL, dl ? dyn_leiden_membership(dl) : NULL, &layout);
	IGRAPH_ASSERT(ct && dl && dls);
	IGRAPH_ASSERT(check_placement_invariants(&layout));

	static const igraph_integer_t more[] = {3, 0, 4, 0};
	igraph_vector_int_t batch, touched_levels, community_changed;
	IGRAPH_ASSERT(igraph_vector_int_init(&batch, 0) == IGRAPH_SUCCESS);
	IGRAPH_ASSERT(igraph_vector_int_init(&touched_levels, 0) == IGRAPH_SUCCESS);
	IGRAPH_ASSERT(igraph_vector_int_init(&community_changed, 0) == IGRAPH_SUCCESS);
	for (size_t i = 0; i < sizeof(more) / sizeof(more[0]); i++)
		IGRAPH_ASSERT(igraph_vector_int_push_back(&batch, more[i]) == IGRAPH_SUCCESS);
	IGRAPH_ASSERT(igraph_add_edges(&g, &batch, NULL) == IGRAPH_SUCCESS);

	IGRAPH_ASSERT(dyn_core_tree_on_edges(ct, &g, &batch, &touched_levels));
	IGRAPH_ASSERT(dyn_leiden_on_edges(dl, &g, &batch, &community_changed));
	IGRAPH_ASSERT(dyn_layered_sphere_on_update(dls, &g, ct, &touched_levels, NULL, dyn_leiden_membership(dl), &community_changed, &layout));
	IGRAPH_ASSERT(check_placement_invariants(&layout));

	igraph_vector_int_destroy(&community_changed);
	igraph_vector_int_destroy(&touched_levels);
	igraph_vector_int_destroy(&batch);
	dyn_layered_sphere_destroy(dls);
	dyn_leiden_destroy(dl);
	dyn_core_tree_destroy(ct);
	igraph_matrix_destroy(&layout);
	igraph_destroy(&g);
	return 0;
}

int main(void)
{
	RUN_TEST(test_triangle);
	RUN_TEST(test_distinct_levels_open_multiple_spheres);
	RUN_TEST(test_two_disparate_communities);
	RUN_TEST(test_isolated_new_vertices);
	RUN_TEST(test_bootstrap_then_stream);

	printf("all tests passed\n");
	return EXIT_SUCCESS;
}
