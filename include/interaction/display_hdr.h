/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef INTERACTION_DISPLAY_HDR_H
#define INTERACTION_DISPLAY_HDR_H

#include "interaction/window.h"

#include <stdbool.h>
#include <stdint.h>

bool window_display_hdr_init(WindowState *window);
void window_display_hdr_cleanup(WindowState *window);
bool window_consume_display_hdr_status(WindowState *window, bool *known, bool *hdr10);
bool display_hdr_is_bt2020_primaries(int32_t r_x, int32_t r_y, int32_t g_x, int32_t g_y, int32_t b_x, int32_t b_y, int32_t w_x, int32_t w_y);
bool display_hdr_description_is_hdr10(bool has_st2084_pq, bool has_bt2020_primaries);

#endif // INTERACTION_DISPLAY_HDR_H
