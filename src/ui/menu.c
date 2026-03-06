#include "ui/menu.h"
#include "graph/command_registry.h"
#include "vulkan/text.h"
#include <igraph.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

extern FontAtlas globalAtlas;

const float MENU_ITEM_HEIGHT = 0.09f;
const float TITLE_BAR_HEIGHT = 0.10f;
const float TEXT_PADDING = 0.05f;

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
	memset(node->info_buffer, 0, sizeof(node->info_buffer));
	node->poll_info = NULL;
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

		MenuNodeType type = (cmd_def->node_type != 0) ? cmd_def->node_type : NODE_LEAF_COMMAND;
		MenuNode *leaf = create_menu_node(cmd_def->display_name, type);

		if (type == NODE_INFO_DISPLAY) {
			leaf->poll_info = cmd_def->poll_func;
			memset(leaf->info_buffer, 0, sizeof(leaf->info_buffer));
			leaf->info_value = leaf->info_buffer;
		} else {
			leaf->command = create_command(cmd_def->command_id, cmd_def->display_name, NULL, 0);
			leaf->command->cmd_def = cmd_def;
		}

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

	if (node->type == NODE_BRANCH && node->num_children > 0) {
		float max_width = measure_text_width(node->label);

		for (int i = 0; i < node->num_children; i++) {
			MenuNode *child = node->children[i];
			float child_w = measure_text_width(child->label);

			if (child->type == NODE_INFO_DISPLAY && child->info_value) {
				float info_w = measure_text_width(child->info_value);
				if (info_w > child_w)
					child_w = info_w;
			} else if (child->type == NODE_INPUT_TEXT && child->input_buffer[0]) {
				float input_w = measure_text_width(child->input_buffer);
				if (input_w > child_w)
					child_w = input_w;
			}

			if (child->type == NODE_BRANCH) {
				child_w += 0.15f;
			}

			if (child_w > max_width) {
				max_width = child_w;
			}

			calculate_card_dimensions(child);
		}

		node->card_width = max_width + 0.2f;
		node->card_height = TITLE_BAR_HEIGHT + (node->num_children * MENU_ITEM_HEIGHT);
	} else {
		node->card_width = measure_text_width(node->label) + 0.2f;
		node->card_height = MENU_ITEM_HEIGHT;
	}
}

static void update_nextstep_layout_recursive(MenuNode *node, vec3 top_left_anchor)
{
	if (!node)
		return;

	node->target_phi = 0.0f;
	node->target_theta = 0.0f;

	if (node->type == NODE_BRANCH && node->num_children > 0) {
		vec3 card_bg_pos;
		vec3 right_offset, up_offset;
		glm_vec3_scale(node->right_vec, node->card_width * 0.5f, right_offset);
		glm_vec3_scale(node->up_vec, node->card_height * 0.5f, up_offset);
		glm_vec3_add(top_left_anchor, right_offset, card_bg_pos);
		glm_vec3_sub(card_bg_pos, up_offset, card_bg_pos);
		glm_vec3_copy(card_bg_pos, node->card_bg_pos);

		for (int i = 0; i < node->num_children; i++) {
			MenuNode *child = node->children[i];

			if (node->is_expanded) {
				child->box_width = node->card_width;
				child->box_height = MENU_ITEM_HEIGHT;

				vec3 child_top_left;
				glm_vec3_copy(top_left_anchor, child_top_left);
				vec3 down_offset;
				glm_vec3_scale(node->up_vec, TITLE_BAR_HEIGHT + i * MENU_ITEM_HEIGHT, down_offset);
				glm_vec3_sub(child_top_left, down_offset, child_top_left);

				vec3 quad_center;
				glm_vec3_copy(child_top_left, quad_center);
				vec3 center_right, center_down;
				glm_vec3_scale(node->right_vec, node->card_width * 0.5f, center_right);
				glm_vec3_scale(node->up_vec, MENU_ITEM_HEIGHT * 0.5f, center_down);
				glm_vec3_add(quad_center, center_right, quad_center);
				glm_vec3_sub(quad_center, center_down, quad_center);
				glm_vec3_copy(quad_center, child->quad_center_pos);

				vec3 text_anchor;
				glm_vec3_copy(child_top_left, text_anchor);
				vec3 text_right, text_down;
				glm_vec3_scale(node->right_vec, TEXT_PADDING, text_right);
				glm_vec3_scale(node->up_vec, MENU_ITEM_HEIGHT * 0.5f, text_down);
				glm_vec3_add(text_anchor, text_right, text_anchor);
				glm_vec3_sub(text_anchor, text_down, text_anchor);
				glm_vec3_copy(text_anchor, child->text_anchor_pos);

				glm_vec3_copy(quad_center, child->world_pos);

				vec3 submenu_top_left;
				glm_vec3_copy(top_left_anchor, submenu_top_left);
				vec3 submenu_offset;
				glm_vec3_scale(node->right_vec, node->card_width, submenu_offset);
				glm_vec3_add(submenu_top_left, submenu_offset, submenu_top_left);

				update_nextstep_layout_recursive(child, submenu_top_left);
			}
		}
	}
}

static void copy_basis_recursive(MenuNode *node, const SpatialBasis *basis)
{
	if (!node)
		return;
	memcpy(node->right_vec, basis->right, sizeof(vec3));
	memcpy(node->up_vec, basis->up, sizeof(vec3));
	memcpy(node->rotation, basis->rotation, sizeof(versor));
	for (int i = 0; i < node->num_children; i++) {
		copy_basis_recursive(node->children[i], basis);
	}
}

void update_menu_transforms(MenuNode *node, const SpatialBasis *basis)
{
	if (node == NULL)
		return;

	calculate_card_dimensions(node);
	copy_basis_recursive(node, basis);

	vec3 root_top_left;
	spatial_resolve_position(basis, -0.6f, 0.4f, 2.5f, root_top_left);

	update_nextstep_layout_recursive(node, root_top_left);
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

void update_menu_info_displays(MenuNode *node, igraph_t *graph)
{
	if (!node)
		return;

	if (node->type == NODE_INFO_DISPLAY && node->poll_info) {
		node->poll_info(graph, node->info_buffer, sizeof(node->info_buffer));
	}

	if (node->is_expanded) {
		for (int i = 0; i < node->num_children; i++) {
			update_menu_info_displays(node->children[i], graph);
		}
	}
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