#include "interaction/menu.h"
#include "interaction/picking.h"
#include <GLFW/glfw3.h>
#include <cglm/cglm.h>
#include <float.h>
#include <string.h>

static MenuNode *pick_menu_recursive(MenuNode *node, float *ray_ori, float *ray_dir, float *min_t)
{
	if (node == NULL || node->current_radius < 0.1f)
		return NULL;

	MenuNode *best_hit = NULL;

	float box_width = node->box_width * node->current_radius;
	float box_height = node->box_height * node->current_radius;

	float t;
	if (picking_ray_quad_intersection(ray_ori, ray_dir, node->quad_center_pos, node->right_vec, node->up_vec, box_width, box_height, &t)) {
		if (t > 0 && t < *min_t) {
			*min_t = t;
			best_hit = node;
		}
	}

	for (int i = 0; i < node->num_children; i++) {
		MenuNode *child_hit = pick_menu_recursive(node->children[i], ray_ori, ray_dir, min_t);
		if (child_hit) {
			best_hit = child_hit;
		}
	}

	return best_hit;
}

void clear_menu_hover_recursive(MenuNode *node)
{
	if (!node)
		return;
	node->hovered = false;
	for (int i = 0; i < node->num_children; i++) {
		clear_menu_hover_recursive(node->children[i]);
	}
}

void clear_menu_focus_recursive(MenuNode *node)
{
	if (!node)
		return;
	node->is_focused = false;
	for (int i = 0; i < node->num_children; i++) {
		clear_menu_focus_recursive(node->children[i]);
	}
}

MenuNode *find_focused_node(MenuNode *node)
{
	if (!node)
		return NULL;
	if (node->is_focused)
		return node;
	for (int i = 0; i < node->num_children; i++) {
		MenuNode *found = find_focused_node(node->children[i]);
		if (found)
			return found;
	}
	return NULL;
}

void set_menu_node_focused(MenuNode *root, MenuNode *target)
{
	clear_menu_focus_recursive(root);
	if (target)
		target->is_focused = true;
}

MenuNode *raycast_menu_crosshair(AppState *state)
{
	clear_menu_hover_recursive(state->app_ctx.root_menu);
	float min_t = FLT_MAX;
	MenuNode *hit = pick_menu_recursive(state->app_ctx.root_menu, state->camera.pos, state->camera.front, &min_t);
	if (hit)
		hit->hovered = true;
	return hit;
}

MenuNode *interaction_pick_menu_node(AppState *state, double mouse_x, double mouse_y)
{
	float x = (2.0f * (float)mouse_x) / state->win_w - 1.0f;
	float y = 1.0f - (2.0f * (float)mouse_y) / state->win_h;

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

bool handle_menu_key_input(MenuNode *root, int key, int scancode, int action, int mods)
{
	MenuNode *focused = find_focused_node(root);
	if (!focused || action != GLFW_PRESS)
		return false;

	if (focused->type == NODE_INPUT_TEXT) {
		if (key == GLFW_KEY_BACKSPACE) {
			size_t len = strlen(focused->input_buffer);
			if (len > 0) {
				focused->input_buffer[len - 1] = '\0';
			}
			return true;
		} else if (key == GLFW_KEY_ENTER || key == GLFW_KEY_ESCAPE) {
			focused->is_focused = false;
			return true;
		} else if (key >= GLFW_KEY_SPACE && key <= GLFW_KEY_WORLD_2) {
			char c = (char)key;
			if (c >= ' ' && c <= '~') {
				size_t len = strlen(focused->input_buffer);
				if (len < sizeof(focused->input_buffer) - 1) {
					focused->input_buffer[len] = c;
					focused->input_buffer[len + 1] = '\0';
				}
			}
			return true;
		}
	}

	return false;
}