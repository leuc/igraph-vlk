/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "vulkan/renderer_anim_values.h"

#include <assert.h>
#include <float.h>
#include <math.h>

int main(void)
{
	assert(renderer_anim_reveal_at(0, 0.5f) == 0.0f);
	assert(renderer_anim_reveal_at(-5, 0.5f) == 0.0f);
	assert(fabsf(renderer_anim_reveal_at(4, 0.25f) - 1.0f) < 1e-6f);
	assert(renderer_anim_reveal_at_or_dim(4, 0.25f, true) == 1.0f);
	assert(renderer_anim_reveal_at_or_dim(-1, 0.25f, true) == FLT_MAX);
	assert(renderer_anim_reveal_at_or_dim(-1, 0.25f, false) == 0.0f);
	assert(fabsf(renderer_anim_host_strength(1.0f) - log1pf(1.0f)) < 1e-6f);
	assert(renderer_anim_host_strength(-1.0f) == 0.0f);
	assert(renderer_anim_host_strength(INFINITY) == 0.0f);
	assert(renderer_anim_base_strength(0.0f, true) == 1.0f);
	assert(renderer_anim_base_strength(0.0f, false) == 0.0f);
	return 0;
}
