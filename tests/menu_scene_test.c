/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "interaction/state.h"
#include "ui/menu_metrics.h"
#include "vulkan/menu_scene.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static bool resolve_text(void *context, const char *text, TextRegion *region)
{
	(void)context;
	memset(region, 0, sizeof(*region));
	region->u1 = 1.0f;
	region->v1 = 1.0f;
	region->width_px = (float)strlen(text) * 10.0f;
	region->height_px = 20.0f;
	return true;
}

static float measure_text(void *context, const char *text)
{
	(void)context;
	return (float)strlen(text) * 10.0f * MENU_METRICS.text_scale;
}

static void init_node(MenuNode *node, MenuNodeType type, const char *label)
{
	memset(node, 0, sizeof(*node));
	node->type = type;
	node->label = label;
	node->box_width = 1.0f;
	node->box_height = MENU_METRICS.item_height;
	node->card_width = 1.0f;
	node->card_height = 1.0f;
	node->right_vec[0] = 1.0f;
	node->up_vec[1] = 1.0f;
	node->rotation[3] = 1.0f;
}

static void test_scene_counts(void)
{
	MenuNode root;
	MenuNode leaf;
	MenuNode branch;
	MenuNode grandchild;
	init_node(&root, NODE_BRANCH, "Main");
	init_node(&leaf, NODE_LEAF_COMMAND, "Leaf");
	init_node(&branch, NODE_BRANCH, "Branch");
	init_node(&grandchild, NODE_LEAF_COMMAND, "Grandchild");
	root.is_expanded = true;
	branch.is_expanded = true;
	leaf.is_visible = true;
	branch.is_visible = true;
	grandchild.is_visible = true;
	MenuNode *root_children[] = {&leaf, &branch};
	MenuNode *branch_children[] = {&grandchild};
	root.children = root_children;
	root.num_children = 2;
	branch.children = branch_children;
	branch.num_children = 1;

	MenuState menu = {.root = &root, .active_level = &root, .hovered_node = &leaf};
	MenuTextProvider text = {.resolve = resolve_text, .measure_width = measure_text};
	MenuScene scene = {0};
	assert(menu_scene_build(&menu, &text, &scene));
	assert(scene.instance_count == 7);
	assert(scene.text_instance_count == 6);
	assert(scene.instances[2].hovered == 1.0f);

	InfoCardData info = {.title = "Details", .num_pairs = 1, .pairs = {{{"Key"}, {"Value"}}}};
	menu_set_info_card(&menu, &info);
	assert(menu_scene_build(&menu, &text, &scene));
	assert(scene.instance_count == 9);
	assert(scene.text_instance_count == 8);

	branch.is_expanded = false;
	assert(menu_scene_build(&menu, &text, &scene));
	assert(scene.instance_count == 6);
	assert(scene.text_instance_count == 6);
	menu_scene_destroy(&scene);
}

static void test_capacity_growth(void)
{
	enum { CHILD_COUNT = 300 };
	MenuNode root;
	init_node(&root, NODE_BRANCH, "Main");
	root.is_expanded = true;
	MenuNode *children[CHILD_COUNT];
	MenuNode nodes[CHILD_COUNT];
	for (int i = 0; i < CHILD_COUNT; i++) {
		init_node(&nodes[i], NODE_LEAF_COMMAND, "Item");
		nodes[i].is_visible = true;
		children[i] = &nodes[i];
	}
	root.children = children;
	root.num_children = CHILD_COUNT;
	MenuState menu = {.root = &root};
	MenuTextProvider text = {.resolve = resolve_text, .measure_width = measure_text};
	MenuScene scene = {0};
	assert(menu_scene_build(&menu, &text, &scene));
	assert(scene.instance_count == CHILD_COUNT + 2);
	assert(scene.text_instance_count == CHILD_COUNT + 1);
	assert(scene.instance_capacity >= scene.instance_count);
	assert(scene.text_instance_capacity >= scene.text_instance_count);
	const MenuInstance *instances = scene.instances;
	const TextQuadInstance *text_instances = scene.text_instances;
	root.is_expanded = false;
	assert(menu_scene_build(&menu, &text, &scene));
	assert(scene.instance_count == 0);
	assert(scene.text_instance_count == 0);
	assert(scene.instances == instances);
	assert(scene.text_instances == text_instances);
	menu_scene_destroy(&scene);
}

static void test_revisions(void)
{
	MenuNode first;
	MenuNode second;
	MenuState menu = {.layout_revision = 1, .text_revision = 1, .scene_revision = 1};
	assert(menu_set_hovered(&menu, &first));
	assert(menu.scene_revision == 2);
	assert(menu.text_revision == 1);
	assert(!menu_set_hovered(&menu, &first));
	assert(menu.scene_revision == 2);
	assert(menu_set_hovered(&menu, &second));
	assert(menu.scene_revision == 3);

	menu_invalidate(&menu, MENU_INVALIDATE_LAYOUT);
	assert(menu.layout_revision == 2);
	assert(menu.scene_revision == 4);

	InfoCardData info = {.title = "Result", .num_pairs = 1, .pairs = {{{"A"}, {"B"}}}};
	menu_set_info_card(&menu, &info);
	assert(menu.text_revision == 2);
	assert(menu.scene_revision == 5);
	menu_set_info_card(&menu, &info);
	assert(menu.text_revision == 2);
	assert(menu.scene_revision == 5);
}

int main(void)
{
	test_scene_counts();
	test_capacity_growth();
	test_revisions();
	return 0;
}
