/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "interaction/state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
	const char *command_id;
	float duration;
} FollowAnimationSpeedOption;

static const FollowAnimationSpeedOption FOLLOW_ANIMATION_SPEED_OPTIONS[] = {
	{"follow_speed_3", 3.0f},
	{"follow_speed_9", 9.0f},
	{"follow_speed_18", 18.0f},
	{"follow_speed_27", 27.0f},
};

static bool menu_update_follow_animation_labels(MenuNode *node, float duration)
{
	if (!node)
		return false;

	bool changed = false;
	if (node->type == NODE_LEAF_COMMAND && node->command && node->command->id_name) {
		for (size_t i = 0; i < sizeof(FOLLOW_ANIMATION_SPEED_OPTIONS) / sizeof(FOLLOW_ANIMATION_SPEED_OPTIONS[0]); i++) {
			const FollowAnimationSpeedOption *option = &FOLLOW_ANIMATION_SPEED_OPTIONS[i];
			if (strcmp(node->command->id_name, option->command_id) != 0)
				continue;

			char label[32];
			snprintf(label, sizeof(label), "(%c) %.0f sec", option->duration == duration ? 'x' : ' ', option->duration);
			if (strcmp(node->label, label) == 0)
				break;

			char *new_label = strdup(label);
			char *new_display_name = strdup(label);
			if (!new_label || !new_display_name) {
				free(new_label);
				free(new_display_name);
				break;
			}
			free((void *)node->label);
			free((void *)node->command->display_name);
			node->label = new_label;
			node->command->display_name = new_display_name;
			changed = true;
			break;
		}
	}

	for (int i = 0; i < node->num_children; i++)
		if (menu_update_follow_animation_labels(node->children[i], duration))
			changed = true;
	return changed;
}

void menu_invalidate(MenuState *menu, MenuInvalidation flags)
{
	if (!menu || flags == MENU_INVALIDATE_NONE)
		return;
	if (flags & MENU_INVALIDATE_LAYOUT)
		menu->layout_revision++;
	if (flags & MENU_INVALIDATE_TEXT)
		menu->text_revision++;
	menu->scene_revision++;
}

bool menu_set_hovered(MenuState *menu, MenuNode *node)
{
	if (!menu || menu->hovered_node == node)
		return false;
	menu->hovered_node = node;
	menu_invalidate(menu, MENU_INVALIDATE_SCENE);
	return true;
}

void menu_set_info_card(MenuState *menu, const InfoCardData *data)
{
	InfoCardState next = {0};
	if (data) {
		next.is_visible = true;
		snprintf(next.title, sizeof(next.title), "%s", data->title);
		next.num_pairs = data->num_pairs;
		if (next.num_pairs < 0)
			next.num_pairs = 0;
		if (next.num_pairs > 8)
			next.num_pairs = 8;
		for (int i = 0; i < next.num_pairs; i++) {
			snprintf(next.pairs[i].key, sizeof(next.pairs[i].key), "%s", data->pairs[i].key);
			snprintf(next.pairs[i].value, sizeof(next.pairs[i].value), "%s", data->pairs[i].value);
		}
	}
	if (!menu || memcmp(&menu->info_card, &next, sizeof(next)) == 0)
		return;
	menu->info_card = next;
	menu_invalidate(menu, MENU_INVALIDATE_TEXT);
}

bool menu_set_follow_animation_duration(MenuState *menu, float duration)
{
	if (!menu)
		return false;

	bool valid = false;
	for (size_t i = 0; i < sizeof(FOLLOW_ANIMATION_SPEED_OPTIONS) / sizeof(FOLLOW_ANIMATION_SPEED_OPTIONS[0]); i++)
		if (FOLLOW_ANIMATION_SPEED_OPTIONS[i].duration == duration) {
			valid = true;
			break;
		}
	if (!valid)
		return false;

	bool changed = menu->follow_animation_duration != duration;
	menu->follow_animation_duration = duration;
	if (menu_update_follow_animation_labels(menu->root, duration))
		changed = true;
	if (changed)
		menu_invalidate(menu, MENU_INVALIDATE_LAYOUT | MENU_INVALIDATE_TEXT);
	return changed;
}
