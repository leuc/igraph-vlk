/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "ui/menu.h"
#include "graph/command_registry.h"
#include "graph/graph_filter_visibility.h"
#include "graph/graph_types.h"
#include "graph/repo_netzschleuder.h"
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
	node->target_phi = 0.0f;
	node->target_theta = 0.0f;
	node->current_radius = 0.0f;
	node->target_radius = 0.0f;
	node->num_children = 0;
	node->children = NULL;
	node->command = NULL;
	node->is_expanded = false;
	node->hovered = false;
	node->card_width = 0.0f;
	node->card_height = 0.0f;
	return node;
}

static IgraphCommand *create_command(const char *id_name, const char *display_name, IgraphWrapperFunc execute, int num_params)
{
	IgraphCommand *cmd = (IgraphCommand *)malloc(sizeof(IgraphCommand));
	cmd->id_name = strdup(id_name);
	cmd->display_name = strdup(display_name);
	cmd->execute = execute;
	cmd->num_params = num_params;
	cmd->params = (CommandParameter *)malloc(sizeof(CommandParameter) * num_params);
	cmd->produces_visual_output = false;
	cmd->cmd_def = NULL;
	cmd->user_data = NULL;
	return cmd;
}

void init_menu_tree(MenuNode *root)
{
	root->label = strdup("Main");
	root->type = NODE_BRANCH;
	root->target_phi = 0.0f;
	root->target_theta = 0.0f;
	root->current_radius = 0.0f;
	root->target_radius = 1.0f;
	root->num_children = 0;
	root->children = NULL;
	root->command = NULL;
	root->is_expanded = true;
	root->hovered = false;
	root->card_width = 0.0f;
	root->card_height = 0.0f;

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

		MenuNode *leaf = create_menu_node(cmd_def->display_name, NODE_LEAF_COMMAND);
		leaf->command = create_command(cmd_def->command_id, cmd_def->display_name, NULL, 0);
		leaf->command->cmd_def = cmd_def;

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
		if (node->command->user_data) {
			FilterContext *fc = (FilterContext *)node->command->user_data;
			free((void *)fc->attr_name);
			free((void *)fc->attr_value);
			free(node->command->user_data);
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

// ============================================================================
// Dynamic Attribute Filter Menu
// ============================================================================

// Get the command ID for a filter entry (static buffer, call within same scope)
static void filter_make_command_id(char *buf, size_t buf_size, const char *attr_name, const char *attr_value)
{
	snprintf(buf, buf_size, "filter_%s_%s", attr_name, attr_value);
}

// Register a filter in the lookup table
static void filter_register(GraphData *data, const char *command_id, const char *attr_name, const char *attr_value)
{
	if (data->filter_lookup_count >= data->filter_lookup_capacity) {
		int new_cap = data->filter_lookup_capacity ? data->filter_lookup_capacity * 2 : 64;
		FilterLookup *tmp = realloc(data->filter_lookup, sizeof(FilterLookup) * new_cap);
		if (!tmp)
			return;
		data->filter_lookup = tmp;
		data->filter_lookup_capacity = new_cap;
	}
	FilterLookup *entry = &data->filter_lookup[data->filter_lookup_count++];
	entry->command_id = strdup(command_id);
	entry->attr_name = strdup(attr_name);
	entry->attr_value = strdup(attr_value);
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

// Declare the filter execute function (implemented in interaction/filter.c)
extern void execute_filter_reset(ExecutionContext *ctx);
extern void execute_filter_by_attr(ExecutionContext *ctx);

void menu_populate_attribute_filters(MenuNode *root, GraphData *data)
{
	if (!root || !data)
		return;

	// Clear any existing filter entries to prevent duplicates
	menu_clear_attribute_filters(root, data);

	// Find or create "Node" branch
	MenuNode *node_branch = find_child_branch(root, "Node");
	if (!node_branch) {
		node_branch = create_menu_node("Node", NODE_BRANCH);
		MenuNode **tmp = realloc(root->children, sizeof(MenuNode *) * (root->num_children + 1));
		if (!tmp) {
			menu_tree_destroy(node_branch);
			return;
		}
		root->children = tmp;
		root->children[root->num_children++] = node_branch;
	}

	// Find or create "Filter" sub-branch
	MenuNode *filter_branch = find_child_branch(node_branch, "Filter");
	if (!filter_branch) {
		filter_branch = create_menu_node("Filter", NODE_BRANCH);
		MenuNode **tmp = realloc(node_branch->children, sizeof(MenuNode *) * (node_branch->num_children + 1));
		if (!tmp) {
			menu_tree_destroy(filter_branch);
			return;
		}
		node_branch->children = tmp;
		node_branch->children[node_branch->num_children++] = filter_branch;
	}

	// Add "Show All" leaf
	MenuNode *show_all = create_menu_node("Show All", NODE_LEAF_COMMAND);
	show_all->command = create_command("filter_show_all", "Show All", execute_filter_reset, 0);
	show_all->command->cmd_def = NULL;
	{
		MenuNode **tmp = realloc(filter_branch->children, sizeof(MenuNode *) * (filter_branch->num_children + 1));
		if (!tmp) {
			menu_tree_destroy(show_all);
			return;
		}
		filter_branch->children = tmp;
		filter_branch->children[filter_branch->num_children++] = show_all;
	}

	if (data->num_filterable_attrs == 0)
		return;

	// Add attribute name sub-branches with value leaves
	for (int a = 0; a < data->num_filterable_attrs; a++) {
		FilterableAttr *fa = &data->filterable_attrs[a];

		MenuNode *attr_branch = create_menu_node(fa->name, NODE_BRANCH);

		for (int v = 0; v < fa->num_values; v++) {
			char cmd_id[256];
			filter_make_command_id(cmd_id, sizeof(cmd_id), fa->name, fa->values[v]);

			MenuNode *val_leaf = create_menu_node(fa->values[v], NODE_LEAF_COMMAND);
			val_leaf->command = create_command(cmd_id, fa->values[v], execute_filter_by_attr, 0);
			val_leaf->command->cmd_def = NULL;

			// Store attr name/value in user_data
			FilterContext *fc = malloc(sizeof(FilterContext));
			fc->attr_name = strdup(fa->name);
			fc->attr_value = strdup(fa->values[v]);
			val_leaf->command->user_data = fc;

			MenuNode **tmp = realloc(attr_branch->children, sizeof(MenuNode *) * (attr_branch->num_children + 1));
			if (!tmp) {
				menu_tree_destroy(val_leaf);
				free(fc->attr_name);
				free(fc->attr_value);
				free(fc);
				continue;
			}
			attr_branch->children = tmp;
			attr_branch->children[attr_branch->num_children++] = val_leaf;

			filter_register(data, cmd_id, fa->name, fa->values[v]);
		}

		{
			MenuNode **tmp = realloc(filter_branch->children, sizeof(MenuNode *) * (filter_branch->num_children + 1));
			if (!tmp) {
				menu_tree_destroy(attr_branch);
				continue;
			}
			filter_branch->children = tmp;
			filter_branch->children[filter_branch->num_children++] = attr_branch;
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

	// Free the filter lookup table
	if (data) {
		for (int i = 0; i < data->filter_lookup_count; i++) {
			free(data->filter_lookup[i].command_id);
			free(data->filter_lookup[i].attr_name);
			free(data->filter_lookup[i].attr_value);
		}
		free(data->filter_lookup);
		data->filter_lookup = NULL;
		data->filter_lookup_count = 0;
		data->filter_lookup_capacity = 0;
	}

	// Find "Node" branch, then "Filter" sub-branch, clear its children
	MenuNode *node_branch = find_child_branch(root, "Node");
	if (!node_branch)
		return;
	MenuNode *filter_branch = find_child_branch(node_branch, "Filter");
	if (!filter_branch)
		return;
	menu_clear_children(filter_branch);
}

// ============================================================================
// Netzschleuder Catalog Menu Population
// ============================================================================

extern const CommandDef g_command_registry[];
extern const int g_command_registry_size;

static const CommandDef netzschleuder_download_def = {
	"Repository/Netzschleuder", "netzschleuder_download", "Download Network", run_netzschleuder_download, apply_netzschleuder_download, free_netzschleuder_download, NULL, (const CommandParamDef[]){{"entry_id", PARAM_TYPE_STRING, 0, 0, NULL, 0}, {"version_id", PARAM_TYPE_STRING, 0, 0, NULL, 0}}, 2,
};

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

void menu_populate_netzschleuder(MenuNode *root)
{
	fprintf(stderr, "[Netzschleuder] Loading catalog...\n");
	NetzschleuderCatalog *cat = netzschleuder_catalog_load();
	if (!cat)
		return;
	fprintf(stderr, "[Netzschleuder] Loaded %d entries, %d tags\n", cat->num_entries, cat->tag_index.num_tags);

	MenuNode *netz_branch = find_or_create_path(root, "Repository/Netzschleuder");
	if (!netz_branch) {
		netzschleuder_catalog_free(cat);
		return;
	}

	for (int t = 0; t < cat->tag_index.num_tags; t++) {
		NetzschleuderTag *tag = &cat->tag_index.tags[t];
		fprintf(stderr, "[Netzschleuder] Tag %d/%d: '%s' (%d entries)\n", t, cat->tag_index.num_tags, tag->name, tag->num_entries);
		MenuNode *tag_branch = create_menu_node(tag->name, NODE_BRANCH);

		for (int e = 0; e < tag->num_entries; e++) {
			int entry_idx = tag->entry_indices[e];
			NetzschleuderEntry *entry = &cat->entries[entry_idx];

			const char *version_id = (entry->num_nets > 0) ? entry->nets[0] : entry->id;

			MenuNode *leaf = create_menu_node(entry->title, NODE_LEAF_COMMAND);
			leaf->command = create_command(netzschleuder_download_def.command_id, entry->title, NULL, 2);
			leaf->command->cmd_def = &netzschleuder_download_def;

			leaf->command->params[0].name = "entry_id";
			leaf->command->params[0].type = PARAM_TYPE_STRING;
			leaf->command->params[0].value.str_val = strdup(entry->id);

			leaf->command->params[1].name = "version_id";
			leaf->command->params[1].type = PARAM_TYPE_STRING;
			leaf->command->params[1].value.str_val = strdup(version_id);

			add_child(tag_branch, leaf);
		}

		add_child(netz_branch, tag_branch);
	}

	fprintf(stderr, "[Netzschleuder] Menu populated, freeing catalog\n");
	netzschleuder_catalog_free(cat);
	fprintf(stderr, "[Netzschleuder] Done\n");
}
