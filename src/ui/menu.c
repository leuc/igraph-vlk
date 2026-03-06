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
	node->target_radius = 0.0f; // Start collapsed
	node->num_children = 0;
	node->children = NULL;
	node->command = NULL;
	node->is_expanded = false;
	return node;
}

// Helper function to create a command
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
	cmd->cmd_def = NULL; // Will be set for data-driven commands
	return cmd;
}

// Forward declarations
static void assign_menu_icons(MenuNode *node);

// Initialize the menu tree from the command registry
void init_menu_tree(MenuNode *root)
{
	// Initialize root
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

	for (int i = 0; i < g_command_registry_size; i++) {
		const CommandDef *cmd_def = &g_command_registry[i];

		MenuNode *current_parent = root;

		// 1. Process Path (Create/Traverse Folders)
		if (cmd_def->category_path && strlen(cmd_def->category_path) > 0) {
			char path_copy[256];
			strncpy(path_copy, cmd_def->category_path, sizeof(path_copy) - 1);
			path_copy[255] = '\0';

			char *token = strtok(path_copy, "/");
			while (token != NULL) {
				MenuNode *branch = NULL;

				// Search for existing folder
				for (int c = 0; c < current_parent->num_children; c++) {
					if (current_parent->children[c]->type == NODE_BRANCH && strcmp(current_parent->children[c]->label, token) == 0) {
						branch = current_parent->children[c];
						break;
					}
				}

				// Create folder if it doesn't exist
				if (branch == NULL) {
					branch = create_menu_node(token, NODE_BRANCH);
					current_parent->children = (MenuNode **)realloc(current_parent->children, sizeof(MenuNode *) * (current_parent->num_children + 1));
					current_parent->children[current_parent->num_children] = branch;
					current_parent->num_children++;
				}

				// Move down into the folder
				current_parent = branch;
				token = strtok(NULL, "/");
			}
		}

		// 2. Path complete. Attach the Leaf Node.
		MenuNode *leaf = create_menu_node(cmd_def->display_name, NODE_LEAF);
		leaf->command = create_command(cmd_def->command_id, cmd_def->display_name, NULL, 0);
		leaf->command->cmd_def = cmd_def;

		current_parent->children = (MenuNode **)realloc(current_parent->children, sizeof(MenuNode *) * (current_parent->num_children + 1));
		current_parent->children[current_parent->num_children] = leaf;
		current_parent->num_children++;
	}

	// Assign icons globally
	assign_menu_icons(root);
}

// Destroy the menu tree recursively
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

	// Also free command if it exists
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

// Update menu animation with smooth easing
// Uses recursive vertical tree layout
static void update_menu_layout_recursive(MenuNode *node, float delta_time, int depth, float *current_y)
{
	if (node == NULL)
		return;

	float speed = 8.0f;
	float diff = node->target_radius - node->current_radius;
	if (fabsf(diff) < 0.001f) {
		node->current_radius = node->target_radius;
	} else {
		node->current_radius += diff * speed * delta_time;
	}

	// Position THIS node
	node->target_phi = (float)(depth - 1) * 0.12f;
	node->target_theta = *current_y;

	// Only consume vertical space if this node is expanding/open
	// This ensures smooth stacking animation
	*current_y -= 0.12f * node->current_radius;

	// Recursively update children
	if (node->num_children > 0) {
		for (int i = 0; i < node->num_children; i++) {
			MenuNode *child = node->children[i];

			// Children only expand if parent is mostly open AND expanded
			child->target_radius = (node->current_radius > 0.5f && node->is_expanded) ? 1.0f : 0.0f;

			// Always recurse if parent is visible to ensure children close/open correctly
			if (node->current_radius > 0.001f) {
				update_menu_layout_recursive(child, delta_time, depth + 1, current_y);
			} else {
				// Parent is fully closed, snap child state
				child->current_radius = 0.0f;
				child->target_radius = 0.0f;
				child->target_phi = 0.0f;
				child->target_theta = 0.0f;
			}
		}
	}
}

void update_menu_animation(MenuNode *node, float delta_time)
{
	if (node == NULL)
		return;

	// Start tracking layout from the top
	float start_y = 0.0f;

	// The root node itself doesn't need indentation, its children start at depth 1
	update_menu_layout_recursive(node, delta_time, 1, &start_y);
}

// Calculate and cache spatial transforms for ALL nodes in the menu tree
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

		float x_off = current->target_phi - 0.8f;
		float y_off = current->target_theta;

		spatial_resolve_position(basis, x_off, y_off, 2.5f, current->text_anchor_pos);

		float world_text_scale = 0.003f;
		float padding_x = 0.08f;
		float fixed_height = 0.09f;

		float total_w = 0.0f;
		if (current->label) {
			int label_len = strlen(current->label);
			for (int i = 0; i < label_len; i++) {
				unsigned char c = current->label[i];
				CharInfo *ci = (c < 128) ? &globalAtlas.chars[c] : &globalAtlas.chars[32];
				total_w += ci->xadvance;
			}
		}

		current->box_width = (total_w * world_text_scale) + padding_x;
		current->box_height = fixed_height;

		glm_vec3_copy(current->text_anchor_pos, current->quad_center_pos);
		if (current->label) {
			vec3 offset;
			glm_vec3_scale(current->right_vec, current->box_width * 0.5f, offset);
			glm_vec3_add(current->quad_center_pos, offset, current->quad_center_pos);
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

// Static helper to assign icons to nodes recursively
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
