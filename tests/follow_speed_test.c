/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "app_state.h"
#include "graph/wrappers_follow_speed.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

void menu_invalidate(MenuState *menu, MenuInvalidation flags)
{
	if (flags & MENU_INVALIDATE_LAYOUT)
		menu->layout_revision++;
	if (flags & MENU_INVALIDATE_TEXT)
		menu->text_revision++;
	menu->scene_revision++;
}

static void init_speed_node(MenuNode *node, IgraphCommand *command, const char *id, const char *label)
{
	memset(node, 0, sizeof(*node));
	memset(command, 0, sizeof(*command));
	node->type = NODE_LEAF_COMMAND;
	node->label = strdup(label);
	node->command = command;
	command->id_name = strdup(id);
	command->display_name = strdup(label);
}

static void destroy_speed_node(MenuNode *node)
{
	free((void *)node->label);
	free((void *)node->command->id_name);
	free((void *)node->command->display_name);
}

int main(void)
{
	AppState state = {0};
	ExecutionContext ctx = {.app_state = &state};
	MenuNode root = {0};
	MenuNode nodes[4];
	IgraphCommand commands[4];
	MenuNode *children[] = {&nodes[0], &nodes[1], &nodes[2], &nodes[3]};
	init_speed_node(&nodes[0], &commands[0], "follow_speed_3sec", "[x] 3sec");
	init_speed_node(&nodes[1], &commands[1], "follow_speed_9sec", "[ ] 9sec");
	init_speed_node(&nodes[2], &commands[2], "follow_speed_18sec", "[ ] 18sec");
	init_speed_node(&nodes[3], &commands[3], "follow_speed_27sec", "[ ] 27sec");
	root.children = children;
	root.num_children = 4;
	state.app_ctx.menu.root = &root;

	apply_follow_speed_3sec(&ctx, NULL);
	assert(state.follow_reveal_duration == 3.0f);
	apply_follow_speed_9sec(&ctx, NULL);
	assert(state.follow_reveal_duration == 9.0f);
	apply_follow_speed_18sec(&ctx, NULL);
	assert(state.follow_reveal_duration == 18.0f);
	assert(strcmp(nodes[0].label, "[ ] 3sec") == 0);
	assert(strcmp(nodes[1].label, "[ ] 9sec") == 0);
	assert(strcmp(nodes[2].label, "[x] 18sec") == 0);
	assert(strcmp(nodes[3].label, "[ ] 27sec") == 0);
	apply_follow_speed_27sec(&ctx, NULL);
	assert(state.follow_reveal_duration == 27.0f);
	assert(strcmp(nodes[3].label, "[x] 27sec") == 0);
	assert(state.app_ctx.menu.layout_revision > 0);
	assert(state.app_ctx.menu.text_revision > 0);
	for (int i = 0; i < 4; i++)
		destroy_speed_node(&nodes[i]);
	return 0;
}
