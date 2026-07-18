/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for graph/layered_sphere_common: the geometry/comparator/grid
 * helpers extracted from graph/layered_sphere.c. Covers the pure math
 * helpers directly and exercises the stateful (LayeredSphereContext-based)
 * helpers with small, hand-built contexts where the expected geometry can be
 * computed by hand.
 */

#include "graph/layered_sphere_common.h"
#include "test_utilities.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define TOL 1e-6

static int dbl_close(double a, double b, double tol)
{
	return fabs(a - b) <= tol;
}

static int test_geodesic_distance(void)
{
	// Same point: distance 0.
	IGRAPH_ASSERT(dbl_close(geodesic_distance(1, 0, 0, 1, 0, 0, 5.0), 0.0, TOL));
	// Antipodal points: distance = radius * pi.
	IGRAPH_ASSERT(dbl_close(geodesic_distance(1, 0, 0, -1, 0, 0, 2.0), 2.0 * M_PI, TOL));
	// Orthogonal points: distance = radius * pi/2.
	IGRAPH_ASSERT(dbl_close(geodesic_distance(1, 0, 0, 0, 1, 0, 3.0), 3.0 * M_PI / 2.0, TOL));
	// Clamping: a dot product that drifts slightly above 1.0 due to fp error
	// must not make acos() return NaN.
	double d = geodesic_distance(1.0, 0.0, 0.0, 1.0 + 1e-16, 0.0, 0.0, 1.0);
	IGRAPH_ASSERT(dbl_close(d, 0.0, TOL));
	return 0;
}

static int test_hilbert_curve(void)
{
	// xy2d must be a bijection onto [0, n*n) for an n x n grid.
	int n = 8;
	int seen[64];
	memset(seen, 0, sizeof(seen));
	for (int x = 0; x < n; x++) {
		for (int y = 0; y < n; y++) {
			int d = xy2d(n, x, y);
			IGRAPH_ASSERT(d >= 0 && d < n * n);
			IGRAPH_ASSERT(seen[d] == 0);
			seen[d] = 1;
		}
	}
	for (int i = 0; i < n * n; i++)
		IGRAPH_ASSERT(seen[i] == 1);

	// Corner is always distance 0 for the standard Hilbert construction.
	IGRAPH_ASSERT(xy2d(n, 0, 0) == 0);
	return 0;
}

static int test_compare_communities_kcore(void)
{
	CommData comms[3] = {
		{.comm_id = 0, .avg_kcore = 1.0, .node_count = 1},
		{.comm_id = 1, .avg_kcore = 5.0, .node_count = 1},
		{.comm_id = 2, .avg_kcore = 3.0, .node_count = 1},
	};
	qsort(comms, 3, sizeof(CommData), compare_communities_kcore);
	// Descending by avg_kcore.
	IGRAPH_ASSERT(comms[0].comm_id == 1);
	IGRAPH_ASSERT(comms[1].comm_id == 2);
	IGRAPH_ASSERT(comms[2].comm_id == 0);
	return 0;
}

