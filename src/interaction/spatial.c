#include "interaction/spatial.h"
#include <string.h>

void spatial_calculate_basis(vec3 cam_pos, vec3 cam_front, vec3 cam_up, SpatialBasis *out_basis)
{
	glm_vec3_copy(cam_pos, out_basis->origin);
	glm_vec3_copy(cam_front, out_basis->front);

	glm_vec3_cross(cam_front, cam_up, out_basis->right);
	glm_vec3_normalize(out_basis->right);
	glm_vec3_cross(out_basis->right, cam_front, out_basis->up);
	glm_vec3_normalize(out_basis->up);

	mat3 rot_mat;
	vec3 neg_front;
	glm_vec3_negate_to(cam_front, neg_front);
	glm_mat3_identity(rot_mat);
	glm_mat3_copy((mat3){{out_basis->right[0], out_basis->right[1], out_basis->right[2]}, {out_basis->up[0], out_basis->up[1], out_basis->up[2]}, {neg_front[0], neg_front[1], neg_front[2]}}, rot_mat);

	glm_mat3_quat(rot_mat, out_basis->rotation);
}

void spatial_resolve_position(const SpatialBasis *basis, float x_offset, float y_offset, float forward_dist, vec3 out_pos)
{
	SpatialBasis b;
	memcpy(&b, basis, sizeof(SpatialBasis));

	vec3 world_pos;
	glm_vec3_copy(b.origin, world_pos);

	vec3 f_part, r_part, u_part;
	glm_vec3_scale(b.front, forward_dist, f_part);
	glm_vec3_scale(b.right, x_offset, r_part);
	glm_vec3_scale(b.up, y_offset, u_part);

	glm_vec3_add(world_pos, f_part, world_pos);
	glm_vec3_add(world_pos, r_part, world_pos);
	glm_vec3_add(world_pos, u_part, world_pos);

	glm_vec3_copy(world_pos, out_pos);
}
