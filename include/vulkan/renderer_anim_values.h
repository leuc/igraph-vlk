/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef RENDERER_ANIM_VALUES_H
#define RENDERER_ANIM_VALUES_H

#include <stdbool.h>

float renderer_anim_host_strength(float raw_value);
float renderer_anim_reveal_at(int rank, float seconds_per_step);
float renderer_anim_base_strength(float raw_value, bool unweighted_fallback);

#endif