// Pins the greedy bucketing formula shared by the batch (layered_sphere.c
// PHASE_INIT) and streaming (dyn_layered_sphere.c) paths: nucleus capacity =
// first community's own size, sphere s>0 capacity = base_capacity * s^2 with
// base_capacity = max(15, remaining * 0.015), zero-count communities skipped.
// Input must already be sorted by avg_kcore descending (the callers qsort
// with compare_communities_kcore first).
static int test_bucket_communities_into_spheres(void)
{
	// Nucleus exactly fits community A; B overflows into sphere 1; C and E
	// still fit sphere 1 (capacity 15); D is empty and must be skipped.
	CommData comms1[5] = {
		{.comm_id = 0, .avg_kcore = 9.0, .node_count = 5},	// A -> sphere 0 (nucleus, capacity 5)
		{.comm_id = 1, .avg_kcore = 5.0, .node_count = 3},	// B -> sphere 1 (5+3 > 5)
		{.comm_id = 2, .avg_kcore = 3.0, .node_count = 2},	// C -> sphere 1 (3+2 <= 15)
		{.comm_id = 3, .avg_kcore = 1.0, .node_count = 10}, // E -> sphere 1 (5+10 <= 15)
		{.comm_id = 4, .avg_kcore = 0.0, .node_count = 0},	// D -> skipped entirely
	};
	int comm_to_sphere1[5] = {-99, -99, -99, -99, -99};
	IGRAPH_ASSERT(bucket_communities_into_spheres(comms1, 5, 20, comm_to_sphere1) == 2);
	IGRAPH_ASSERT(comm_to_sphere1[0] == 0);
	IGRAPH_ASSERT(comm_to_sphere1[1] == 1);
	IGRAPH_ASSERT(comm_to_sphere1[2] == 1);
	IGRAPH_ASSERT(comm_to_sphere1[3] == 1);
	IGRAPH_ASSERT(comm_to_sphere1[4] == -99); // empty community never written

	// Quadratic capacity growth: sphere 1 caps at base_capacity (15), sphere 2
	// at base_capacity * 4 (60), so the third big community overflows sphere 1
	// but the fourth still fits sphere 2 alongside it.
	CommData comms2[4] = {
		{.comm_id = 0, .avg_kcore = 9.0, .node_count = 4},	// -> sphere 0
		{.comm_id = 1, .avg_kcore = 8.0, .node_count = 10}, // -> sphere 1 (4+10 > 4)
		{.comm_id = 2, .avg_kcore = 7.0, .node_count = 10}, // -> sphere 2 (10+10 > 15)
		{.comm_id = 3, .avg_kcore = 6.0, .node_count = 10}, // -> sphere 2 (10+10 <= 60)
	};
	int comm_to_sphere2[4];
	IGRAPH_ASSERT(bucket_communities_into_spheres(comms2, 4, 34, comm_to_sphere2) == 3);
	IGRAPH_ASSERT(comm_to_sphere2[0] == 0);
	IGRAPH_ASSERT(comm_to_sphere2[1] == 1);
	IGRAPH_ASSERT(comm_to_sphere2[2] == 2);
	IGRAPH_ASSERT(comm_to_sphere2[3] == 2);

	// Degenerate single community: everything on the nucleus, one sphere.
	CommData comms3[1] = {
		{.comm_id = 0, .avg_kcore = 2.0, .node_count = 7},
	};
	int comm_to_sphere3[1];
	IGRAPH_ASSERT(bucket_communities_into_spheres(comms3, 1, 7, comm_to_sphere3) == 1);
	IGRAPH_ASSERT(comm_to_sphere3[0] == 0);

	return 0;
}

static int test_compare_nodes_placement(void)
{
	NodePlacement nodes[4] = {
		{.id = 0, .community_id = 1, .density = 0.5, .intra_degree = 2},
		{.id = 1, .community_id = 0, .density = 0.1, .intra_degree = 9},
		{.id = 2, .community_id = 1, .density = 0.2, .intra_degree = 5},
		{.id = 3, .community_id = 1, .density = 0.2, .intra_degree = 5},
	};
	qsort(nodes, 4, sizeof(NodePlacement), compare_nodes_placement);
	// community_id ascending first.
	IGRAPH_ASSERT(nodes[0].community_id == 0);
	IGRAPH_ASSERT(nodes[0].id == 1);
	// Within community 1: intra_degree descending, then density ascending;
	// nodes 2 and 3 tie on both, so any relative order between them is fine,
	// but both must come before node 0 (lower intra_degree).
	IGRAPH_ASSERT(nodes[1].community_id == 1 && nodes[1].intra_degree == 5);
	IGRAPH_ASSERT(nodes[2].community_id == 1 && nodes[2].intra_degree == 5);
	IGRAPH_ASSERT(nodes[3].community_id == 1 && nodes[3].id == 0);
	return 0;
}

static int test_compare_points(void)
{
	SpherePoint pts[3] = {
		{.x = 0, .y = 0, .z = 0, .hilbert_dist = 30},
		{.x = 0, .y = 0, .z = 0, .hilbert_dist = 10},
		{.x = 0, .y = 0, .z = 0, .hilbert_dist = 20},
	};
	qsort(pts, 3, sizeof(SpherePoint), compare_points);
	IGRAPH_ASSERT(pts[0].hilbert_dist == 10);
	IGRAPH_ASSERT(pts[1].hilbert_dist == 20);
	IGRAPH_ASSERT(pts[2].hilbert_dist == 30);
	return 0;
}

