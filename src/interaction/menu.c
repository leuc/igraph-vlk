/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "interaction/menu.h"
#include "interaction/picking.h"
#include "interaction/spatial.h"
#include <GLFW/glfw3.h>
#include <cglm/cglm.h>
#include <float.h>
#include <string.h>

void interaction_menu_toggle(AppState *state)
{
	if (state->app_ctx.current_state == STATE_GRAPH_VIEW && state->app_ctx.root_menu->current_radius < 0.01f) {
		state->app_ctx.current_state = STATE_MENU_OPEN;
		state->app_ctx.root_menu->target_radius = 1.0f;

#ifdef USE_OPENXR
		if (state->vr_enabled) {
			// Get headset position and forward vector
			vec3 head_pos, head_fwd, head_up = {0.0f, 1.0f, 0.0f};
			// We use the first eye's pose as the headset reference, plus play offset
			XrVector3f pos = state->xr_ctx.views[0].pose.position;
			head_pos[0] = pos.x + state->vr_play_offset[0];
			head_pos[1] = pos.y + state->vr_play_offset[1];
			head_pos[2] = pos.z + state->vr_play_offset[2];
			// Extract forward from the view pose orientation
			XrQuaternionf rot = state->xr_ctx.views[0].pose.orientation;
			mat4 rot_mat;
			glm_quat_mat4((float *)&rot, rot_mat);
			head_fwd[0] = -rot_mat[2][0];
			head_fwd[1] = -rot_mat[2][1];
			head_fwd[2] = -rot_mat[2][2];
			spatial_calculate_basis(head_pos, head_fwd, head_up, &state->app_ctx.menu_spawn_basis);
		} else {
#endif
			spatial_calculate_basis(state->camera.pos, state->camera.front, state->camera.up, &state->app_ctx.menu_spawn_basis);
#ifdef USE_OPENXR
		}
#endif

		if (state->win.handle) {
			glfwSetInputMode(state->win.handle, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
		}
	} else if (state->app_ctx.current_state == STATE_MENU_OPEN || state->app_ctx.root_menu->current_radius > 0.99f) {
		state->app_ctx.current_state = STATE_GRAPH_VIEW;
		state->app_ctx.root_menu->target_radius = 0.0f;
		if (state->win.handle) {
			glfwSetInputMode(state->win.handle, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
		}
	}
}

static MenuNode *pick_menu_recursive(MenuNode *node, float *ray_ori, float *ray_dir, float *min_t)
{
	if (node == NULL)
		return NULL;

	MenuNode *best_hit = NULL;

	float t;
	if (picking_ray_quad_intersection(ray_ori, ray_dir, node->quad_center_pos, node->right_vec, node->up_vec, node->box_width, node->box_height, &t)) {
		if (t > 0 && t < *min_t) {
			*min_t = t;
			best_hit = node;
		}
	}

	if (node->is_expanded) {
		for (int i = 0; i < node->num_children; i++) {
			MenuNode *child_hit = pick_menu_recursive(node->children[i], ray_ori, ray_dir, min_t);
			if (child_hit) {
				best_hit = child_hit;
			}
		}
	}

	return best_hit;
}

void menu_hover_clear_recursive(MenuNode *node)
{
	if (!node)
		return;
	node->hovered = false;
	for (int i = 0; i < node->num_children; i++) {
		menu_hover_clear_recursive(node->children[i]);
	}
}

MenuNode *raycast_menu_crosshair(AppState *state)
{
	menu_hover_clear_recursive(state->app_ctx.root_menu);
	float min_t = FLT_MAX;
	MenuNode *hit = pick_menu_recursive(state->app_ctx.root_menu, state->camera.pos, state->camera.front, &min_t);
	if (hit)
		hit->hovered = true;
	return hit;
}

MenuNode *raycast_menu_vr(AppState *state, vec3 ray_ori, vec3 ray_dir)
{
	menu_hover_clear_recursive(state->app_ctx.root_menu);
	float min_t = FLT_MAX;
	MenuNode *hit = pick_menu_recursive(state->app_ctx.root_menu, ray_ori, ray_dir, &min_t);
	if (hit) {
		printf("HIT: %s at t=%.2f\n", hit->label, min_t);
		hit->hovered = true;
	}
	return hit;
}

MenuNode *interaction_pick_menu_node(AppState *state, double mouse_x, double mouse_y)
{
	float x = (2.0f * (float)mouse_x) / state->win.w - 1.0f;
	float y = 1.0f - (2.0f * (float)mouse_y) / state->win.h;

	vec3 ray_dir;
	vec3 right, up;
	glm_vec3_cross(state->camera.front, state->camera.up, right);
	glm_vec3_normalize(right);
	glm_vec3_cross(right, state->camera.front, up);
	glm_vec3_normalize(up);

	glm_vec3_copy(state->camera.front, ray_dir);
	vec3 right_offset, up_offset;
	glm_vec3_scale(right, x * 0.5f, right_offset);
	glm_vec3_scale(up, y * 0.5f, up_offset);
	glm_vec3_add(ray_dir, right_offset, ray_dir);
	glm_vec3_add(ray_dir, up_offset, ray_dir);
	glm_vec3_normalize(ray_dir);

	float min_t = FLT_MAX;
	return pick_menu_recursive(state->app_ctx.root_menu, state->camera.pos, ray_dir, &min_t);
}
