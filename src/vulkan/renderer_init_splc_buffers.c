/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "vulkan/renderer_init_splc_buffers.h"

#include "vulkan/vulkan_types.h"

void splc_save_old_buffers(Renderer *r, BufPair out[5])
{
	out[0].buf = r->splc.nodes_buffer;
	out[0].mem = r->splc.nodes_memory;
	out[1].buf = r->splc.edges_buffer;
	out[1].mem = r->splc.edges_memory;
	out[2].buf = r->splc.traffic_buffer;
	out[2].mem = r->splc.traffic_memory;
	out[3].buf = r->splc.level_buffer;
	out[3].mem = r->splc.level_memory;
	out[4].buf = r->splc.max_buffer;
	out[4].mem = r->splc.max_memory;
}
