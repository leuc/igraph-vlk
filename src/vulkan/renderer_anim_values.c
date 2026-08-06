/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "vulkan/renderer_anim_values.h"

#include <math.h>

float renderer_anim_host_strength(float raw_value)
{
	if (!isfinite(raw_value) || raw_value <= 0.0f)
		return 0.0f;
	float strength = log1pf(raw_value);
	return isfinite(strength) && strength >= 0.0f ? strength : 0.0f;
}

float renderer_anim_reveal_at(int rank, float seconds_per_step)
{
	return rank > 0 && isfinite(seconds_per_step) && seconds_per_step > 0.0f ? rank * seconds_per_step : 0.0f;
}

float renderer_anim_base_strength(float raw_value, bool unweighted_fallback)
{
	float strength = renderer_anim_host_strength(raw_value);
	return strength > 0.0f || !unweighted_fallback ? strength : 1.0f;
}
