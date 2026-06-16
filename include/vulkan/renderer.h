/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef RENDERER_H
#define RENDERER_H

#include "vulkan/commands.h"
#include "vulkan/device.h"
#include "vulkan/render_pass.h"
#include "vulkan/swapchain.h"
#include "vulkan/vulkan_types.h"

void renderer_update_graph(Renderer *r, GraphData *graph);
void renderer_render_ray(Renderer *r, VkCommandBuffer cmd, vec3 origin, vec3 dir, mat4 view, mat4 proj);

#endif
