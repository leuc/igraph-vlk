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
			spatial_calculate_basis(head_pos, head_fwd, head_up, &menu->spawn_basis);
		} else {
#endif
			spatial_calculate_basis(state->camera.pos, state->camera.front, state->camera.up, &menu->spawn_basis);
#ifdef USE_OPENXR
		}
#endif

		if (state->win.handle) {
			glfwSetInputMode(state->win.handle, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
		}
	} else if (state->app_ctx.current_state == STATE_MENU_OPEN) {
		state->app_ctx.current_state = STATE_GRAPH_VIEW;
		menu->is_open = false;
		// Drop the hover so a click in graph view can't activate the row aimed at when closing.
		menu_hover_clear_recursive(menu->root);
		menu->hovered_node = NULL;
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

	// Only rows the last layout pass actually placed on screen are pickable. Skipping the rest
	// keeps the root (which owns no row) and every stale quad from a collapsed subtree out of
	// the ray test, so a hit always corresponds to something the user can see.
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

// The single picking entry point. Every input source (mouse, gamepad, VR controller) resolves
// its hover through this function, so they can never disagree about which row is under the
// pointer; the caller only supplies the ray its device defines.
static MenuNode *menu_pick_ray(AppState *state, float *ray_ori, float *ray_dir)
{
	menu_hover_clear_recursive(state->app_ctx.menu.root);
	float min_t = FLT_MAX;
	MenuNode *hit = pick_menu_recursive(state->app_ctx.menu.root, ray_ori, ray_dir, &min_t);
	if (hit)
		hit->hovered = true;
	return hit;
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

// Desktop/gamepad pointer. The cursor is captured (GLFW_CURSOR_DISABLED) whenever the menu is
// open — mouse motion steers the camera, so the crosshair *is* the pointer. Picking from
// glfwGetCursorPos() instead would use a free-running virtual coordinate unrelated to what the
// user is aiming at, which is why no input source is allowed to build its own screen-space ray.
MenuNode *raycast_menu_crosshair(AppState *state)
{
	return menu_pick_ray(state, state->camera.pos, state->camera.front);
}

// VR pointer: the ray is the controller pose, supplied by the caller.
MenuNode *raycast_menu_vr(AppState *state, vec3 ray_ori, vec3 ray_dir)
{
	return menu_pick_ray(state, ray_ori, ray_dir);
}
