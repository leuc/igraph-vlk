/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#pragma once

#include "app_state.h"
#include <stdbool.h>

/**
 * Initialize input handling (register callbacks).
 * @param window GLFW window to register callbacks for
 */
void interaction_init(GLFWwindow *window);

/**
 * Process continuous input (WASD movement).
 * Should be called every frame.
 * @param state Pointer to the application state
 * @param delta_time Time since last frame in seconds
 */
void interaction_process_continuous_input(AppState *state, float delta_time);