static int test_get_vector_int_max(void)
{
	igraph_vector_int_t v;
	IGRAPH_ASSERT(igraph_vector_int_init(&v, 0) == IGRAPH_SUCCESS);
	IGRAPH_ASSERT(igraph_vector_int_push_back(&v, 3) == IGRAPH_SUCCESS);
	IGRAPH_ASSERT(igraph_vector_int_push_back(&v, 7) == IGRAPH_SUCCESS);
	IGRAPH_ASSERT(igraph_vector_int_push_back(&v, 2) == IGRAPH_SUCCESS);
	IGRAPH_ASSERT(get_vector_int_max(&v) == 7);
	igraph_vector_int_destroy(&v);

	// All-zero (or empty) input maps to 1, matching the "at least one
	// community" convention used by the PHASE_INIT sphere-assignment code.
	igraph_vector_int_t z;
	IGRAPH_ASSERT(igraph_vector_int_init(&z, 3) == IGRAPH_SUCCESS);
	IGRAPH_ASSERT(get_vector_int_max(&z) == 1);
	igraph_vector_int_destroy(&z);

	return 0;
}

static int test_find_closest_slot_by_hilbert(void)
{
	SpherePoint pts[5] = {
		{.hilbert_dist = 0}, {.hilbert_dist = 10}, {.hilbert_dist = 20}, {.hilbert_dist = 30}, {.hilbert_dist = 40},
	};
	SphereGrid grid = {.max_slots = 5, .slots = pts};

	IGRAPH_ASSERT(find_closest_slot_by_hilbert(&grid, 0) == 0);
	IGRAPH_ASSERT(find_closest_slot_by_hilbert(&grid, 40) == 4);
	IGRAPH_ASSERT(find_closest_slot_by_hilbert(&grid, 21) == 2); // closer to 20
	IGRAPH_ASSERT(find_closest_slot_by_hilbert(&grid, 26) == 3); // closer to 30
	IGRAPH_ASSERT(find_closest_slot_by_hilbert(&grid, 25) == 3); // tie -> higher index wins (strict '<' favors the upper bound)
	return 0;
}

static int test_sphere_radius_for(void)
{
	// Sphere 0: radius is fmax(5.0, needed_r) regardless of prev_radius.
	double required_area = 10 * 40.0;
	double needed_r0 = sqrt(required_area / (4.0 * M_PI));
	IGRAPH_ASSERT(dbl_close(sphere_radius_for(0, 10, 999.0), fmax(5.0, needed_r0), TOL));

	// A tiny group at sphere 0 falls back to the 5.0 floor.
	IGRAPH_ASSERT(dbl_close(sphere_radius_for(0, 1, 0.0), 5.0, TOL));

	// Sphere > 0: fmax(prev_radius + log_gap, needed_r).
	double prev = 20.0;
	double log_gap = 8.0 + (20.0 / log2(1 + 2.0));
	double expected = fmax(prev + log_gap, sqrt((5 * 40.0) / (4.0 * M_PI)));
	IGRAPH_ASSERT(dbl_close(sphere_radius_for(1, 5, prev), expected, TOL));
	return 0;
}

static int test_sphere_slot_count(void)
{
	// Small group/radius: dominated by the n*3 floor.
	IGRAPH_ASSERT(sphere_slot_count(10, 1.0) == 30);
	// Mid radius: dominated by the area-based term (still under the cap).
	int expected = (int)((4.0 * M_PI * 50.0 * 50.0) / 20.0);
	IGRAPH_ASSERT(sphere_slot_count(10, 50.0) == expected);
	// Clamped to 100000 for very large radii.
	IGRAPH_ASSERT(sphere_slot_count(10, 1000.0) == 100000);
	return 0;
}

static int test_compute_slot_point(void)
{
	int M_s = 100;
	double radius = 7.0;
	for (int i = 0; i < M_s; i += 7) {
		SpherePoint p;
		compute_slot_point(i, M_s, radius, HILBERT_RES, &p);
		double r2 = p.x * p.x + p.y * p.y + p.z * p.z;
		IGRAPH_ASSERT(dbl_close(sqrt(r2), radius, 1e-6));
		IGRAPH_ASSERT(p.hilbert_dist >= 0 && p.hilbert_dist < HILBERT_RES * HILBERT_RES);
	}
	return 0;
}

