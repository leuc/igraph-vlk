/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "interaction/picking.h"
#include "test_utilities.h"

#include <math.h>

#define FEQ(a, b) (fabsf((a) - (b)) < 1e-4f)

// interaction_pick_object() (not under test) calls renderer_update_graph();
// stub it out so this test executable doesn't need to link the Vulkan renderer.
void renderer_update_graph(Renderer *r, GraphData *graph)
{
	(void)r;
	(void)graph;
}

static int test_sphere_hit_through_center(void)
{
	float ori[3] = {0.0f, 0.0f, -5.0f};
	float dir[3] = {0.0f, 0.0f, 1.0f};
	float center[3] = {0.0f, 0.0f, 0.0f};
	float t;
	IGRAPH_ASSERT(picking_ray_sphere_intersection(ori, dir, center, 1.0f, &t));
	IGRAPH_ASSERT(FEQ(t, 4.0f)); // enters sphere at z = -1, distance 4 from ori
	return 0;
}

static int test_sphere_miss(void)
{
	float ori[3] = {0.0f, 5.0f, -5.0f};
	float dir[3] = {0.0f, 0.0f, 1.0f};
	float center[3] = {0.0f, 0.0f, 0.0f};
	float t;
	IGRAPH_ASSERT(!picking_ray_sphere_intersection(ori, dir, center, 1.0f, &t));
	return 0;
}

static int test_sphere_tangent(void)
{
	float ori[3] = {0.0f, 1.0f, -5.0f};
	float dir[3] = {0.0f, 0.0f, 1.0f};
	float center[3] = {0.0f, 0.0f, 0.0f};
	float t;
	IGRAPH_ASSERT(picking_ray_sphere_intersection(ori, dir, center, 1.0f, &t));
	IGRAPH_ASSERT(FEQ(t, 5.0f));
	return 0;
}

static int test_sphere_behind_ray_origin_still_reports_hit(void)
{
	// Documents actual (not "should-be") behavior: the function does not
	// clamp t >= 0. Callers (interaction_pick_object) filter t > 0 themselves.
	float ori[3] = {0.0f, 0.0f, 5.0f};
	float dir[3] = {0.0f, 0.0f, 1.0f};
	float center[3] = {0.0f, 0.0f, 0.0f};
	float t;
	IGRAPH_ASSERT(picking_ray_sphere_intersection(ori, dir, center, 1.0f, &t));
	IGRAPH_ASSERT(t < 0.0f);
	return 0;
}

static int test_segment_dist_through_midpoint(void)
{
	float ori[3] = {0.0f, 0.0f, -5.0f};
	float dir[3] = {0.0f, 0.0f, 1.0f};
	float p1[3] = {-1.0f, 0.0f, 0.0f};
	float p2[3] = {1.0f, 0.0f, 0.0f};
	float t;
	float dist = picking_dist_ray_segment(ori, dir, p1, p2, &t);
	IGRAPH_ASSERT(FEQ(dist, 0.0f));
	IGRAPH_ASSERT(FEQ(t, 5.0f));
	return 0;
}

static int test_segment_dist_parallel_offset(void)
{
	float ori[3] = {0.0f, 2.0f, -5.0f};
	float dir[3] = {0.0f, 0.0f, 1.0f};
	float p1[3] = {-1.0f, 0.0f, 0.0f};
	float p2[3] = {1.0f, 0.0f, 0.0f};
	float t;
	float dist = picking_dist_ray_segment(ori, dir, p1, p2, &t);
	IGRAPH_ASSERT(FEQ(dist, 2.0f));
	return 0;
}

static int test_segment_dist_clamped_to_endpoint(void)
{
	// Ray's closest approach to the *infinite line* falls outside [0,1] on
	// the segment; sc should clamp to the nearest endpoint (p2, sc=1) rather
	// than extrapolate past it.
	float ori[3] = {5.0f, 0.0f, -5.0f};
	float dir[3] = {0.0f, 0.0f, 1.0f};
	float p1[3] = {-1.0f, 0.0f, 0.0f};
	float p2[3] = {1.0f, 0.0f, 0.0f};
	float t;
	float dist = picking_dist_ray_segment(ori, dir, p1, p2, &t);
	// closest point on ray line is (5,0,0); nearest clamped segment point is p2=(1,0,0)
	IGRAPH_ASSERT(FEQ(dist, 4.0f));
	return 0;
}

