/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "interaction/state.h"

#include <stdio.h>
#include <string.h>

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