static int test_build_sphere_grid(void)
{
	SphereGrid grid;
	memset(&grid, 0, sizeof(grid));
	IGRAPH_ASSERT(build_sphere_grid(&grid, 20, 15.0, HILBERT_RES) == true);

	IGRAPH_ASSERT(grid.radius == 15.0);
	IGRAPH_ASSERT(grid.max_slots == sphere_slot_count(20, 15.0));
	IGRAPH_ASSERT(grid.slots != NULL);
	IGRAPH_ASSERT(grid.slot_occupant != NULL);

	for (int k = 0; k < grid.max_slots; k++)
		IGRAPH_ASSERT(grid.slot_occupant[k] == -1);

	// Slots are sorted ascending by hilbert_dist.
	for (int k = 1; k < grid.max_slots; k++)
		IGRAPH_ASSERT(grid.slots[k].hilbert_dist >= grid.slots[k - 1].hilbert_dist);

	// Scratch neighbor vector was initialized and is usable/empty.
	IGRAPH_ASSERT(igraph_vector_int_size(&grid.neis) == 0);

	free(grid.slots);
	free(grid.slot_occupant);
	igraph_vector_int_destroy(&grid.neis);
	return 0;
}

static int test_advance_phase(void)
{
	LayeredSphereContext ctx;
	memset(&ctx, 0, sizeof(ctx));

	// Intra phase, moves happened, under the iteration cap: stay in intra,
	// phase_iter increments.
	ctx.phase = PHASE_INTRA_SPHERE;
	ctx.phase_iter = 0;
	advance_phase(&ctx, /*local_moves=*/3);
	IGRAPH_ASSERT(ctx.phase == PHASE_INTRA_SPHERE);
	IGRAPH_ASSERT(ctx.phase_iter == 1);

	// Intra phase, no moves: transition to inter, phase_iter resets.
	ctx.phase = PHASE_INTRA_SPHERE;
	ctx.phase_iter = 5;
	advance_phase(&ctx, /*local_moves=*/0);
	IGRAPH_ASSERT(ctx.phase == PHASE_INTER_SPHERE);
	IGRAPH_ASSERT(ctx.phase_iter == 0);

	// Intra phase, moves happened but over the iteration cap: still forced
	// into inter phase.
	ctx.phase = PHASE_INTRA_SPHERE;
	ctx.phase_iter = MAX_INTRA_ITERS + 1;
	advance_phase(&ctx, /*local_moves=*/3);
	IGRAPH_ASSERT(ctx.phase == PHASE_INTER_SPHERE);
	IGRAPH_ASSERT(ctx.phase_iter == 0);

	// Inter phase, moves happened: stays in inter, both counters advance.
	ctx.phase = PHASE_INTER_SPHERE;
	ctx.phase_iter = 0;
	ctx.inter_sphere_pass = 0;
	advance_phase(&ctx, /*local_moves=*/1);
	IGRAPH_ASSERT(ctx.phase == PHASE_INTER_SPHERE);
	IGRAPH_ASSERT(ctx.phase_iter == 1);
	IGRAPH_ASSERT(ctx.inter_sphere_pass == 1);

	// Inter phase, no moves, but inter_sphere_pass becomes odd after
	// increment: not done yet (the "both halves settled" check needs an
	// even pass).
	ctx.phase = PHASE_INTER_SPHERE;
	ctx.phase_iter = 0;
	ctx.inter_sphere_pass = 0;
	advance_phase(&ctx, /*local_moves=*/0);
	IGRAPH_ASSERT(ctx.phase == PHASE_INTER_SPHERE);
	IGRAPH_ASSERT(ctx.inter_sphere_pass == 1);

	// Inter phase, no moves, inter_sphere_pass becomes even after
	// increment: done.
	ctx.phase = PHASE_INTER_SPHERE;
	ctx.phase_iter = 0;
	ctx.inter_sphere_pass = 1;
	advance_phase(&ctx, /*local_moves=*/0);
	IGRAPH_ASSERT(ctx.phase == PHASE_DONE);

	// Inter phase, over the iteration cap: forced done regardless of moves.
	ctx.phase = PHASE_INTER_SPHERE;
	ctx.phase_iter = MAX_INTER_ITERS + 1;
	ctx.inter_sphere_pass = 0;
	advance_phase(&ctx, /*local_moves=*/5);
	IGRAPH_ASSERT(ctx.phase == PHASE_DONE);

	return 0;
}