static int test_segment_dist_near_parallel_fallback(void)
{
	// Ray parallel to the segment (D < 1e-6): exercises the sc=0 fallback branch.
	float ori[3] = {0.0f, 1.0f, 0.0f};
	float dir[3] = {1.0f, 0.0f, 0.0f};
	float p1[3] = {0.0f, 0.0f, 0.0f};
	float p2[3] = {1.0f, 0.0f, 0.0f};
	float t;
	float dist = picking_dist_ray_segment(ori, dir, p1, p2, &t);
	IGRAPH_ASSERT(FEQ(dist, 1.0f));
	return 0;
}

static int test_quad_hit_center(void)
{
	vec3 ray_ori = {0.0f, 0.0f, -5.0f};
	vec3 ray_dir = {0.0f, 0.0f, 1.0f};
	vec3 quad_center = {0.0f, 0.0f, 0.0f};
	vec3 right = {1.0f, 0.0f, 0.0f};
	vec3 up = {0.0f, 1.0f, 0.0f};
	float t;
	IGRAPH_ASSERT(picking_ray_quad_intersection(ray_ori, ray_dir, quad_center, right, up, 2.0f, 2.0f, &t));
	IGRAPH_ASSERT(FEQ(t, 5.0f));
	return 0;
}

static int test_quad_miss_outside_bounds(void)
{
	vec3 ray_ori = {5.0f, 5.0f, -5.0f};
	vec3 ray_dir = {0.0f, 0.0f, 1.0f};
	vec3 quad_center = {0.0f, 0.0f, 0.0f};
	vec3 right = {1.0f, 0.0f, 0.0f};
	vec3 up = {0.0f, 1.0f, 0.0f};
	float t;
	IGRAPH_ASSERT(!picking_ray_quad_intersection(ray_ori, ray_dir, quad_center, right, up, 2.0f, 2.0f, &t));
	return 0;
}

static int test_quad_parallel_ray_misses(void)
{
	vec3 ray_ori = {0.0f, 0.0f, -5.0f};
	vec3 ray_dir = {1.0f, 0.0f, 0.0f}; // parallel to the quad's plane (normal is +Z)
	vec3 quad_center = {0.0f, 0.0f, 0.0f};
	vec3 right = {1.0f, 0.0f, 0.0f};
	vec3 up = {0.0f, 1.0f, 0.0f};
	float t;
	IGRAPH_ASSERT(!picking_ray_quad_intersection(ray_ori, ray_dir, quad_center, right, up, 2.0f, 2.0f, &t));
	return 0;
}

static int test_quad_behind_ray_origin_misses(void)
{
	vec3 ray_ori = {0.0f, 0.0f, 5.0f};
	vec3 ray_dir = {0.0f, 0.0f, 1.0f}; // pointing away from the quad at z=0
	vec3 quad_center = {0.0f, 0.0f, 0.0f};
	vec3 right = {1.0f, 0.0f, 0.0f};
	vec3 up = {0.0f, 1.0f, 0.0f};
	float t;
	IGRAPH_ASSERT(!picking_ray_quad_intersection(ray_ori, ray_dir, quad_center, right, up, 2.0f, 2.0f, &t));
	return 0;
}

int main(void)
{
	RUN_TEST(test_sphere_hit_through_center);
	RUN_TEST(test_sphere_miss);
	RUN_TEST(test_sphere_tangent);
	RUN_TEST(test_sphere_behind_ray_origin_still_reports_hit);
	RUN_TEST(test_segment_dist_through_midpoint);
	RUN_TEST(test_segment_dist_parallel_offset);
	RUN_TEST(test_segment_dist_clamped_to_endpoint);
	RUN_TEST(test_segment_dist_near_parallel_fallback);
	RUN_TEST(test_quad_hit_center);
	RUN_TEST(test_quad_miss_outside_bounds);
	RUN_TEST(test_quad_parallel_ray_misses);
	RUN_TEST(test_quad_behind_ray_origin_misses);
	return 0;
}
