/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "vulkan/renderer_anim.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "vulkan/buffers.h"
#include "vulkan/renderer_criticality.h"
#include "vulkan/renderer_lifecycle.h"
#include "vulkan/utils.h"

static uint32_t renderer_anim_float_bits(float value)
{
	uint32_t bits;
	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static void renderer_anim_ensure_node_capacity(Renderer *r, uint32_t count)
{
	uint32_t capacity = count > 0 ? count : 1;
	if (r->anim.node_capacity >= capacity)
		return;

	renderer_wait_frames_idle(r);
	VK_DESTROY_BUFFER(r->core.device, r->anim.node_buffer, r->anim.node_memory);
	VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, sizeof(RendererAnimNode) * capacity, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &r->anim.node_buffer, &r->anim.node_memory);
	r->anim.node_capacity = capacity;
}

static void renderer_anim_ensure_edge_capacity(Renderer *r, uint32_t count)
{
	uint32_t capacity = count > 0 ? count : 1;
	if (r->anim.edge_capacity >= capacity)
		return;

	renderer_wait_frames_idle(r);
	VK_DESTROY_BUFFER(r->core.device, r->anim.edge_buffer, r->anim.edge_memory);
	VkDeviceSize size = sizeof(RendererAnimEdgeHeader) + sizeof(RendererAnimEdge) * capacity;
	VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, &r->anim.edge_buffer, &r->anim.edge_memory);
	r->anim.edge_capacity = capacity;
}

static void renderer_anim_write_descriptors(Renderer *r)
{
	VkDescriptorBufferInfo edge_info = {r->anim.edge_buffer, 0, sizeof(RendererAnimEdgeHeader) + sizeof(RendererAnimEdge) * r->anim.edge_capacity};
	VkDescriptorBufferInfo node_info = {r->anim.node_buffer, 0, sizeof(RendererAnimNode) * r->anim.node_capacity};
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT * MAX_VIEWS * 4; i++) {
		VkWriteDescriptorSet writes[] = {
			VK_WRITE_DESC_BUFFER(r->descriptors.sets[i], 2, &edge_info, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
			VK_WRITE_DESC_BUFFER(r->descriptors.sets[i], 5, &node_info, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
		};
		vkUpdateDescriptorSets(r->core.device, 2, writes, 0, NULL);
	}
}

static void renderer_anim_upload_neutral(Renderer *r)
{
	RendererAnimNode node = {0};
	RendererAnimEdgeHeader header = {0};
	RendererAnimEdge edge = {0};
	update_buffer(r->core.device, r->anim.node_memory, sizeof(node), &node);
	update_buffer(r->core.device, r->anim.edge_memory, sizeof(header), &header);
	update_buffer(r->core.device, r->anim.edge_memory, sizeof(edge), &edge);
}

void renderer_anim_init(Renderer *r)
{
	memset(&r->anim, 0, sizeof(r->anim));
	r->anim.data.fade = 0.3f;
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT * MAX_VIEWS; i++) {
		VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, sizeof(GlobalAnimState), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, &r->anim.buffers[i], &r->anim.memory[i]);
		VK_CHECK(vkMapMemory(r->core.device, r->anim.memory[i], 0, sizeof(GlobalAnimState), 0, &r->anim.mapped[i]), "Failed to map anim UBO memory");
	}

	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT * MAX_VIEWS * 4; i++) {
		int buffer_index = i % (MAX_FRAMES_IN_FLIGHT * MAX_VIEWS);
		VkDescriptorBufferInfo buffer_info = {r->anim.buffers[buffer_index], 0, sizeof(GlobalAnimState)};
		VkWriteDescriptorSet write = VK_WRITE_DESC_BUFFER(r->descriptors.sets[i], 4, &buffer_info, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
		vkUpdateDescriptorSets(r->core.device, 1, &write, 0, NULL);
	}

	renderer_anim_ensure_node_capacity(r, 0);
	renderer_anim_ensure_edge_capacity(r, 0);
	renderer_anim_upload_neutral(r);
	renderer_anim_write_descriptors(r);
}

void renderer_anim_update(Renderer *r, float time, float delta_time, uint32_t frame_count)
{
	r->anim.data.time = time;
	r->anim.data.delta_time = delta_time;
	r->anim.data.frame_count = frame_count;
	r->anim.data.playhead = fmaxf(0.0f, time - r->anim.start_time);
}

void renderer_anim_upload(Renderer *r, uint32_t ubo_idx)
{
	memcpy(r->anim.mapped[ubo_idx], &r->anim.data, sizeof(GlobalAnimState));
}

