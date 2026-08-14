/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef VULKAN_RENDERER_PRESENT_H
#define VULKAN_RENDERER_PRESENT_H

#include "vulkan/vulkan_types.h"

void renderer_present_init(Renderer *r);
void renderer_present_recreate(Renderer *r);
void renderer_present_record(Renderer *r, VkCommandBuffer command_buffer, uint32_t image_index);
void renderer_present_destroy(Renderer *r);

#endif
