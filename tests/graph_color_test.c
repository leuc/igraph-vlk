/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "graph/graph_color.h"
#include "test_utilities.h"

#include <stdlib.h>
#include <string.h>

#define COLOR_SAMPLE_SIZE 100000

typedef struct
{
	uint32_t red;
	uint32_t green;
	uint32_t blue;
} ColorBits;

static int compare_color_bits(const void *left, const void *right)
{
	return memcmp(left, right, sizeof(ColorBits));
}

static int test_deterministic(void)
{
	GraphColor first = graph_color_generate(0x123456789ABCDEF0ULL);
	GraphColor second = graph_color_generate(0x123456789ABCDEF0ULL);
	IGRAPH_ASSERT(memcmp(&first, &second, sizeof(first)) == 0);
	return 0;
}

static int test_sequential_keys_do_not_repeat(void)
{
	ColorBits *colors = malloc(COLOR_SAMPLE_SIZE * sizeof(ColorBits));
	IGRAPH_ASSERT(colors != NULL);
	for (uint64_t key = 0; key < COLOR_SAMPLE_SIZE; key++) {
		GraphColor color = graph_color_generate(key);
		memcpy(&colors[key].red, &color.sdr_srgb[0], sizeof(uint32_t));
		memcpy(&colors[key].green, &color.sdr_srgb[1], sizeof(uint32_t));
		memcpy(&colors[key].blue, &color.sdr_srgb[2], sizeof(uint32_t));
	}
	qsort(colors, COLOR_SAMPLE_SIZE, sizeof(ColorBits), compare_color_bits);
	for (int i = 1; i < COLOR_SAMPLE_SIZE; i++)
		IGRAPH_ASSERT(memcmp(&colors[i - 1], &colors[i], sizeof(ColorBits)) != 0);
	free(colors);
	return 0;
}

static int test_gamuts_and_wide_gamut_use(void)
{
	bool found_outside_srgb = false;
	for (uint64_t key = 0; key < 10000; key++) {
		GraphColor color = graph_color_generate(key);
		for (int channel = 0; channel < 3; channel++) {
			IGRAPH_ASSERT(color.sdr_srgb[channel] >= 0.0f && color.sdr_srgb[channel] <= 1.0f);
			IGRAPH_ASSERT(color.hdr_linear_bt2020[channel] >= 0.0f && color.hdr_linear_bt2020[channel] <= 1.0f);
		}
		float red = 1.6604910f * color.hdr_linear_bt2020[0] - 0.5876411f * color.hdr_linear_bt2020[1] - 0.0728499f * color.hdr_linear_bt2020[2];
		float green = -0.1245505f * color.hdr_linear_bt2020[0] + 1.1328999f * color.hdr_linear_bt2020[1] - 0.0083494f * color.hdr_linear_bt2020[2];
		float blue = -0.0181508f * color.hdr_linear_bt2020[0] - 0.1005789f * color.hdr_linear_bt2020[1] + 1.1187297f * color.hdr_linear_bt2020[2];
		if (red < 0.0f || red > 1.0f || green < 0.0f || green > 1.0f || blue < 0.0f || blue > 1.0f)
			found_outside_srgb = true;
	}
	IGRAPH_ASSERT(found_outside_srgb);
	return 0;
}

static int test_key_hashes(void)
{
	IGRAPH_ASSERT(graph_color_hash_string("vertex") == graph_color_hash_string("vertex"));
	IGRAPH_ASSERT(graph_color_hash_string("vertex") != graph_color_hash_string("Vertex"));
	IGRAPH_ASSERT(graph_color_hash_u64(42) == graph_color_hash_u64(42));
	return 0;
}

int main(void)
{
	RUN_TEST(test_deterministic);
	RUN_TEST(test_sequential_keys_do_not_repeat);
	RUN_TEST(test_gamuts_and_wide_gamut_use);
	RUN_TEST(test_key_hashes);
	return EXIT_SUCCESS;
}
