/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "vulkan/renderer_camera.h"

void renderer_update_view(Renderer *r, vec3 pos, vec3 front, vec3 up)
{
	vec3 c;
	glm_vec3_add(pos, front, c);
	glm_lookat(pos, c, up, r->ubo.view);
}
