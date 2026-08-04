/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef RENDERER_ANIM_H
#define RENDERER_ANIM_H

#include "vulkan/vulkan_types.h"

typedef struct
{
	float start_time;
	float duration;
	float value;
	float _reserved;
} RendererAnimEvent;

typedef struct
{
	const int *node_steps;
	const float *node_values;
	const uint32_t *edge_sources;
	const float *edge_values;
	uint32_t node_count;
	uint32_t edge_count;
	float duration;
	const uint32_t *edge_event_offsets;
	const RendererAnimEvent *edge_events;
	uint32_t edge_event_count;
} RendererAnimClip;

void renderer_anim_init(Renderer *r);
void renderer_anim_update(Renderer *r, float time, float delta_time, uint32_t frame_count);
void renderer_anim_upload(Renderer *r, uint32_t ubo_idx);
void renderer_anim_cleanup(Renderer *r);
void renderer_anim_play(Renderer *r, const RendererAnimClip *clip);
void renderer_anim_clear(Renderer *r);
void renderer_anim_reset(Renderer *r, uint32_t node_count, uint32_t edge_count);

#endif