// Build a minimal single-sphere context for 3 nodes, all in sphere 0.
static void init_single_sphere_ctx(LayeredSphereContext *ctx, igraph_matrix_t *layout, int vcount)
{
	memset(ctx, 0, sizeof(*ctx));
	ctx->vcount = vcount;
	ctx->num_spheres = 1;
	ctx->phase = PHASE_INTRA_SPHERE;
	IGRAPH_ASSERT(igraph_matrix_init(layout, vcount, 3) == IGRAPH_SUCCESS);
	ctx->layout = layout;
	ctx->node_to_sphere_id = malloc(vcount * sizeof(int));
	ctx->node_to_slot_idx = malloc(vcount * sizeof(int));
	for (int i = 0; i < vcount; i++)
		ctx->node_to_sphere_id[i] = 0;
	ctx->grids = calloc(1, sizeof(SphereGrid));
}

static int test_placement_and_seed_slots(void)
{
	LayeredSphereContext ctx;
	igraph_matrix_t layout;
	init_single_sphere_ctx(&ctx, &layout, 3);

	IGRAPH_ASSERT(build_sphere_grid(&ctx.grids[0], 3, 10.0, HILBERT_RES) == true);

	igraph_vector_int_t membership;
	igraph_vector_t transitivity;
	IGRAPH_ASSERT(igraph_vector_int_init(&membership, 3) == IGRAPH_SUCCESS);
	IGRAPH_ASSERT(igraph_vector_init(&transitivity, 3) == IGRAPH_SUCCESS);
	// Node 0: community 1, low intra_degree -> placed last.
	// Node 1: community 0 -> placed first (community ascending).
	// Node 2: community 1, high intra_degree -> placed before node 0.
	VECTOR(membership)[0] = 1;
	VECTOR(membership)[1] = 0;
	VECTOR(membership)[2] = 1;
	VECTOR(transitivity)[0] = 0.5;
	VECTOR(transitivity)[1] = 0.1;
	VECTOR(transitivity)[2] = 0.2;
	int intra_degree[3] = {1, 9, 5};

	int placed_count = -1;
	NodePlacement *grp = placement_order_for_sphere(&ctx, 0, 3, &membership, &transitivity, intra_degree, &placed_count);
	IGRAPH_ASSERT(placed_count == 3);
	IGRAPH_ASSERT(grp[0].id == 1);
	IGRAPH_ASSERT(grp[1].id == 2);
	IGRAPH_ASSERT(grp[2].id == 0);

	seed_slots_for_sphere(&ctx, 0, grp, placed_count);

	int M_s = ctx.grids[0].max_slots;
	int step = M_s / 3;
	for (int i = 0; i < 3; i++) {
		int nid = grp[i].id;
		int expected_slot = (int)fmin(M_s - 1, i * step);
		IGRAPH_ASSERT(ctx.node_to_slot_idx[nid] == expected_slot);
		IGRAPH_ASSERT(ctx.grids[0].slot_occupant[expected_slot] == nid);
		IGRAPH_ASSERT(dbl_close(MATRIX(layout, nid, 0), ctx.grids[0].slots[expected_slot].x, TOL));
		IGRAPH_ASSERT(dbl_close(MATRIX(layout, nid, 1), ctx.grids[0].slots[expected_slot].y, TOL));
		IGRAPH_ASSERT(dbl_close(MATRIX(layout, nid, 2), ctx.grids[0].slots[expected_slot].z, TOL));
	}

	free(grp);
	igraph_vector_int_destroy(&membership);
	igraph_vector_destroy(&transitivity);
	free(ctx.node_to_sphere_id);
	free(ctx.node_to_slot_idx);
	free(ctx.grids[0].slots);
	free(ctx.grids[0].slot_occupant);
	igraph_vector_int_destroy(&ctx.grids[0].neis);
	free(ctx.grids);
	igraph_matrix_destroy(&layout);
	return 0;
}

