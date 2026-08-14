/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef VULKAN_COLOR_SPACE_H
#define VULKAN_COLOR_SPACE_H

#include "color_output.h"

float color_srgb_to_linear(float value);
float color_linear_to_srgb(float value);
void color_linear_bt709_to_bt2020(const float input[3], float output[3]);
void color_srgb_to_linear_bt2020(const float input[3], float output[3]);
float color_st2084_encode(float nits);
float color_st2084_decode(float encoded);
RendererColorState renderer_color_state(VulkanOutputMode mode, const DisplayColorInfo *display);
float renderer_map_hdr_relative(float value, const RendererColorState *state);

#endif
