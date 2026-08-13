/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "graph/wrappers_follow_speed.h"

#include "app_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
	const char *command_id;
	const char *label;
	float duration;
} FollowSpeedPreset;

static void follow_speed_update_labels(MenuNode *node, const FollowSpeedPreset *preset, bool *changed)
{
	if (!node)
		return;
	if (node->command && node->command->id_name) {
		const char *label = strcmp(node->command->id_name, preset->command_id) == 0 ? preset->label : NULL;
		if (label && strcmp(node->label, label) != 0) {
			free((void *)node->label);
			node->label = strdup(label);
			free((void *)node->command->display_name);
			node->command->display_name = strdup(label);
			*changed = true;
		} else if (!label && strncmp(node->command->id_name, "follow_speed_", 13) == 0) {
			const char *unchecked = node->command->id_name + 13;
			char display_name[24];
			snprintf(display_name, sizeof(display_name), "[ ] %s", unchecked);
			if (strcmp(node->label, display_name) != 0) {
				free((void *)node->label);
				node->label = strdup(display_name);
				free((void *)node->command->display_name);
				node->command->display_name = strdup(display_name);
				*changed = true;
			}
		}
	}
	for (int i = 0; i < node->num_children; i++)
		follow_speed_update_labels(node->children[i], preset, changed);
}

static void apply_follow_speed(ExecutionContext *ctx, const FollowSpeedPreset *preset)
{
	if (!ctx || !ctx->app_state)
		return;
	ctx->app_state->follow_reveal_duration = preset->duration;
	bool changed = false;
	follow_speed_update_labels(ctx->app_state->app_ctx.menu.root, preset, &changed);
	if (changed)
		menu_invalidate(&ctx->app_state->app_ctx.menu, MENU_INVALIDATE_LAYOUT | MENU_INVALIDATE_TEXT);
}

void apply_follow_speed_3sec(ExecutionContext *ctx, void *result_data)
{
	(void)result_data;
	static const FollowSpeedPreset preset = {"follow_speed_3sec", "[x] 3sec", 3.0f};
	apply_follow_speed(ctx, &preset);
}

void apply_follow_speed_9sec(ExecutionContext *ctx, void *result_data)
{
	(void)result_data;
	static const FollowSpeedPreset preset = {"follow_speed_9sec", "[x] 9sec", 9.0f};
	apply_follow_speed(ctx, &preset);
}

void apply_follow_speed_18sec(ExecutionContext *ctx, void *result_data)
{
	(void)result_data;
	static const FollowSpeedPreset preset = {"follow_speed_18sec", "[x] 18sec", 18.0f};
	apply_follow_speed(ctx, &preset);
}

void apply_follow_speed_27sec(ExecutionContext *ctx, void *result_data)
{
	(void)result_data;
	static const FollowSpeedPreset preset = {"follow_speed_27sec", "[x] 27sec", 27.0f};
	apply_follow_speed(ctx, &preset);
}