// calculate_move_delta_intra: moving u toward a neighbor's direction must
// reduce the geodesic sum, and moving it away must increase it.
static int test_calculate_move_delta_intra(void)
{
	igraph_t g;
	// u=0, v=1 (target-slot occupant, isolated), w=2 (u's only neighbor).
	IGRAPH_ASSERT(igraph_small(&g, 3, IGRAPH_UNDIRECTED, 0, 2, -1) == IGRAPH_SUCCESS);

	igraph_matrix_t layout;
	IGRAPH_ASSERT(igraph_matrix_init(&layout, 3, 3) == IGRAPH_SUCCESS);
	// w sits exactly at the target slot's direction.
	MATRIX(layout, 2, 0) = 0.0;
	MATRIX(layout, 2, 1) = 10.0;
	MATRIX(layout, 2, 2) = 0.0;

	LayeredSphereContext ctx;
	memset(&ctx, 0, sizeof(ctx));
	ctx.vcount = 3;
	ctx.layout = &layout;
	ctx.node_to_sphere_id = malloc(3 * sizeof(int));
	ctx.node_to_slot_idx = malloc(3 * sizeof(int));
	for (int i = 0; i < 3; i++)
		ctx.node_to_sphere_id[i] = 0;
	ctx.num_spheres = 1;
	ctx.grids = calloc(1, sizeof(SphereGrid));

	SpherePoint pts[2] = {
		{.x = 10.0, .y = 0.0, .z = 0.0, .hilbert_dist = 0}, // u's current slot
		{.x = 0.0, .y = 10.0, .z = 0.0, .hilbert_dist = 1}, // target slot (v here)
	};
	ctx.grids[0].radius = 10.0;
	ctx.grids[0].max_slots = 2;
	ctx.grids[0].slots = pts;
	int occ[2] = {0, 1};
	ctx.grids[0].slot_occupant = occ;

	ctx.node_to_slot_idx[0] = 0; // u at slot 0
	ctx.node_to_slot_idx[1] = 1; // v at slot 1 (target)

	double delta = calculate_move_delta_intra(&g, &layout, &ctx, /*u=*/0, /*target_sphere_s=*/0, /*target_slot_k=*/1);
	// Moving u from (1,0,0) to (0,1,0) direction takes it from a quarter
	// circle away from w to exactly on top of w.
	double expected = 0.0 - (10.0 * M_PI / 2.0);
	IGRAPH_ASSERT(dbl_close(delta, expected, 1e-4));
	IGRAPH_ASSERT(delta < -0.001); // this move would be accepted

	free(ctx.node_to_sphere_id);
	free(ctx.node_to_slot_idx);
	free(ctx.grids);
	igraph_matrix_destroy(&layout);
	igraph_destroy(&g);
	return 0;
}

// calculate_move_delta_inter: same idea but across two spheres of different
// radii, using the averaged radius for the geodesic distance.
static int test_calculate_move_delta_inter(void)
{
	igraph_t g;
	IGRAPH_ASSERT(igraph_small(&g, 3, IGRAPH_UNDIRECTED, 0, 2, -1) == IGRAPH_SUCCESS);

	igraph_matrix_t layout;
	IGRAPH_ASSERT(igraph_matrix_init(&layout, 3, 3) == IGRAPH_SUCCESS);
	MATRIX(layout, 2, 0) = 0.0;
	MATRIX(layout, 2, 1) = 20.0;
	MATRIX(layout, 2, 2) = 0.0;

	LayeredSphereContext ctx;
	memset(&ctx, 0, sizeof(ctx));
	ctx.vcount = 3;
	ctx.layout = &layout;
	ctx.node_to_sphere_id = malloc(3 * sizeof(int));
	ctx.node_to_slot_idx = malloc(3 * sizeof(int));
	ctx.node_to_sphere_id[0] = 0; // u lives in sphere 0
	ctx.node_to_sphere_id[1] = 1; // v (target occupant) lives in sphere 1
	ctx.node_to_sphere_id[2] = 1; // w irrelevant for inter (no sphere filter)
	ctx.num_spheres = 2;
	ctx.grids = calloc(2, sizeof(SphereGrid));

	SpherePoint u_slot = {.x = 10.0, .y = 0.0, .z = 0.0, .hilbert_dist = 0};
	ctx.grids[0].radius = 10.0;
	ctx.grids[0].max_slots = 1;
	ctx.grids[0].slots = &u_slot;
	int occ0[1] = {0};
	ctx.grids[0].slot_occupant = occ0;

	SpherePoint target_slot_pt = {.x = 0.0, .y = 20.0, .z = 0.0, .hilbert_dist = 0};
	ctx.grids[1].radius = 20.0;
	ctx.grids[1].max_slots = 1;
	ctx.grids[1].slots = &target_slot_pt;
	int occ1[1] = {1};
	ctx.grids[1].slot_occupant = occ1;

	ctx.node_to_slot_idx[0] = 0;
	ctx.node_to_slot_idx[1] = 0;

	double delta = calculate_move_delta_inter(&g, &layout, &ctx, /*u=*/0, /*target_sphere_s=*/1, /*target_slot_k=*/0);
	double avg_radius = (10.0 + 20.0) / 2.0;
	double expected = 0.0 - (avg_radius * M_PI / 2.0);
	IGRAPH_ASSERT(dbl_close(delta, expected, 1e-4));

	free(ctx.node_to_sphere_id);
	free(ctx.node_to_slot_idx);
	free(ctx.grids);
	igraph_matrix_destroy(&layout);
	igraph_destroy(&g);
	return 0;
}

