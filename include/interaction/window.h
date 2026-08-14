/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#pragma once

#include <stdbool.h>

#include "color_output.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

typedef struct DisplayHdrTracker DisplayHdrTracker;

/**
 * Window geometry, display mode, and platform capabilities.
 * Grouped for clean separation from the rest of AppState.
 */
typedef struct
{
	GLFWwindow *handle;
	bool is_fullscreen;
	bool can_position;
	int x, y, w, h;
	float content_scale_x;
	float content_scale_y;
	DisplayHdrTracker *displayHdrTracker;
	DisplayColorInfo displayColor;
	bool displayColorDirty;
} WindowState;

/* Opaque forward declaration — window.c includes app_state.h for the full type */
typedef struct AppState AppState;

/**
 * Create the GLFW window with platform-appropriate defaults.
 * Sizes the window to 80% of the primary monitor work area, centers it,
 * and applies platform-specific hints for Wayland/X11/macOS/Win32.
 * @param state Pointer to the application state (state->win is filled on success)
 * @return true on success, false on failure
 */
bool window_create(AppState *state);
void window_destroy(WindowState *win);

/**
 * Toggle between fullscreen and windowed mode.
 * Saves/restores window position and size.
 * @param win Pointer to the window state
 */
void window_toggle_fullscreen(WindowState *win);

/**
 * Move fullscreen window to an adjacent monitor.
 * @param win Pointer to the window state
 * @param direction -1 for left, +1 for right
 */
void window_cycle_monitor(WindowState *win, int direction);
