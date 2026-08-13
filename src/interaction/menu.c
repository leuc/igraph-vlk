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
	MenuState *menu = &state->app_ctx.menu;

	if (state->app_ctx.current_state == STATE_GRAPH_VIEW && !menu->is_open) {
		state->app_ctx.current_state = STATE_MENU_OPEN;
		menu->is_open = true;

#ifdef USE_OPENXR
		if (state->vr_enabled) {
			vec3 head_pos, head_fwd, head_up = {0.0f, 1.0f, 0.0f};
			XrVector3f pos = state->xr_ctx.views[0].pose.position;
			head_pos[0] = pos.x + state->vr_play_offset[0];
			head_pos[1] = pos.y + state->vr_play_offset[1];
			head_pos[2] = pos.z + state->vr_play_offset[2];
			XrQuaternionf rot = state->xr_ctx.views[0].pose.orientation;
			mat4 rot_mat;
			glm_quat_mat4((float *)&rot, rot_mat);
			head_fwd[0] = -rot_mat[2][0];
			head_fwd[1] = -rot_mat[2][1];
			head_fwd[2] = -rot_mat[2][2];
			spatial_calculate_basis(head_pos, head_fwd, head_up, &menu->spawn_basis);
		} else {
#endif
			spatial_calculate_basis(state->camera.pos, state->camera.front, state->camera.up, &menu->spawn_basis);
#ifdef USE_OPENXR
		}
#endif
		menu_invalidate(menu, MENU_INVALIDATE_LAYOUT);

		if (state->win.handle) {
			glfwSetInputMode(state->win.handle, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
		}
	} else if (state->app_ctx.current_state == STATE_MENU_OPEN) {
		state->app_ctx.current_state = STATE_GRAPH_VIEW;
		menu->is_open = false;
		menu_set_hovered(menu, NULL);
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
	if (node->is_visible && picking_ray_quad_intersection(ray_ori, ray_dir, node->quad_center_pos, node->right_vec, node->up_vec, node->box_width, node->box_height, &t)) {
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

static MenuNode *menu_pick_ray(AppState *state, float *ray_ori, float *ray_dir)
{
	float min_t = FLT_MAX;
	MenuNode *hit = pick_menu_recursive(state->app_ctx.menu.root, ray_ori, ray_dir, &min_t);
	menu_set_hovered(&state->app_ctx.menu, hit);
	return hit;
}

MenuNode *raycast_menu_crosshair(AppState *state)
{
	return menu_pick_ray(state, state->camera.pos, state->camera.front);
}

MenuNode *raycast_menu_vr(AppState *state, vec3 ray_ori, vec3 ray_dir)
{
	return menu_pick_ray(state, ray_ori, ray_dir);
}