static int test_try_move_node(void)
{
	igraph_t g;
	IGRAPH_ASSERT(igraph_small(&g, 3, IGRAPH_UNDIRECTED, 0, 2, -1) == IGRAPH_SUCCESS);

	igraph_matrix_t layout;
	IGRAPH_ASSERT(igraph_matrix_init(&layout, 3, 3) == IGRAPH_SUCCESS);
	MATRIX(layout, 2, 0) = 0.0;
	MATRIX(layout, 2, 1) = 10.0;
	MATRIX(layout, 2, 2) = 0.0;

	LayeredSphereContext ctx;
	memset(&ctx, 0, sizeof(ctx));
	ctx.vcount = 3;
	ctx.layout = &layout;
	ctx.node_to_sphere_id = malloc(3 * sizeof(int));
	ctx.node_to_slot_idx = malloc(3 * sizeof(int));
	for (int i = 0; i < 3; i++)
		ctx.node_to_sphere_id[i] = 0;
	ctx.num_spheres = 1;
	ctx.grids = calloc(1, sizeof(SphereGrid));

	SpherePoint pts[2] = {
		{.x = 10.0, .y = 0.0, .z = 0.0, .hilbert_dist = 0},
		{.x = 0.0, .y = 10.0, .z = 0.0, .hilbert_dist = 1},
	};
	ctx.grids[0].radius = 10.0;
	ctx.grids[0].max_slots = 2;
	ctx.grids[0].slots = pts;
	int occ[2] = {0, 1};
	ctx.grids[0].slot_occupant = occ;
	ctx.node_to_slot_idx[0] = 0;
	ctx.node_to_slot_idx[1] = 1;
	MATRIX(layout, 0, 0) = pts[0].x;
	MATRIX(layout, 0, 1) = pts[0].y;
	MATRIX(layout, 0, 2) = pts[0].z;
	MATRIX(layout, 1, 0) = pts[1].x;
	MATRIX(layout, 1, 1) = pts[1].y;
	MATRIX(layout, 1, 2) = pts[1].z;

	int local_moves = 0;
	// Moving u (0) to the target slot (1) improves its distance to w: the
	// swap must happen and occupants/positions/local_moves all update.
	try_move_node(&g, &layout, &ctx, /*u=*/0, /*s=*/0, /*target_slot=*/1, /*current_slot=*/0, /*is_intra=*/true, &local_moves);

	IGRAPH_ASSERT(local_moves == 1);
	IGRAPH_ASSERT(ctx.grids[0].slot_occupant[1] == 0); // u now at target slot
	IGRAPH_ASSERT(ctx.grids[0].slot_occupant[0] == 1); // v swapped into u's old slot
	IGRAPH_ASSERT(ctx.node_to_slot_idx[0] == 1);
	IGRAPH_ASSERT(ctx.node_to_slot_idx[1] == 0);
	IGRAPH_ASSERT(dbl_close(MATRIX(layout, 0, 1), 10.0, TOL)); // u's coords updated
	IGRAPH_ASSERT(dbl_close(MATRIX(layout, 1, 0), 10.0, TOL)); // v's coords updated

	free(ctx.node_to_sphere_id);
	free(ctx.node_to_slot_idx);
	free(ctx.grids);
	igraph_matrix_destroy(&layout);
	igraph_destroy(&g);
	return 0;
}

