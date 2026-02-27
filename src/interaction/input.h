#pragma once

#include <stdbool.h>
#include "app_state.h"

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

/**
 * Get the current monitor for fullscreen handling.
 * @param window GLFW window
 * @return Pointer to the monitor to use
 */
GLFWmonitor* interaction_get_current_monitor(GLFWwindow *window);

