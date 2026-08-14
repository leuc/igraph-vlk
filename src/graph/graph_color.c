/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "graph/graph_color.h"

#include <math.h>

static uint64_t graph_color_mix(uint64_t value)
{
	value += 0x9E3779B97F4A7C15ULL;
	value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ULL;
	value = (value ^ (value >> 27)) * 0x94D049BB133111EBULL;
	return value ^ (value >> 31);
}

static float hash_unit_float(uint64_t value)
{
	return (float)((double)(value >> 11) * (1.0 / 9007199254740992.0));
}

static float srgb_to_linear(float value)
{
	return value <= 0.04045f ? value / 12.92f : powf((value + 0.055f) / 1.055f, 2.4f);
}

static float linear_to_srgb(float value)
{
	return value <= 0.0031308f ? value * 12.92f : 1.055f * powf(value, 1.0f / 2.4f) - 0.055f;
}

static void linear_srgb_to_bt2020(const float input[3], float output[3])
{
	output[0] = 0.6274040f * input[0] + 0.3292820f * input[1] + 0.0433136f * input[2];
	output[1] = 0.0690970f * input[0] + 0.9195400f * input[1] + 0.0113612f * input[2];
	output[2] = 0.0163916f * input[0] + 0.0880132f * input[1] + 0.8955950f * input[2];
}

static void oklch_to_linear_srgb(float lightness, float chroma, float hue, float output[3])
{
	float a = chroma * cosf(hue);
	float b = chroma * sinf(hue);
	float l = lightness + 0.3963377774f * a + 0.2158037573f * b;
	float m = lightness - 0.1055613458f * a - 0.0638541728f * b;
	float s = lightness - 0.0894841775f * a - 1.2914855480f * b;
	l = l * l * l;
	m = m * m * m;
	s = s * s * s;
	output[0] = 4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s;
	output[1] = -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s;
	output[2] = -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s;
}

static bool in_unit_gamut(const float color[3])
{
	return color[0] >= 0.0f && color[0] <= 1.0f && color[1] >= 0.0f && color[1] <= 1.0f && color[2] >= 0.0f && color[2] <= 1.0f;
}

static void gamut_mapped_oklch(float lightness, float chroma, float hue, bool bt2020, float output[3])
{
	float linear_srgb[3];
	float candidate[3];
	oklch_to_linear_srgb(lightness, chroma, hue, linear_srgb);
	if (bt2020)
		linear_srgb_to_bt2020(linear_srgb, candidate);
	else
		candidate[0] = linear_srgb[0], candidate[1] = linear_srgb[1], candidate[2] = linear_srgb[2];
	if (!in_unit_gamut(candidate)) {
		float low = 0.0f;
		float high = chroma;
		for (int i = 0; i < 24; i++) {
			float middle = (low + high) * 0.5f;
			oklch_to_linear_srgb(lightness, middle, hue, linear_srgb);
			if (bt2020)
				linear_srgb_to_bt2020(linear_srgb, candidate);
			else
				candidate[0] = linear_srgb[0], candidate[1] = linear_srgb[1], candidate[2] = linear_srgb[2];
			if (in_unit_gamut(candidate))
				low = middle;
			else
				high = middle;
		}
		oklch_to_linear_srgb(lightness, low, hue, linear_srgb);
		if (bt2020)
			linear_srgb_to_bt2020(linear_srgb, candidate);
		else
			candidate[0] = linear_srgb[0], candidate[1] = linear_srgb[1], candidate[2] = linear_srgb[2];
	}
	for (int i = 0; i < 3; i++)
		output[i] = fminf(fmaxf(candidate[i], 0.0f), 1.0f);
}

uint64_t graph_color_hash_string(const char *value)
{
	uint64_t hash = 14695981039346656037ULL;
	if (!value)
		return graph_color_mix(hash);
	for (; *value; value++) {
		hash ^= (unsigned char)*value;
		hash *= 1099511628211ULL;
	}
	return graph_color_mix(hash);
}

uint64_t graph_color_hash_u64(uint64_t value)
{
	return graph_color_mix(value);
}

GraphColor graph_color_generate(uint64_t key)
{
	const float tau = 6.2831853071795864769f;
	float hue = hash_unit_float(graph_color_mix(key ^ 0x243F6A8885A308D3ULL)) * tau;
	float lightness = 0.65f + hash_unit_float(graph_color_mix(key ^ 0x13198A2E03707344ULL)) * 0.18f;
	float chroma = 0.14f + hash_unit_float(graph_color_mix(key ^ 0xA4093822299F31D0ULL)) * 0.10f;
	GraphColor color;
	float linear_srgb[3];
	gamut_mapped_oklch(lightness, chroma, hue, false, linear_srgb);
	for (int i = 0; i < 3; i++)
		color.sdr_srgb[i] = fminf(fmaxf(linear_to_srgb(linear_srgb[i]), 0.0f), 1.0f);
	gamut_mapped_oklch(lightness, chroma, hue, true, color.hdr_linear_bt2020);
	return color;
}

GraphColor graph_color_from_srgb(float red, float green, float blue)
{
	GraphColor color = {.sdr_srgb = {red, green, blue}};
	float linear_srgb[3] = {srgb_to_linear(red), srgb_to_linear(green), srgb_to_linear(blue)};
	linear_srgb_to_bt2020(linear_srgb, color.hdr_linear_bt2020);
	return color;
}

void graph_reset_emphasis(GraphData *data)
{
	if (!data)
		return;
	for (uint32_t i = 0; i < data->node_count; i++)
		data->nodes[i].emphasis = EMPHASIS_FULL;
	for (uint32_t i = 0; i < data->edge_count; i++)
		data->edges[i].emphasis = EMPHASIS_FULL;
}
