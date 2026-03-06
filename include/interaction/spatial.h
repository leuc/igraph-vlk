#pragma once

#include <cglm/cglm.h>

typedef struct
{
	vec3 origin;
	vec3 front;
	vec3 right;
	vec3 up;
	versor rotation;
} SpatialBasis;

void spatial_calculate_basis(vec3 cam_pos, vec3 cam_front, vec3 cam_up, SpatialBasis *out_basis);

void spatial_resolve_position(const SpatialBasis *basis, float x_offset, float y_offset, float forward_dist, vec3 out_pos);
