/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef VULKAN_MENU_H
#define VULKAN_MENU_H

#include "interaction/state.h"
#include "vulkan/renderer.h"

bool renderer_menu_init(Renderer *renderer);
bool renderer_menu_update(Renderer *renderer, const MenuState *state, bool visible);
void renderer_menu_destroy(Renderer *renderer);

#endif // VULKAN_MENU_H
