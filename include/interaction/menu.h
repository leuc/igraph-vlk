#pragma once

#include "app_state.h"
#include <stdbool.h>

/**
 * Pick a menu node when the sphere menu is open using mouse coordinates.
 */
MenuNode *interaction_pick_menu_node(AppState *state, double mouse_x, double mouse_y);

/**
 * Raycast from the crosshair (center of screen) to pick a menu node.
 * Used for immersive 3D menu interaction without releasing mouse lock.
 */
MenuNode *raycast_menu_crosshair(AppState *state);

/**
 * Clear the hovered flag from all nodes in the menu tree.
 */
void clear_menu_hover_recursive(MenuNode *node);
