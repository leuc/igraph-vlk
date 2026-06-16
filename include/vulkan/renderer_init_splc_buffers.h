/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef RENDERER_INIT_SPLC_BUFFERS_H
#define RENDERER_INIT_SPLC_BUFFERS_H

#include "vulkan/vulkan_types.h"

typedef struct
{
	VkBuffer buf;
	VkDeviceMemory mem;
} BufPair;

void splc_save_old_buffers(Renderer *r, BufPair out[5]);

#endif