void renderer_anim_play(Renderer *r, const RendererAnimClip *clip)
{
	if (!r || !clip)
		return;

	if (r->anim.owner == RENDERER_ANIM_MAIN_PATH)
		renderer_cancel_main_path(r);
	renderer_wait_frames_idle(r);
	renderer_anim_ensure_node_capacity(r, clip->node_count);
	renderer_anim_ensure_edge_capacity(r, clip->edge_count);

	RendererAnimNode *nodes = calloc(r->anim.node_capacity, sizeof(RendererAnimNode));
	VkDeviceSize edge_size = sizeof(RendererAnimEdgeHeader) + sizeof(RendererAnimEdge) * r->anim.edge_capacity;
	unsigned char *edges = calloc(1, edge_size);
	if (!nodes || !edges) {
		free(nodes);
		free(edges);
		return;
	}
	if (clip->nodes && clip->node_count > 0)
		memcpy(nodes, clip->nodes, sizeof(RendererAnimNode) * clip->node_count);
	RendererAnimEdgeHeader *header = (RendererAnimEdgeHeader *)edges;
	float max_strength = clip->strength_max;
	if (!(max_strength > 0.0f) && clip->owner != RENDERER_ANIM_MAIN_PATH && clip->edges) {
		for (uint32_t i = 0; i < clip->edge_count; i++)
			if (isfinite(clip->edges[i].strength) && clip->edges[i].strength > max_strength)
				max_strength = clip->edges[i].strength;
	}
	if (!isfinite(max_strength) || max_strength < 0.0f)
		max_strength = 0.0f;
	header->strength_max_bits = renderer_anim_float_bits(max_strength);
	if (clip->edges && clip->edge_count > 0)
		memcpy(edges + sizeof(*header), clip->edges, sizeof(RendererAnimEdge) * clip->edge_count);

	update_buffer(r->core.device, r->anim.node_memory, sizeof(RendererAnimNode) * r->anim.node_capacity, nodes);
	update_buffer(r->core.device, r->anim.edge_memory, edge_size, edges);
	renderer_anim_write_descriptors(r);

	r->anim.start_time = r->anim.data.time;
	r->anim.data.playhead = 0.0f;
	r->anim.data.fade = clip->fade > 0.0f ? clip->fade : 0.3f;
	r->anim.data.reveal_mask = clip->reveal_mask;
	r->anim.owner = clip->owner;

	free(nodes);
	free(edges);
}

void renderer_anim_clear(Renderer *r, const GraphData *graph)
{
	if (!r || !graph)
		return;

	RendererAnimNode *nodes = calloc(graph->node_count > 0 ? graph->node_count : 1, sizeof(RendererAnimNode));
	RendererAnimEdge *edges = calloc(graph->edge_count > 0 ? graph->edge_count : 1, sizeof(RendererAnimEdge));
	if (!nodes || !edges) {
		free(nodes);
		free(edges);
		return;
	}
	bool all_zero = graph->edge_count > 0;
	float max_strength = 0.0f;
	for (uint32_t e = 0; e < graph->edge_count; e++) {
		edges[e].strength = renderer_anim_base_strength(graph->edges[e].weight, false);
		if (edges[e].strength > 0.0f)
			all_zero = false;
		if (edges[e].strength > max_strength)
			max_strength = edges[e].strength;
	}
	if (all_zero) {
		max_strength = 1.0f;
		for (uint32_t e = 0; e < graph->edge_count; e++)
			edges[e].strength = renderer_anim_base_strength(graph->edges[e].weight, true);
	}
	RendererAnimClip clip = {
		.nodes = nodes,
		.edges = edges,
		.node_count = graph->node_count,
		.edge_count = graph->edge_count,
		.strength_max = max_strength,
		.fade = 0.3f,
		.reveal_mask = 0,
		.owner = RENDERER_ANIM_NONE,
	};
	renderer_anim_play(r, &clip);
	free(nodes);
	free(edges);
}

void renderer_anim_reset(Renderer *r, const GraphData *graph)
{
	renderer_anim_clear(r, graph);
}

void renderer_anim_cleanup(Renderer *r)
{
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT * MAX_VIEWS; i++) {
		if (r->anim.mapped[i])
			vkUnmapMemory(r->core.device, r->anim.memory[i]);
		VK_DESTROY_BUFFER(r->core.device, r->anim.buffers[i], r->anim.memory[i]);
	}
	VK_DESTROY_BUFFER(r->core.device, r->anim.node_buffer, r->anim.node_memory);
	VK_DESTROY_BUFFER(r->core.device, r->anim.edge_buffer, r->anim.edge_memory);
}
