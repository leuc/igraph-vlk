/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#pragma once

#include "app_state.h"
#include <stdbool.h>

void interaction_menu_toggle(AppState *state);
MenuNode *raycast_menu_crosshair(AppState *state);
MenuNode *raycast_menu_vr(AppState *state, vec3 ray_ori, vec3 ray_dir);
