/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef RENDERER_ANIM_H
#define RENDERER_ANIM_H

#include "vulkan/renderer_anim_values.h"
#include "vulkan/vulkan_types.h"

typedef struct
{
	const RendererAnimNode *nodes;
	const RendererAnimEdge *edges;
	uint32_t node_count;
	uint32_t edge_count;
	float strength_max;
	float fade;
	uint32_t reveal_mask;
	RendererAnimOwner owner;
} RendererAnimClip;

void renderer_anim_init(Renderer *r);
void renderer_anim_update(Renderer *r, float time, float delta_time, uint32_t frame_count);
void renderer_anim_upload(Renderer *r, uint32_t ubo_idx);
void renderer_anim_cleanup(Renderer *r);
bool renderer_anim_play(Renderer *r, const RendererAnimClip *clip);
void renderer_anim_clear(Renderer *r, const GraphData *graph);
void renderer_anim_reset(Renderer *r, const GraphData *graph);

#endif
