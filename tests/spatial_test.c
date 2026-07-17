/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "interaction/spatial.h"
#include "test_utilities.h"

#include <math.h>

#define FEQ(a, b) (fabsf((a) - (b)) < 1e-5f)

static int test_calculate_basis_axis_aligned(void)
{
	vec3 cam_pos = {1.0f, 2.0f, 3.0f};
	vec3 cam_front = {0.0f, 0.0f, -1.0f};
	vec3 cam_up = {0.0f, 1.0f, 0.0f};
	SpatialBasis basis;

	spatial_calculate_basis(cam_pos, cam_front, cam_up, &basis);

	IGRAPH_ASSERT(FEQ(basis.origin[0], 1.0f) && FEQ(basis.origin[1], 2.0f) && FEQ(basis.origin[2], 3.0f));
	IGRAPH_ASSERT(FEQ(basis.front[0], 0.0f) && FEQ(basis.front[1], 0.0f) && FEQ(basis.front[2], -1.0f));

	// right/up must be unit length
	float right_len = sqrtf(basis.right[0] * basis.right[0] + basis.right[1] * basis.right[1] + basis.right[2] * basis.right[2]);
	float up_len = sqrtf(basis.up[0] * basis.up[0] + basis.up[1] * basis.up[1] + basis.up[2] * basis.up[2]);
	IGRAPH_ASSERT(FEQ(right_len, 1.0f));
	IGRAPH_ASSERT(FEQ(up_len, 1.0f));

	// right/up orthogonal to front, and to each other
	float dot_right_front = basis.right[0] * basis.front[0] + basis.right[1] * basis.front[1] + basis.right[2] * basis.front[2];
	float dot_up_front = basis.up[0] * basis.front[0] + basis.up[1] * basis.front[1] + basis.up[2] * basis.front[2];
	float dot_right_up = basis.right[0] * basis.up[0] + basis.right[1] * basis.up[1] + basis.right[2] * basis.up[2];
	IGRAPH_ASSERT(FEQ(dot_right_front, 0.0f));
	IGRAPH_ASSERT(FEQ(dot_up_front, 0.0f));
	IGRAPH_ASSERT(FEQ(dot_right_up, 0.0f));

	// right = cross(front, up) normalized = cross((0,0,-1),(0,1,0)) = (1,0,0)
	IGRAPH_ASSERT(FEQ(basis.right[0], 1.0f) && FEQ(basis.right[1], 0.0f) && FEQ(basis.right[2], 0.0f));
	// up = cross(right, front) normalized = cross((1,0,0),(0,0,-1)) = (0,1,0)
	IGRAPH_ASSERT(FEQ(basis.up[0], 0.0f) && FEQ(basis.up[1], 1.0f) && FEQ(basis.up[2], 0.0f));

	// rotation quaternion must be unit length
	float qlen = sqrtf(basis.rotation[0] * basis.rotation[0] + basis.rotation[1] * basis.rotation[1] + basis.rotation[2] * basis.rotation[2] + basis.rotation[3] * basis.rotation[3]);
	IGRAPH_ASSERT(FEQ(qlen, 1.0f));

	return 0;
}

static int test_calculate_basis_degenerate_up(void)
{
	// cam_up parallel to cam_front: cross(front, up) degenerates towards
	// the zero vector. The function does not guard against this input;
	// document the actual behavior (right/up collapse near zero) rather
	// than asserting a "correct" result.
	vec3 cam_pos = {0.0f, 0.0f, 0.0f};
	vec3 cam_front = {0.0f, 0.0f, -1.0f};
	vec3 cam_up = {0.0f, 0.0f, -1.0f};
	SpatialBasis basis;

	spatial_calculate_basis(cam_pos, cam_front, cam_up, &basis);

	float right_len = sqrtf(basis.right[0] * basis.right[0] + basis.right[1] * basis.right[1] + basis.right[2] * basis.right[2]);
	// glm_vec3_normalize on a zero vector leaves it as zero (no NaN), so
	// right stays degenerate (near-zero length) instead of unit length.
	IGRAPH_ASSERT(right_len < 1e-5f);

	return 0;
}

static int test_resolve_position_zero_offsets(void)
{
	SpatialBasis basis;
	vec3 origin = {5.0f, 6.0f, 7.0f};
	vec3 front = {0.0f, 0.0f, -1.0f};
	vec3 right = {1.0f, 0.0f, 0.0f};
	vec3 up = {0.0f, 1.0f, 0.0f};
	glm_vec3_copy(origin, basis.origin);
	glm_vec3_copy(front, basis.front);
	glm_vec3_copy(right, basis.right);
	glm_vec3_copy(up, basis.up);

	vec3 out;
	spatial_resolve_position(&basis, 0.0f, 0.0f, 0.0f, out);

	IGRAPH_ASSERT(FEQ(out[0], 5.0f) && FEQ(out[1], 6.0f) && FEQ(out[2], 7.0f));
	return 0;
}

static int test_resolve_position_with_offsets(void)
{
	SpatialBasis basis;
	vec3 origin = {0.0f, 0.0f, 0.0f};
	vec3 front = {0.0f, 0.0f, -1.0f};
	vec3 right = {1.0f, 0.0f, 0.0f};
	vec3 up = {0.0f, 1.0f, 0.0f};
	glm_vec3_copy(origin, basis.origin);
	glm_vec3_copy(front, basis.front);
	glm_vec3_copy(right, basis.right);
	glm_vec3_copy(up, basis.up);

	vec3 out;
	// x_offset=2 (right), y_offset=3 (up), forward_dist=4 (front)
	spatial_resolve_position(&basis, 2.0f, 3.0f, 4.0f, out);

	// expected = origin + front*4 + right*2 + up*3 = (0,0,-4) + (2,0,0) + (0,3,0) = (2,3,-4)
	IGRAPH_ASSERT(FEQ(out[0], 2.0f) && FEQ(out[1], 3.0f) && FEQ(out[2], -4.0f));
	return 0;
}

int main(void)
{
	RUN_TEST(test_calculate_basis_axis_aligned);
	RUN_TEST(test_calculate_basis_degenerate_up);
	RUN_TEST(test_resolve_position_zero_offsets);
	RUN_TEST(test_resolve_position_with_offsets);
	return 0;
}
