/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#pragma once

#include "app_state.h"

/**
 * Create the GLFW window with platform-appropriate defaults.
 * Sizes the window to 80% of the primary monitor work area, centers it,
 * and applies platform-specific hints for Wayland/X11/macOS/Win32.
 * @param state Pointer to the application state (window field is set on success)
 * @return true on success, false on failure
 */
bool window_create(AppState *state);

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
