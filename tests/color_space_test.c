/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "test_utilities.h"
#include "vulkan/color_space.h"

#include <math.h>

static bool near(float actual, float expected, float tolerance)
{
	return fabsf(actual - expected) <= tolerance;
}

static int test_srgb_transfer(void)
{
	IGRAPH_ASSERT(near(color_srgb_to_linear(0.0f), 0.0f, 1e-7f));
	IGRAPH_ASSERT(near(color_srgb_to_linear(0.04045f), 0.0031308f, 1e-6f));
	IGRAPH_ASSERT(near(color_srgb_to_linear(1.0f), 1.0f, 1e-6f));
	IGRAPH_ASSERT(near(color_linear_to_srgb(0.0031308f), 0.04045f, 1e-5f));
	return 0;
}

static int test_bt709_white_stays_white(void)
{
	const float input[3] = {1.0f, 1.0f, 1.0f};
	float output[3];
	color_linear_bt709_to_bt2020(input, output);
	IGRAPH_ASSERT(near(output[0], 1.0f, 2e-6f));
	IGRAPH_ASSERT(near(output[1], 1.0f, 2e-6f));
	IGRAPH_ASSERT(near(output[2], 1.0f, 2e-6f));
	return 0;
}

static int test_st2084_reference_values(void)
{
	const float nits[] = {0.0f, 100.0f, 203.0f, 1000.0f, 10000.0f};
	for (size_t i = 0; i < sizeof(nits) / sizeof(nits[0]); i++) {
		float encoded = color_st2084_encode(nits[i]);
		float decoded = color_st2084_decode(encoded);
		float tolerance = nits[i] > 0.0f ? nits[i] * 0.001f : 0.001f;
		IGRAPH_ASSERT(near(decoded, nits[i], tolerance));
	}
	IGRAPH_ASSERT(near(color_st2084_encode(100.0f), 0.5080784f, 1e-5f));
	IGRAPH_ASSERT(near(color_st2084_encode(1000.0f), 0.7518271f, 1e-5f));
	return 0;
}

static int test_display_luminance_policy(void)
{
	DisplayColorInfo display = {.known = true, .hdr10 = true, .has_luminance = true, .reference_luminance = 203.0f, .max_luminance = 800.0f, .has_target_luminance = true, .target_max_luminance = 600.0f};
	RendererColorState state = renderer_color_state(VULKAN_OUTPUT_HDR10, &display);
	IGRAPH_ASSERT(state.reference_nits == 203.0f);
	IGRAPH_ASSERT(state.peak_nits == 600.0f);
	IGRAPH_ASSERT(state.highlight_nits == 600.0f);
	IGRAPH_ASSERT(near(renderer_map_hdr_relative(4.0f, &state) * state.reference_nits, 600.0f, 0.01f));
	IGRAPH_ASSERT(renderer_map_hdr_relative(1.0f, &state) == 1.0f);
	state = renderer_color_state(VULKAN_OUTPUT_SDR, &display);
	IGRAPH_ASSERT(state.peak_nits == state.reference_nits);
	IGRAPH_ASSERT(state.highlight_nits == state.reference_nits);
	return 0;
}

int main(void)
{
	RUN_TEST(test_srgb_transfer);
	RUN_TEST(test_bt709_white_stays_white);
	RUN_TEST(test_st2084_reference_values);
	RUN_TEST(test_display_luminance_policy);
	return 0;
}
