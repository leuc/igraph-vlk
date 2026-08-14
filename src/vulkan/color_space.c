/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "vulkan/color_space.h"

#include <math.h>

#define DEFAULT_REFERENCE_NITS 203.0f
#define DEFAULT_PEAK_NITS 1000.0f
#define ST2084_MAX_NITS 10000.0f

static float clamp_float(float value, float minimum, float maximum)
{
	if (value < minimum) {
		return minimum;
	}
	if (value > maximum) {
		return maximum;
	}
	return value;
}

float color_srgb_to_linear(float value)
{
	value = value < 0.0f ? 0.0f : value;
	return value <= 0.04045f ? value / 12.92f : powf((value + 0.055f) / 1.055f, 2.4f);
}

float color_linear_to_srgb(float value)
{
	value = value < 0.0f ? 0.0f : value;
	return value <= 0.0031308f ? value * 12.92f : 1.055f * powf(value, 1.0f / 2.4f) - 0.055f;
}

void color_linear_bt709_to_bt2020(const float input[3], float output[3])
{
	output[0] = 0.6274040f * input[0] + 0.3292820f * input[1] + 0.0433136f * input[2];
	output[1] = 0.0690970f * input[0] + 0.9195400f * input[1] + 0.0113612f * input[2];
	output[2] = 0.0163916f * input[0] + 0.0880132f * input[1] + 0.8955950f * input[2];
}

float color_st2084_encode(float nits)
{
	const float m1 = 2610.0f / 16384.0f;
	const float m2 = 2523.0f / 32.0f;
	const float c1 = 3424.0f / 4096.0f;
	const float c2 = 2413.0f / 128.0f;
	const float c3 = 2392.0f / 128.0f;
	float normalized = clamp_float(nits / ST2084_MAX_NITS, 0.0f, 1.0f);
	float powered = powf(normalized, m1);
	return powf((c1 + c2 * powered) / (1.0f + c3 * powered), m2);
}

float color_st2084_decode(float encoded)
{
	const float m1 = 2610.0f / 16384.0f;
	const float m2 = 2523.0f / 32.0f;
	const float c1 = 3424.0f / 4096.0f;
	const float c2 = 2413.0f / 128.0f;
	const float c3 = 2392.0f / 128.0f;
	float powered = powf(clamp_float(encoded, 0.0f, 1.0f), 1.0f / m2);
	float numerator = powered - c1;
	if (numerator < 0.0f) {
		numerator = 0.0f;
	}
	float denominator = c2 - c3 * powered;
	if (denominator <= 0.0f) {
		return ST2084_MAX_NITS;
	}
	return powf(numerator / denominator, 1.0f / m1) * ST2084_MAX_NITS;
}

RendererColorState renderer_color_state(VulkanOutputMode mode, const DisplayColorInfo *display)
{
	RendererColorState state = {.mode = mode, .reference_nits = DEFAULT_REFERENCE_NITS, .peak_nits = DEFAULT_PEAK_NITS, .highlight_nits = DEFAULT_REFERENCE_NITS * 4.0f};
	if (display) {
		if (display->has_luminance && display->reference_luminance > 0.0f) {
			state.reference_nits = display->reference_luminance;
		}
		if (display->has_target_luminance && display->target_max_luminance > 0.0f) {
			state.peak_nits = display->target_max_luminance;
		} else if (display->has_luminance && display->max_luminance > 0.0f) {
			state.peak_nits = display->max_luminance;
		}
	}
	state.reference_nits = clamp_float(state.reference_nits, 1.0f, ST2084_MAX_NITS);
	state.peak_nits = clamp_float(state.peak_nits, state.reference_nits, ST2084_MAX_NITS);
	state.highlight_nits = clamp_float(state.reference_nits * 4.0f, state.reference_nits, state.peak_nits);
	if (mode == VULKAN_OUTPUT_SDR) {
		state.peak_nits = state.reference_nits;
		state.highlight_nits = state.reference_nits;
	}
	return state;
}

float renderer_map_hdr_relative(float value, const RendererColorState *state)
{
	if (!state || value <= 1.0f) {
		return value < 0.0f ? 0.0f : value;
	}
	float relative_peak = state->highlight_nits / state->reference_nits;
	float t = clamp_float((value - 1.0f) / 3.0f, 0.0f, 1.0f);
	t = t * t * (3.0f - 2.0f * t);
	return 1.0f + (relative_peak - 1.0f) * t;
}
