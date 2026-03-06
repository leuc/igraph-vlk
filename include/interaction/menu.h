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

/**
 * Clear the focused flag from all nodes in the menu tree.
 */
void clear_menu_focus_recursive(MenuNode *node);

/**
 * Find the currently focused menu node.
 */
MenuNode *find_focused_node(MenuNode *node);

/**
 * Set focus to a specific node, clearing focus from all others.
 */
void set_menu_node_focused(MenuNode *root, MenuNode *target);

/**
 * Handle keyboard input for focused menu input fields.
 * Returns true if the key was handled, false otherwise.
 */
bool handle_menu_key_input(MenuNode *root, int key, int scancode, int action, int mods);
