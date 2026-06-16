/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#pragma once

#include <openxr/openxr.h>
#include <stdbool.h>

typedef struct AppState AppState;

bool xr_init_vr(AppState *app);
void xr_process_input(AppState *app, float deltaTime);
void xr_render_frame(AppState *app, XrTime *last_predicted_display_time, int *consecutive_missed_frames);
