/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef COLOR_OUTPUT_H
#define COLOR_OUTPUT_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
	float r_x;
	float r_y;
	float g_x;
	float g_y;
	float b_x;
	float b_y;
	float w_x;
	float w_y;
} ColorPrimaries;

typedef struct
{
	bool known;
	bool hdr10;
	bool has_primaries;
	bool has_target_primaries;
	bool has_luminance;
	bool has_target_luminance;
	ColorPrimaries primaries;
	ColorPrimaries target_primaries;
	float min_luminance;
	float max_luminance;
	float reference_luminance;
	float target_min_luminance;
	float target_max_luminance;
	float target_max_cll;
	float target_max_fall;
	uint32_t revision;
} DisplayColorInfo;

typedef enum {
	VULKAN_OUTPUT_SDR = 0,
	VULKAN_OUTPUT_HDR10 = 1,
} VulkanOutputMode;

typedef struct
{
	VulkanOutputMode mode;
	float reference_nits;
	float peak_nits;
	float highlight_nits;
} RendererColorState;

#endif
