/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef RENDERER_ANIM_H
#define RENDERER_ANIM_H

#include "graph/graph_types.h"
#include "vulkan/vulkan_types.h"

void renderer_anim_init(Renderer *r);
void renderer_anim_update(Renderer *r, float time, float delta_time, uint32_t frame_count);
void renderer_anim_upload(Renderer *r, uint32_t ubo_idx);
void renderer_anim_cleanup(Renderer *r);
void renderer_anim_compute_bfs(Renderer *r, GraphData *graph);

#endif
