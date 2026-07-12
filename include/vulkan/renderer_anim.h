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

// Generic GPU upload helpers — used by all algorithm-specific wrappers
void renderer_anim_upload_node_ranks(Renderer *r, const int *ranks, uint32_t node_count, float total_duration);
void renderer_anim_upload_edge_from(Renderer *r, const uint32_t *from, uint32_t edge_count);
void renderer_anim_upload_edge_floats(Renderer *r, const float *data, uint32_t edge_count, float max_value);
void renderer_anim_reset_timer(Renderer *r);

// Algorithm-specific compute + upload
void renderer_anim_compute_bfs(Renderer *r, GraphData *graph);
void renderer_anim_compute_dfs(Renderer *r, GraphData *graph);
void renderer_anim_compute_topo(Renderer *r, GraphData *graph);

// Reset helpers
void renderer_anim_reset_nodes(Renderer *r, GraphData *graph);
void renderer_anim_reset_edges(Renderer *r);

#endif
