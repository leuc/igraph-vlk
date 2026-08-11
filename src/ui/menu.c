/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "ui/menu.h"
#include "graph/command_registry.h"
#include "graph/graph_filter_visibility.h"
#include "graph/graph_types.h"
#include "graph/repo_netzschleuder.h"
#include "graph/wrappers_constructors.h"
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
	// calloc: the cached spatial data (quad_center_pos, box_width/height, ...) stays zeroed
	// until the node is first laid out, so an unlaid node can never present a garbage quad.
	MenuNode *node = (MenuNode *)calloc(1, sizeof(MenuNode));
	if (!node)
		return NULL;
	node->label = strdup(label);
	node->type = type;
	node->num_children = 0;
	node->children = NULL;
	node->command = NULL;
	node->is_expanded = false;
	node->is_visible = false;
	node->hovered = false;
	node->card_width = 0.0f;
	node->card_height = 0.0f;
	return node;
}

static IgraphCommand *create_command(const char *id_name, const char *display_name, int num_params)
{
	IgraphCommand *cmd = (IgraphCommand *)malloc(sizeof(IgraphCommand));
	cmd->id_name = strdup(id_name);
	cmd->display_name = strdup(display_name);
	cmd->num_params = num_params;
	cmd->params = num_params > 0 ? (CommandParameter *)malloc(sizeof(CommandParameter) * num_params) : NULL;
	cmd->cmd_def = NULL;
	return cmd;
}

void init_menu_tree(MenuNode *root)
{
	// The caller allocates the root with malloc, so clear it before use: the root is never laid
	// out as a row (it only owns a card), and picking must not see uninitialized quad data.
	memset(root, 0, sizeof(MenuNode));
	root->label = strdup("Main");
	root->type = NODE_BRANCH;
	root->num_children = 0;
	root->children = NULL;
	root->command = NULL;
	root->is_expanded = true;
	root->is_visible = false;
	root->hovered = false;
	root->card_width = 0.0f;
	root->card_height = 0.0f;

	for (int i = 0; i < g_command_registry_size; i++) {
		const CommandDef *cmd_def = &g_command_registry[i];

		// Skip entries with NULL display_name — they are populated dynamically
		if (cmd_def->display_name == NULL)
			continue;

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
					if (!branch)
						return;
					MenuNode **tmp = (MenuNode **)realloc(current_parent->children, sizeof(MenuNode *) * (current_parent->num_children + 1));
					if (!tmp) {
						menu_tree_destroy(branch);
						return;
					}
					current_parent->children = tmp;
					current_parent->children[current_parent->num_children] = branch;
					current_parent->num_children++;
				}

				current_parent = branch;
				token = strtok(NULL, "/");
			}
		}

		if (cmd_def->worker_func == NULL) {
			// Branch anchor: find or create a branch node (no command attached)
			MenuNode *branch = NULL;
			for (int c = 0; c < current_parent->num_children; c++) {
				if (current_parent->children[c]->type == NODE_BRANCH && strcmp(current_parent->children[c]->label, cmd_def->display_name) == 0) {
					branch = current_parent->children[c];
					break;
				}
			}
			if (!branch) {
				branch = create_menu_node(cmd_def->display_name, NODE_BRANCH);
				if (!branch)
					return;
				MenuNode **tmp = (MenuNode **)realloc(current_parent->children, sizeof(MenuNode *) * (current_parent->num_children + 1));
				if (!tmp) {
					menu_tree_destroy(branch);
					return;
				}
				current_parent->children = tmp;
				current_parent->children[current_parent->num_children++] = branch;
			}
		} else {
			// Leaf: create an actionable command leaf
			MenuNode *leaf = create_menu_node(cmd_def->display_name, NODE_LEAF_COMMAND);
			if (!leaf)
				return;
			leaf->command = create_command(cmd_def->command_id, cmd_def->display_name, cmd_def->num_params);
			leaf->command->cmd_def = cmd_def;

			for (int p = 0; p < cmd_def->num_params; p++) {
				leaf->command->params[p].name = cmd_def->param_defs[p].name;
				leaf->command->params[p].type = cmd_def->param_defs[p].type;
				leaf->command->params[p].min_val = cmd_def->param_defs[p].min_val;
				leaf->command->params[p].max_val = cmd_def->param_defs[p].max_val;
				leaf->command->params[p].value.i_val = 0;
			}

			MenuNode **tmp = (MenuNode **)realloc(current_parent->children, sizeof(MenuNode *) * (current_parent->num_children + 1));
			if (!tmp) {
				menu_tree_destroy(leaf);
				return;
			}
			current_parent->children = tmp;
			current_parent->children[current_parent->num_children] = leaf;
			current_parent->num_children++;
		}
	}
}

