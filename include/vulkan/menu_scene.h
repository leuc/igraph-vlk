/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef VULKAN_MENU_SCENE_H
#define VULKAN_MENU_SCENE_H

#include "interaction/state.h"
#include "vulkan/menu_instance_types.h"
#include "vulkan/text.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct
{
	void *context;
	bool (*resolve)(void *context, const char *text, TextRegion *region);
	float (*measure_width)(void *context, const char *text);
} MenuTextProvider;

typedef struct
{
	MenuInstance *instances;
	size_t instance_count;
	size_t instance_capacity;
	TextQuadInstance *text_instances;
	size_t text_instance_count;
	size_t text_instance_capacity;
} MenuScene;

bool menu_scene_build(const MenuState *menu, const MenuTextProvider *text, MenuScene *scene);
void menu_scene_destroy(MenuScene *scene);

#endif
