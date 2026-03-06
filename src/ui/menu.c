#include "ui/menu.h"
#include "graph/command_registry.h"
#include "vulkan/text.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

extern FontAtlas globalAtlas;

// Helper function to create a menu node
static MenuNode *create_menu_node(const char *label, MenuNodeType type)
{
	MenuNode *node = (MenuNode *)malloc(sizeof(MenuNode));
	node->label = strdup(label);
	node->type = type;
	node->icon_texture_id = -1;
	node->target_phi = 0.0f;
	node->target_theta = 0.0f;
	node->current_radius = 0.0f;
	node->target_radius = 0.0f;
	node->num_children = 0;
	node->children = NULL;
	node->command = NULL;
	node->is_expanded = false;
	node->hovered = false;
	node->is_focused = false;
	node->toggle_state = false;
	node->info_value = NULL;
	node->card_width = 0.0f;
	node->card_height = 0.0f;
	memset(node->input_buffer, 0, sizeof(node->input_buffer));
	return node;
}

static IgraphCommand *create_command(const char *id_name, const char *display_name, IgraphWrapperFunc execute, int num_params)
{
	IgraphCommand *cmd = (IgraphCommand *)malloc(sizeof(IgraphCommand));
	cmd->id_name = strdup(id_name);
	cmd->display_name = strdup(display_name);
	cmd->icon_path = NULL;
	cmd->execute = execute;
	cmd->num_params = num_params;
	cmd->params = (CommandParameter *)malloc(sizeof(CommandParameter) * num_params);
	cmd->produces_visual_output = false;
	cmd->cmd_def = NULL;
	return cmd;
}

static void assign_menu_icons(MenuNode *node);

void init_menu_tree(MenuNode *root)
{
	root->label = strdup("Main");
	root->type = NODE_BRANCH;
	root->icon_texture_id = -1;
	root->target_phi = 0.0f;
	root->target_theta = 0.0f;
	root->current_radius = 0.0f;
	root->target_radius = 1.0f;
	root->num_children = 0;
	root->children = NULL;
	root->command = NULL;
	root->is_expanded = true;
	root->hovered = false;
	root->is_focused = false;
	root->toggle_state = false;
	root->info_value = NULL;
	root->card_width = 0.0f;
	root->card_height = 0.0f;
	memset(root->input_buffer, 0, sizeof(root->input_buffer));

	for (int i = 0; i < g_command_registry_size; i++) {
		const CommandDef *cmd_def = &g_command_registry[i];
		MenuNode *current_parent = root;

		if (cmd_def->category_path && strlen(cmd_def->category_path) > 0) {
			char path_copy[256];
			strncpy(path_copy, cmd_def->category_path, sizeof(path_copy) - 1);
			path_copy[255] = '\0';

			char *token = strtok(path_copy, "/");
			while (token != NULL) {
				MenuNode *branch = NULL;

				for (int c = 0; c < current_parent->num_children; c++) {
					if (current_parent->children[c]->type == NODE_BRANCH && strcmp(current_parent->children[c]->label, token) == 0) {
						branch = current_parent->children[c];
						break;
					}
				}

				if (branch == NULL) {
					branch = create_menu_node(token, NODE_BRANCH);
					current_parent->children = (MenuNode **)realloc(current_parent->children, sizeof(MenuNode *) * (current_parent->num_children + 1));
					current_parent->children[current_parent->num_children] = branch;
					current_parent->num_children++;
				}

				current_parent = branch;
				token = strtok(NULL, "/");
			}
		}

		MenuNode *leaf = create_menu_node(cmd_def->display_name, NODE_LEAF_COMMAND);
		leaf->command = create_command(cmd_def->command_id, cmd_def->display_name, NULL, 0);
		leaf->command->cmd_def = cmd_def;

		current_parent->children = (MenuNode **)realloc(current_parent->children, sizeof(MenuNode *) * (current_parent->num_children + 1));
		current_parent->children[current_parent->num_children] = leaf;
		current_parent->num_children++;
	}

	assign_menu_icons(root);
}

void destroy_menu_tree(MenuNode *node)
{
	if (node == NULL)
		return;
	if (node->type == NODE_BRANCH) {
		for (int i = 0; i < node->num_children; i++) {
			destroy_menu_tree(node->children[i]);
		}
		if (node->children)
			free(node->children);
	}

	if (node->label)
		free((void *)node->label);

	if (node->command) {
		if (node->command->id_name)
			free((void *)node->command->id_name);
		if (node->command->display_name)
			free((void *)node->command->display_name);
		if (node->command->icon_path)
			free((void *)node->command->icon_path);
		if (node->command->params)
			free(node->command->params);
		free(node->command);
	}
}

static float measure_text_width(const char *text)
{
	if (!text)
		return 0.0f;
	float world_text_scale = 0.003f;
	float total_w = 0.0f;
	int len = strlen(text);
	for (int i = 0; i < len; i++) {
		unsigned char c = text[i];
		CharInfo *ci = (c < 128) ? &globalAtlas.chars[c] : &globalAtlas.chars[32];
		total_w += ci->xadvance;
	}
	return (total_w * world_text_scale);
}