void menu_tree_destroy(MenuNode *node)
{
	if (node == NULL)
		return;
	if (node->type == NODE_BRANCH) {
		for (int i = 0; i < node->num_children; i++) {
			menu_tree_destroy(node->children[i]);
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
		if (node->command->params) {
			for (int i = 0; i < node->command->num_params; i++) {
				if (node->command->params[i].type == PARAM_TYPE_STRING)
					free((void *)node->command->params[i].value.str_val);
			}
			free(node->command->params);
		}
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
				// Reached only when every ancestor is expanded, so this row is on screen:
				// its quad data below is fresh and it is a legal picking target this frame.
				child->is_visible = true;

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

// Hide the whole tree before laying it out; update_nextstep_layout_recursive re-marks the rows
// it actually places. Anything it does not reach (collapsed subtree, hidden branch, the root
// itself) therefore stays invisible and keeps its stale quad out of the picker.
static void clear_visibility_recursive(MenuNode *node)
{
	if (!node)
		return;
	node->is_visible = false;
	for (int i = 0; i < node->num_children; i++) {
		clear_visibility_recursive(node->children[i]);
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
	clear_visibility_recursive(node);

	vec3 root_top_left;
	spatial_resolve_position(basis, -0.6f, 0.4f, 2.5f, root_top_left);

	update_nextstep_layout_recursive(node, root_top_left);
}


// Find a branch by label in a parent's children (searches all children)
static MenuNode *find_child_branch(MenuNode *parent, const char *label)
{
	if (!parent)
		return NULL;
	for (int i = 0; i < parent->num_children; i++) {
		if (parent->children[i]->type == NODE_BRANCH && strcmp(parent->children[i]->label, label) == 0)
			return parent->children[i];
	}
	return NULL;
}

// Look up a CommandDef by command_id from g_command_registry
static const CommandDef *find_command_def(const char *command_id)
{
	for (int i = 0; i < g_command_registry_size; i++) {
		if (strcmp(g_command_registry[i].command_id, command_id) == 0)
			return &g_command_registry[i];
	}
	return NULL;
}

MenuNode *menu_find_node_by_command_id(MenuNode *node, const char *command_id)
{
	if (!node)
		return NULL;
	if (node->type == NODE_LEAF_COMMAND && node->command && node->command->cmd_def && strcmp(node->command->cmd_def->command_id, command_id) == 0)
		return node;
	for (int i = 0; i < node->num_children; i++) {
		MenuNode *found = menu_find_node_by_command_id(node->children[i], command_id);
		if (found)
			return found;
	}
	return NULL;
}

void menu_populate_attribute_filters(MenuNode *root, GraphData *data)
{
	if (!root || !data)
		return;

	// Clear any existing filter entries to prevent duplicates
	menu_clear_attribute_filters(root, data);

	// Find "Filter" branch (created by init_menu_tree from the branch anchor)
	MenuNode *filter_root = find_child_branch(root, "Filter");
	if (!filter_root)
		return;

	// Find or create "Node" sub-branch
	MenuNode *node_branch = find_child_branch(filter_root, "Node");
	if (!node_branch) {
		node_branch = create_menu_node("Node", NODE_BRANCH);
		if (!node_branch)
			return;
		MenuNode **tmp = realloc(filter_root->children, sizeof(MenuNode *) * (filter_root->num_children + 1));
		if (!tmp) {
			menu_tree_destroy(node_branch);
			return;
		}
		filter_root->children = tmp;
		filter_root->children[filter_root->num_children++] = node_branch;
	}

	// Add "Show All" leaf using registry CommandDef
	const CommandDef *show_all_def = find_command_def("filter_show_all");
	MenuNode *show_all = create_menu_node("Show All", NODE_LEAF_COMMAND);
	if (!show_all)
		return;
	show_all->command = create_command("filter_show_all", "Show All", 0);
	show_all->command->cmd_def = show_all_def;
	{
		MenuNode **tmp = realloc(node_branch->children, sizeof(MenuNode *) * (node_branch->num_children + 1));
		if (!tmp) {
			menu_tree_destroy(show_all);
			return;
		}
		node_branch->children = tmp;
		node_branch->children[node_branch->num_children++] = show_all;
	}

	if (data->num_filterable_attrs == 0)
		return;

	// Add attribute name sub-branches with value leaves
	const CommandDef *filter_def = find_command_def("filter_by_attr");
	if (!filter_def)
		return;

	for (int a = 0; a < data->num_filterable_attrs; a++) {
		FilterableAttr *fa = &data->filterable_attrs[a];

		MenuNode *attr_branch = create_menu_node(fa->name, NODE_BRANCH);
		if (!attr_branch)
			continue;

		for (int v = 0; v < fa->num_values; v++) {
			MenuNode *val_leaf = create_menu_node(fa->values[v], NODE_LEAF_COMMAND);
			if (!val_leaf)
				continue;
			val_leaf->command = create_command("filter_by_attr", fa->values[v], 2);
			val_leaf->command->cmd_def = filter_def;
			val_leaf->command->params[0].name = "attr_name";
			val_leaf->command->params[0].type = PARAM_TYPE_STRING;
			val_leaf->command->params[0].value.str_val = strdup(fa->name);
			val_leaf->command->params[1].name = "attr_value";
			val_leaf->command->params[1].type = PARAM_TYPE_STRING;
			val_leaf->command->params[1].value.str_val = strdup(fa->values[v]);

			MenuNode **tmp = realloc(attr_branch->children, sizeof(MenuNode *) * (attr_branch->num_children + 1));
			if (!tmp) {
				menu_tree_destroy(val_leaf);
				continue;
			}
			attr_branch->children = tmp;
			attr_branch->children[attr_branch->num_children++] = val_leaf;
		}

		{
			MenuNode **tmp = realloc(node_branch->children, sizeof(MenuNode *) * (node_branch->num_children + 1));
			if (!tmp) {
				menu_tree_destroy(attr_branch);
				continue;
			}
			node_branch->children = tmp;
			node_branch->children[node_branch->num_children++] = attr_branch;
		}
	}
}

// Helper: recursively remove children from a node (free their trees)
static void menu_clear_children(MenuNode *node)
{
	if (!node)
		return;
	for (int i = 0; i < node->num_children; i++) {
		menu_tree_destroy(node->children[i]);
	}
	free(node->children);
	node->children = NULL;
	node->num_children = 0;
}

void menu_clear_attribute_filters(MenuNode *root, GraphData *data)
{
	if (!root)
		return;
	(void)data;

	// Find "Filter" branch, then "Node" sub-branch, clear its children
	MenuNode *filter_root = find_child_branch(root, "Filter");
	if (!filter_root)
		return;
	MenuNode *node_branch = find_child_branch(filter_root, "Node");
	if (!node_branch)
		return;
	menu_clear_children(node_branch);
}

void menu_populate_attribute_edge_filters(MenuNode *root, GraphData *data)
{
	if (!root || !data)
		return;

	// Clear any existing filter entries to prevent duplicates
	menu_clear_attribute_edge_filters(root, data);

	// Find "Filter" branch (created by init_menu_tree from the branch anchor)
	MenuNode *filter_root = find_child_branch(root, "Filter");
	if (!filter_root)
		return;

	// Find or create "Edge" sub-branch
	MenuNode *edge_branch = find_child_branch(filter_root, "Edge");
	if (!edge_branch) {
		edge_branch = create_menu_node("Edge", NODE_BRANCH);
		if (!edge_branch)
			return;
		MenuNode **tmp = realloc(filter_root->children, sizeof(MenuNode *) * (filter_root->num_children + 1));
		if (!tmp) {
			menu_tree_destroy(edge_branch);
			return;
		}
		filter_root->children = tmp;
		filter_root->children[filter_root->num_children++] = edge_branch;
	}

	// Add "Show All" leaf using registry CommandDef
	const CommandDef *show_all_def = find_command_def("filter_edge_show_all");
	MenuNode *show_all = create_menu_node("Show All", NODE_LEAF_COMMAND);
	if (!show_all)
		return;
	show_all->command = create_command("filter_edge_show_all", "Show All", 0);
	show_all->command->cmd_def = show_all_def;
	{
		MenuNode **tmp = realloc(edge_branch->children, sizeof(MenuNode *) * (edge_branch->num_children + 1));
		if (!tmp) {
			menu_tree_destroy(show_all);
			return;
		}
		edge_branch->children = tmp;
		edge_branch->children[edge_branch->num_children++] = show_all;
	}

	if (data->num_filterable_edge_attrs == 0)
		return;

	// Add attribute name sub-branches with value leaves
	const CommandDef *filter_def = find_command_def("filter_by_edge_attr");
	if (!filter_def)
		return;

	for (int a = 0; a < data->num_filterable_edge_attrs; a++) {
		FilterableAttr *fa = &data->filterable_edge_attrs[a];

		MenuNode *attr_branch = create_menu_node(fa->name, NODE_BRANCH);
		if (!attr_branch)
			continue;

		for (int v = 0; v < fa->num_values; v++) {
			MenuNode *val_leaf = create_menu_node(fa->values[v], NODE_LEAF_COMMAND);
			if (!val_leaf)
				continue;
			val_leaf->command = create_command("filter_by_edge_attr", fa->values[v], 2);
			val_leaf->command->cmd_def = filter_def;
			val_leaf->command->params[0].name = "attr_name";
			val_leaf->command->params[0].type = PARAM_TYPE_STRING;
			val_leaf->command->params[0].value.str_val = strdup(fa->name);
			val_leaf->command->params[1].name = "attr_value";
			val_leaf->command->params[1].type = PARAM_TYPE_STRING;
			val_leaf->command->params[1].value.str_val = strdup(fa->values[v]);

			MenuNode **tmp = realloc(attr_branch->children, sizeof(MenuNode *) * (attr_branch->num_children + 1));
			if (!tmp) {
				menu_tree_destroy(val_leaf);
				continue;
			}
			attr_branch->children = tmp;
			attr_branch->children[attr_branch->num_children++] = val_leaf;
		}

		{
			MenuNode **tmp = realloc(edge_branch->children, sizeof(MenuNode *) * (edge_branch->num_children + 1));
			if (!tmp) {
				menu_tree_destroy(attr_branch);
				continue;
			}
			edge_branch->children = tmp;
			edge_branch->children[edge_branch->num_children++] = attr_branch;
		}
	}
}

void menu_clear_attribute_edge_filters(MenuNode *root, GraphData *data)
{
	if (!root)
		return;
	(void)data;

	// Find "Filter" branch, then "Edge" sub-branch, clear its children
	MenuNode *filter_root = find_child_branch(root, "Filter");
	if (!filter_root)
		return;
	MenuNode *edge_branch = find_child_branch(filter_root, "Edge");
	if (!edge_branch)
		return;
	menu_clear_children(edge_branch);
}


extern const CommandDef g_command_registry[];
extern const int g_command_registry_size;

static MenuNode *find_or_create_branch(MenuNode *parent, const char *label)
{
	for (int i = 0; i < parent->num_children; i++) {
		if (parent->children[i]->type == NODE_BRANCH && strcmp(parent->children[i]->label, label) == 0)
			return parent->children[i];
	}
	MenuNode *branch = create_menu_node(label, NODE_BRANCH);
	MenuNode **tmp = (MenuNode **)realloc(parent->children, sizeof(MenuNode *) * (parent->num_children + 1));
	if (!tmp) {
		menu_tree_destroy(branch);
		return NULL;
	}
	parent->children = tmp;
	parent->children[parent->num_children++] = branch;
	return branch;
}

static void add_child(MenuNode *parent, MenuNode *child)
{
	if (!parent || !child)
		return;
	MenuNode **tmp = (MenuNode **)realloc(parent->children, sizeof(MenuNode *) * (parent->num_children + 1));
	if (!tmp) {
		menu_tree_destroy(child);
		return;
	}
	parent->children = tmp;
	parent->children[parent->num_children++] = child;
}

static MenuNode *find_or_create_path(MenuNode *root, const char *path)
{
	char path_copy[256];
	strncpy(path_copy, path, sizeof(path_copy) - 1);
	path_copy[255] = '\0';

	MenuNode *current = root;
	char *token = strtok(path_copy, "/");
	while (token != NULL) {
		current = find_or_create_branch(current, token);
		if (!current)
			return NULL;
		token = strtok(NULL, "/");
	}
	return current;
}

static MenuNode *create_netz_leaf(const char *label, const StaticNetEntry *entry)
{
	const CommandDef *cmd_def = find_command_def("netzschleuder_download");
	if (!cmd_def)
		return NULL;
	MenuNode *leaf = create_menu_node(label, NODE_LEAF_COMMAND);
	if (!leaf)
		return NULL;
	leaf->command = create_command("netzschleuder_download", label, 2);
	leaf->command->cmd_def = cmd_def;
	leaf->command->params[0].name = "entry_id";
	leaf->command->params[0].type = PARAM_TYPE_STRING;
	leaf->command->params[0].value.str_val = strdup(entry->entry_id);
	leaf->command->params[1].name = "version_id";
	leaf->command->params[1].type = PARAM_TYPE_STRING;
	leaf->command->params[1].value.str_val = strdup(entry->version_id);
	return leaf;
}

void menu_populate_netzschleuder_static(MenuNode *root)
{
	int count = 0;
	const StaticNetEntry *entries = netzschleuder_static_entries(&count);
	if (count == 0)
		return;

	MenuNode *netz_branch = find_or_create_path(root, "Data/Repository");
	if (!netz_branch)
		return;

	int tag_max = count < 100 ? 200 : count * 2;
	char **tag_names = malloc(sizeof(char *) * (size_t)tag_max);
	int *tag_counts = calloc((size_t)tag_max, sizeof(int));
	int num_tags = 0;
	char tag_buf[512];

	for (int i = 0; i < count; i++) {
		const char *tags = entries[i].tags;
		if (!tags || tags[0] == '\0')
			continue;
		strncpy(tag_buf, tags, sizeof(tag_buf) - 1);
		tag_buf[sizeof(tag_buf) - 1] = '\0';
		char *t = strtok(tag_buf, ",");
		while (t) {
			while (*t == ' ')
				t++;
			if (*t == '\0') {
				t = strtok(NULL, ",");
				continue;
			}
			int idx = -1;
			for (int j = 0; j < num_tags; j++) {
				if (strcmp(tag_names[j], t) == 0) {
					idx = j;
					break;
				}
			}
			if (idx < 0) {
				idx = num_tags++;
				tag_names[idx] = strdup(t);
				tag_counts[idx] = 0;
			}
			tag_counts[idx]++;
			t = strtok(NULL, ",");
		}
	}

	// Non-qualifying entries are collected for Phase 3.
	int *other_indices = malloc(sizeof(int) * (size_t)count);
	int num_other = 0;
	char label[256];

	for (int i = 0; i < count; i++) {
		const StaticNetEntry *entry = &entries[i];
		snprintf(label, sizeof(label), "%s (%d, %d)", entry->title, entry->num_nodes, entry->num_edges);

		const char *tags = entry->tags;
		if (!tags || tags[0] == '\0') {
			other_indices[num_other++] = i;
			continue;
		}
		strncpy(tag_buf, tags, sizeof(tag_buf) - 1);
		tag_buf[sizeof(tag_buf) - 1] = '\0';

		bool assigned = false;
		char *t = strtok(tag_buf, ",");
		while (t) {
			while (*t == ' ')
				t++;
			if (*t == '\0') {
				t = strtok(NULL, ",");
				continue;
			}
			int tc = 0;
			for (int j = 0; j < num_tags; j++) {
				if (strcmp(tag_names[j], t) == 0) {
					tc = tag_counts[j];
					break;
				}
			}
			if (tc > 1) {
				MenuNode *leaf = create_netz_leaf(label, entry);
				MenuNode *branch = find_or_create_branch(netz_branch, t);
				if (branch)
					add_child(branch, leaf);
				assigned = true;
			}
			t = strtok(NULL, ",");
		}
		if (!assigned)
			other_indices[num_other++] = i;
	}

	if (num_other > 0) {
		MenuNode *other_branch = find_or_create_branch(netz_branch, "Other");
		for (int k = 0; k < num_other; k++) {
			const StaticNetEntry *entry = &entries[other_indices[k]];
			snprintf(label, sizeof(label), "%s (%d, %d)", entry->title, entry->num_nodes, entry->num_edges);
			MenuNode *leaf = create_netz_leaf(label, entry);
			if (other_branch)
				add_child(other_branch, leaf);
		}
	}

	free(other_indices);
	for (int i = 0; i < num_tags; i++)
		free(tag_names[i]);
	free(tag_names);
	free(tag_counts);
}


static const char *famous_graph_names[] = {
	"Bull", "Chvatal", "Coxeter", "Cubical", "Diamond", "Dodecahedral", "Folkman", "Franklin", "Frucht", "Grotzsch", "Heawood", "Herschel", "House", "HouseX", "Icosahedral", "Krackhardt_Kite", "Levi", "McGee", "Meredith", "Noperfectmatching", "Nonline", "Octahedral", "Petersen", "Robertson", "Smallestcyclicgroup", "Tetrahedral", "Thomassen", "Tutte", "Uniquely3colorable", "Walther", "Zachary",
};
static const int num_famous_graphs = sizeof(famous_graph_names) / sizeof(famous_graph_names[0]);

void menu_populate_famous_graphs(MenuNode *root)
{
	const CommandDef *cmd_def = find_command_def("igraph_famous");
	if (!cmd_def)
		return;

	MenuNode *famous_branch = find_or_create_path(root, "Data/Famous");
	if (!famous_branch)
		return;

	for (int i = 0; i < num_famous_graphs; i++) {
		const char *name = famous_graph_names[i];
		MenuNode *leaf = create_menu_node(name, NODE_LEAF_COMMAND);
		if (!leaf)
			continue;
		leaf->command = create_command("igraph_famous", name, 1);
		leaf->command->cmd_def = cmd_def;
		leaf->command->params[0].name = "name";
		leaf->command->params[0].type = PARAM_TYPE_STRING;
		leaf->command->params[0].value.str_val = strdup(name);
		add_child(famous_branch, leaf);
	}
}
