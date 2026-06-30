/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#pragma once

#include "app_state.h"

/**
 * Register window-related GLFW callbacks (focus, framebuffer resize).
 * Called once during interaction_init.
 * @param window GLFW window to register callbacks for
 */
void window_init_callbacks(GLFWwindow *window);

/**
 * Toggle between fullscreen and windowed mode.
 * Saves/restores window position and size.
 * @param state Pointer to the application state
 */
void window_toggle_fullscreen(AppState *state);

/**
 * Move fullscreen window to an adjacent monitor.
 * @param state Pointer to the application state
 * @param direction -1 for left, +1 for right
 */
void window_cycle_monitor(AppState *state, int direction);
