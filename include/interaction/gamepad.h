/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#pragma once

#include "interaction/camera.h"
#include <GLFW/glfw3.h>

int gamepad_get_first_active(void);
bool process_gamepad_input(int joystick_id, void *app_state, float delta_time);