static void calculate_card_dimensions(MenuNode *node)
{
	if (!node)
		return;

	const float item_height = 0.09f;
	const float padding = 0.08f;
	const float title_bar_height = 0.10f;

	if (node->type == NODE_BRANCH && node->num_children > 0) {
		float max_child_width = 0.0f;
		float total_content_height = 0.0f;

		for (int i = 0; i < node->num_children; i++) {
			MenuNode *child = node->children[i];
			calculate_card_dimensions(child);

			float child_width = child->card_width;
			if (child_width > max_child_width)
				max_child_width = child_width;

			if (child->current_radius > 0.01f) {
				total_content_height += item_height * child->current_radius;
			}
		}

		float title_width = measure_text_width(node->label);
		node->card_width = fmaxf(max_child_width, title_width) + padding * 2.0f;
		node->card_height = title_bar_height + total_content_height + padding;
	} else {
		node->card_width = measure_text_width(node->label) + padding * 2.0f;
		node->card_height = item_height;
	}

	for (int i = 0; i < node->num_children; i++) {
		calculate_card_dimensions(node->children[i]);
	}
}

static void update_nextstep_layout_recursive(MenuNode *node, float delta_time, const vec3 parent_card_origin, float parent_top_y)
{
	if (!node)
		return;

	float speed = 8.0f;
	float diff = node->target_radius - node->current_radius;
	if (fabsf(diff) < 0.001f) {
		node->current_radius = node->target_radius;
	} else {
		node->current_radius += diff * speed * delta_time;
	}

	const float item_height = 0.09f;
	const float title_bar_height = 0.10f;

	node->target_phi = 0.0f;
	node->target_theta = 0.0f;

	if (node->type == NODE_BRANCH && node->num_children > 0) {
		vec3 card_pos;
		glm_vec3_copy(parent_card_origin, card_pos);
		glm_vec3_copy(card_pos, node->card_bg_pos);

		float current_y = parent_top_y - title_bar_height;

		for (int i = 0; i < node->num_children; i++) {
			MenuNode *child = node->children[i];

			child->target_radius = (node->current_radius > 0.5f && node->is_expanded) ? 1.0f : 0.0f;

			if (node->current_radius > 0.001f) {
				vec3 child_pos;
				glm_vec3_copy(parent_card_origin, child_pos);

				float x_offset = node->card_width * 0.5f + 0.02f;
				vec3 right_offset;
				glm_vec3_scale(node->right_vec, x_offset, right_offset);
				glm_vec3_add(child_pos, right_offset, child_pos);

				float y_offset = current_y - (item_height * 0.5f);
				vec3 up_offset;
				glm_vec3_scale(node->up_vec, y_offset, up_offset);
				glm_vec3_add(child_pos, up_offset, child_pos);

				glm_vec3_copy(child_pos, child->text_anchor_pos);
				glm_vec3_copy(child_pos, child->quad_center_pos);
				glm_vec3_copy(child_pos, child->world_pos);

				current_y -= item_height * child->current_radius;

				vec3 subcard_origin;
				glm_vec3_copy(parent_card_origin, subcard_origin);
				vec3 sub_offset;
				glm_vec3_scale(node->right_vec, node->card_width, sub_offset);
				glm_vec3_add(subcard_origin, sub_offset, subcard_origin);

				update_nextstep_layout_recursive(child, delta_time, subcard_origin, parent_top_y);
			} else {
				child->current_radius = 0.0f;
				child->target_radius = 0.0f;
			}
		}
	} else {
		glm_vec3_copy(parent_card_origin, node->text_anchor_pos);
		glm_vec3_copy(parent_card_origin, node->quad_center_pos);
		glm_vec3_copy(parent_card_origin, node->world_pos);

		node->box_width = node->card_width;
		node->box_height = node->card_height;
	}
}

void update_menu_animation(MenuNode *node, float delta_time)
{
	if (node == NULL)
		return;

	calculate_card_dimensions(node);

	vec3 origin = {0.0f, 0.0f, 2.5f};
	update_nextstep_layout_recursive(node, delta_time, origin, 0.0f);
}

void update_menu_transforms(MenuNode *node, const SpatialBasis *basis)
{
	if (node == NULL)
		return;

	MenuNode **stack = (MenuNode **)malloc(sizeof(MenuNode *) * 256);
	int stack_top = 0;
	stack[stack_top++] = node;

	while (stack_top > 0) {
		MenuNode *current = stack[--stack_top];
		if (current == NULL)
			continue;

		memcpy(current->right_vec, basis->right, sizeof(vec3));
		memcpy(current->up_vec, basis->up, sizeof(vec3));
		memcpy(current->rotation, basis->rotation, sizeof(versor));

		if (current->current_radius > 0.01f) {
			vec3 offset;
			glm_vec3_scale(current->right_vec, current->card_width * 0.5f, offset);
			glm_vec3_add(current->text_anchor_pos, offset, current->quad_center_pos);

			glm_vec3_copy(current->quad_center_pos, current->world_pos);

			glm_vec3_copy(current->text_anchor_pos, current->card_bg_pos);
			vec3 card_offset;
			glm_vec3_scale(current->right_vec, current->card_width * 0.5f, card_offset);
			glm_vec3_add(current->card_bg_pos, card_offset, current->card_bg_pos);
		}

		for (int i = 0; i < current->num_children; i++) {
			stack[stack_top++] = current->children[i];
		}
	}

	free(stack);
}

MenuNode *find_menu_node(MenuNode *root, const char *label)
{
	if (root == NULL || label == NULL)
		return NULL;
	if (strcmp(root->label, label) == 0)
		return root;
	for (int i = 0; i < root->num_children; i++) {
		MenuNode *res = find_menu_node(root->children[i], label);
		if (res)
			return res;
	}
	return NULL;
}

static void assign_menu_icons(MenuNode *node)
{
	if (node == NULL)
		return;
	static int next_icon_id = 0;
	node->icon_texture_id = next_icon_id++;
	for (int i = 0; i < node->num_children; i++) {
		assign_menu_icons(node->children[i]);
	}
}