static int test_node_hilbert_target_no_neighbors(void)
{
	igraph_t g;
	// Vertex 0 isolated, vertex 1 present but disconnected.
	IGRAPH_ASSERT(igraph_empty(&g, 2, IGRAPH_UNDIRECTED) == IGRAPH_SUCCESS);

	LayeredSphereContext ctx;
	igraph_matrix_t layout;
	init_single_sphere_ctx(&ctx, &layout, 2);
	IGRAPH_ASSERT(build_sphere_grid(&ctx.grids[0], 2, 10.0, HILBERT_RES) == true);
	ctx.node_to_slot_idx[0] = 3;
	ctx.node_to_slot_idx[1] = 5;

	// A node with no neighbors must keep its current slot, regardless of
	// phase or damping.
	int target = node_hilbert_target(&g, &layout, &ctx, /*u=*/0, /*s=*/0, /*is_intra=*/true, /*damping_factor=*/0.4, HILBERT_RES);
	IGRAPH_ASSERT(target == 3);

	free(ctx.node_to_sphere_id);
	free(ctx.node_to_slot_idx);
	free(ctx.grids[0].slots);
	free(ctx.grids[0].slot_occupant);
	igraph_vector_int_destroy(&ctx.grids[0].neis);
	free(ctx.grids);
	igraph_matrix_destroy(&layout);
	igraph_destroy(&g);
	return 0;
}

static int test_node_hilbert_target_cross_sphere_ignored_when_intra(void)
{
	igraph_t g;
	IGRAPH_ASSERT(igraph_small(&g, 2, IGRAPH_UNDIRECTED, 0, 1, -1) == IGRAPH_SUCCESS);

	LayeredSphereContext ctx;
	igraph_matrix_t layout;
	init_single_sphere_ctx(&ctx, &layout, 2);
	// Put node 1 in a different sphere than node 0, but only ever populate
	// grid 0 (node 1's grid is irrelevant to this intra-only lookup).
	ctx.node_to_sphere_id[0] = 0;
	ctx.node_to_sphere_id[1] = 7;
	IGRAPH_ASSERT(build_sphere_grid(&ctx.grids[0], 2, 10.0, HILBERT_RES) == true);
	ctx.node_to_slot_idx[0] = 2;

	// In intra mode, u's only neighbor lives in a different sphere, so it is
	// filtered out entirely: neighbor_count stays 0 and the slot is unchanged.
	int target = node_hilbert_target(&g, &layout, &ctx, /*u=*/0, /*s=*/0, /*is_intra=*/true, /*damping_factor=*/0.4, HILBERT_RES);
	IGRAPH_ASSERT(target == 2);

	free(ctx.node_to_sphere_id);
	free(ctx.node_to_slot_idx);
	free(ctx.grids[0].slots);
	free(ctx.grids[0].slot_occupant);
	igraph_vector_int_destroy(&ctx.grids[0].neis);
	free(ctx.grids);
	igraph_matrix_destroy(&layout);
	igraph_destroy(&g);
	return 0;
}

int main(void)
{
	RUN_TEST(test_geodesic_distance);
	RUN_TEST(test_hilbert_curve);
	RUN_TEST(test_compare_communities_kcore);
	RUN_TEST(test_bucket_communities_into_spheres);
	RUN_TEST(test_compare_nodes_placement);
	RUN_TEST(test_compare_points);
	RUN_TEST(test_get_vector_int_max);
	RUN_TEST(test_find_closest_slot_by_hilbert);
	RUN_TEST(test_sphere_radius_for);
	RUN_TEST(test_sphere_slot_count);
	RUN_TEST(test_compute_slot_point);
	RUN_TEST(test_build_sphere_grid);
	RUN_TEST(test_advance_phase);
	RUN_TEST(test_placement_and_seed_slots);
	RUN_TEST(test_calculate_move_delta_intra);
	RUN_TEST(test_calculate_move_delta_inter);
	RUN_TEST(test_try_move_node);
	RUN_TEST(test_node_hilbert_target_no_neighbors);
	RUN_TEST(test_node_hilbert_target_cross_sphere_ignored_when_intra);

	printf("all tests passed\n");
	return EXIT_SUCCESS;
}
