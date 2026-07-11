/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#pragma once

#include "app_state.h"
#include <stdbool.h>

/**
 * Toggle the 3D spherical menu on/off.
 */
void interaction_menu_toggle(AppState *state);

/**
 * Raycast from the crosshair (center of screen) to pick a menu node, and mark it hovered.
 * This is the desktop/gamepad pointer: the cursor is captured while the menu is open, so
 * screen-space cursor coordinates are meaningless and must never be used to pick.
 */
MenuNode *raycast_menu_crosshair(AppState *state);

/**
 * Clear the hovered flag from all nodes in the menu tree.
 */
void menu_hover_clear_recursive(MenuNode *node);
