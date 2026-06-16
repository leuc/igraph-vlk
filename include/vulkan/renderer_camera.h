/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef RENDERER_CAMERA_H
#define RENDERER_CAMERA_H

#include "vulkan/vulkan_types.h"

void renderer_update_view(Renderer *r, vec3 pos, vec3 front, vec3 up);

#endif
