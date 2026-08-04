/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "vulkan/renderer_anim.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vulkan/buffers.h"
#include "vulkan/utils.h"

static void renderer_anim_ensure_node_capacity(Renderer *r, uint32_t count)
{
	uint32_t capacity = count > 0 ? count : 1;
	if (r->anim.channels.node_capacity >= capacity)
		return;

	VK_DESTROY_BUFFER(r->core.device, r->anim.channels.node_step, r->anim.channels.node_step_memory);
	VK_DESTROY_BUFFER(r->core.device, r->anim.channels.node_value, r->anim.channels.node_value_memory);
	VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, sizeof(int) * capacity, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &r->anim.channels.node_step, &r->anim.channels.node_step_memory);
	VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, sizeof(float) * capacity, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &r->anim.channels.node_value, &r->anim.channels.node_value_memory);
	r->anim.channels.node_capacity = capacity;
}

static void renderer_anim_ensure_edge_capacity(Renderer *r, uint32_t count)
{
	uint32_t capacity = count > 0 ? count : 1;
	if (r->anim.channels.edge_capacity >= capacity)
		return;

	VK_DESTROY_BUFFER(r->core.device, r->anim.channels.edge_source, r->anim.channels.edge_source_memory);
	VK_DESTROY_BUFFER(r->core.device, r->anim.channels.edge_value, r->anim.channels.edge_value_memory);
	VK_DESTROY_BUFFER(r->core.device, r->anim.channels.edge_event_offsets, r->anim.channels.edge_event_offsets_memory);
	VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, sizeof(uint32_t) * capacity, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &r->anim.channels.edge_source, &r->anim.channels.edge_source_memory);
	VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, sizeof(float) * capacity, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &r->anim.channels.edge_value, &r->anim.channels.edge_value_memory);
	VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, sizeof(uint32_t) * (capacity + 1), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &r->anim.channels.edge_event_offsets, &r->anim.channels.edge_event_offsets_memory);
	r->anim.channels.edge_capacity = capacity;
	r->anim.channels.edge_event_offset_capacity = capacity + 1;
}

static void renderer_anim_ensure_event_capacity(Renderer *r, uint32_t count)
{
	uint32_t capacity = count > 0 ? count : 1;
	if (r->anim.channels.edge_event_capacity >= capacity)
		return;

	VK_DESTROY_BUFFER(r->core.device, r->anim.channels.edge_events, r->anim.channels.edge_events_memory);
	VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, sizeof(RendererAnimEvent) * capacity, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &r->anim.channels.edge_events, &r->anim.channels.edge_events_memory);
	r->anim.channels.edge_event_capacity = capacity;
}

static void renderer_anim_upload_ints(Renderer *r, VkDeviceMemory memory, const int *data, uint32_t count, int neutral)
{
	uint32_t upload_count = count > 0 ? count : 1;
	if (data) {
		update_buffer(r->core.device, memory, sizeof(int) * upload_count, data);
		return;
	}

	int *values = malloc(sizeof(int) * upload_count);
	if (!values)
		return;
	for (uint32_t i = 0; i < upload_count; i++)
		values[i] = neutral;
	update_buffer(r->core.device, memory, sizeof(int) * upload_count, values);
	free(values);
}

static void renderer_anim_upload_uints(Renderer *r, VkDeviceMemory memory, const uint32_t *data, uint32_t count, uint32_t neutral)
{
	uint32_t upload_count = count > 0 ? count : 1;
	if (data) {
		update_buffer(r->core.device, memory, sizeof(uint32_t) * upload_count, data);
		return;
	}

	uint32_t *values = malloc(sizeof(uint32_t) * upload_count);
	if (!values)
		return;
	for (uint32_t i = 0; i < upload_count; i++)
		values[i] = neutral;
	update_buffer(r->core.device, memory, sizeof(uint32_t) * upload_count, values);
	free(values);
}

static void renderer_anim_upload_floats(Renderer *r, VkDeviceMemory memory, const float *data, uint32_t count, float neutral)
{
	uint32_t upload_count = count > 0 ? count : 1;
	if (data) {
		update_buffer(r->core.device, memory, sizeof(float) * upload_count, data);
		return;
	}

	float *values = malloc(sizeof(float) * upload_count);
	if (!values)
		return;
	for (uint32_t i = 0; i < upload_count; i++)
		values[i] = neutral;
	update_buffer(r->core.device, memory, sizeof(float) * upload_count, values);
	free(values);
}

static void renderer_anim_write_channel_descriptors(Renderer *r)
{
	VkDescriptorBufferInfo node_step = {r->anim.channels.node_step, 0, sizeof(int) * r->anim.channels.node_capacity};
	VkDescriptorBufferInfo edge_source = {r->anim.channels.edge_source, 0, sizeof(uint32_t) * r->anim.channels.edge_capacity};
	VkDescriptorBufferInfo edge_value = {r->anim.channels.edge_value, 0, sizeof(float) * r->anim.channels.edge_capacity};
	VkDescriptorBufferInfo node_value = {r->anim.channels.node_value, 0, sizeof(float) * r->anim.channels.node_capacity};
	VkDescriptorBufferInfo edge_event_offsets = {r->anim.channels.edge_event_offsets, 0, sizeof(uint32_t) * r->anim.channels.edge_event_offset_capacity};
	VkDescriptorBufferInfo edge_events = {r->anim.channels.edge_events, 0, sizeof(RendererAnimEvent) * r->anim.channels.edge_event_capacity};

	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT * MAX_VIEWS * 4; i++) {
		VkWriteDescriptorSet writes[] = {
			VK_WRITE_DESC_BUFFER(r->descriptors.sets[i], 5, &node_step, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER), VK_WRITE_DESC_BUFFER(r->descriptors.sets[i], 6, &edge_source, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER), VK_WRITE_DESC_BUFFER(r->descriptors.sets[i], 7, &edge_value, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER), VK_WRITE_DESC_BUFFER(r->descriptors.sets[i], 8, &node_value, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER), VK_WRITE_DESC_BUFFER(r->descriptors.sets[i], 9, &edge_event_offsets, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER), VK_WRITE_DESC_BUFFER(r->descriptors.sets[i], 10, &edge_events, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
		};
		vkUpdateDescriptorSets(r->core.device, 6, writes, 0, NULL);
	}
}

void renderer_anim_init(Renderer *r)
{
	memset(&r->anim, 0, sizeof(r->anim));
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

	renderer_anim_clear(r);
}

void renderer_anim_update(Renderer *r, float time, float delta_time, uint32_t frame_count)
{
	r->anim.data.time = time;
	r->anim.data.delta_time = delta_time;
	r->anim.data.frame_count = frame_count;
	r->anim.data.seq_time = time - r->anim.seq_start_time;
	if (r->anim.data.seq_duration > 0.0f && r->anim.data.seq_time <= r->anim.data.seq_duration && frame_count % 60 == 0)
		fprintf(stderr, "[Animation] state: time=%.3fs/%.3fs stride=%.5fs dt=%.4fs frame=%u\n", r->anim.data.seq_time, r->anim.data.seq_duration, r->anim.data.seq_stride, r->anim.data.delta_time, r->anim.data.frame_count);
}

void renderer_anim_upload(Renderer *r, uint32_t ubo_idx)
{
	memcpy(r->anim.mapped[ubo_idx], &r->anim.data, sizeof(GlobalAnimState));
}

void renderer_anim_play(Renderer *r, const RendererAnimClip *clip)
{
	if (!r || !clip)
		return;

	renderer_anim_ensure_node_capacity(r, clip->node_count);
	renderer_anim_ensure_edge_capacity(r, clip->edge_count);
	renderer_anim_ensure_event_capacity(r, clip->edge_event_count);
	renderer_anim_upload_ints(r, r->anim.channels.node_step_memory, clip->node_steps, clip->node_count, 0);
	renderer_anim_upload_floats(r, r->anim.channels.node_value_memory, clip->node_values, clip->node_count, 1.0f);
	renderer_anim_upload_uints(r, r->anim.channels.edge_source_memory, clip->edge_sources, clip->edge_count, 0);
	renderer_anim_upload_floats(r, r->anim.channels.edge_value_memory, clip->edge_values, clip->edge_count, 1.0f);
	if (clip->edge_event_offsets)
		update_buffer(r->core.device, r->anim.channels.edge_event_offsets_memory, sizeof(uint32_t) * (clip->edge_count + 1), clip->edge_event_offsets);
	else
		renderer_anim_upload_uints(r, r->anim.channels.edge_event_offsets_memory, NULL, clip->edge_count + 1, 0);
	if (clip->edge_events && clip->edge_event_count > 0)
		update_buffer(r->core.device, r->anim.channels.edge_events_memory, sizeof(RendererAnimEvent) * clip->edge_event_count, clip->edge_events);
	else {
		RendererAnimEvent neutral = {0};
		update_buffer(r->core.device, r->anim.channels.edge_events_memory, sizeof(neutral), &neutral);
	}
	renderer_anim_write_channel_descriptors(r);

	int max_step = 0;
	if (clip->node_steps) {
		for (uint32_t i = 0; i < clip->node_count; i++)
			if (clip->node_steps[i] > max_step)
				max_step = clip->node_steps[i];
	}
	float event_end = 0.0f;
	for (uint32_t i = 0; clip->edge_events && i < clip->edge_event_count; i++) {
		float end = clip->edge_events[i].start_time + clip->edge_events[i].duration;
		if (end > event_end)
			event_end = end;
	}
	r->anim.data.seq_duration = clip->duration > event_end ? clip->duration : event_end;
	if (max_step == 0 && event_end == 0.0f)
		r->anim.data.seq_duration = 0.0f;
	r->anim.data.seq_stride = max_step > 0 ? r->anim.data.seq_duration / (float)max_step : 0.0f;
	r->anim.seq_start_time = r->anim.data.time;
	r->anim.data.seq_time = 0.0f;
	if (clip->node_steps || clip->node_values || clip->edge_values || clip->edge_event_count > 0)
		fprintf(stderr, "[Animation] start: nodes=%u edges=%u max_step=%d events=%u duration=%.3fs event_end=%.3fs stride=%.5fs\n", clip->node_count, clip->edge_count, max_step, clip->edge_event_count, r->anim.data.seq_duration, event_end, r->anim.data.seq_stride);
}

void renderer_anim_clear(Renderer *r)
{
	RendererAnimClip clip = {0};
	renderer_anim_play(r, &clip);
}

void renderer_anim_reset(Renderer *r, uint32_t node_count, uint32_t edge_count)
{
	RendererAnimClip clip = {.node_count = node_count, .edge_count = edge_count};
	renderer_anim_play(r, &clip);
}

void renderer_anim_cleanup(Renderer *r)
{
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT * MAX_VIEWS; i++)
		VK_DESTROY_BUFFER(r->core.device, r->anim.buffers[i], r->anim.memory[i]);
	VK_DESTROY_BUFFER(r->core.device, r->anim.channels.node_step, r->anim.channels.node_step_memory);
	VK_DESTROY_BUFFER(r->core.device, r->anim.channels.node_value, r->anim.channels.node_value_memory);
	VK_DESTROY_BUFFER(r->core.device, r->anim.channels.edge_source, r->anim.channels.edge_source_memory);
	VK_DESTROY_BUFFER(r->core.device, r->anim.channels.edge_value, r->anim.channels.edge_value_memory);
	VK_DESTROY_BUFFER(r->core.device, r->anim.channels.edge_event_offsets, r->anim.channels.edge_event_offsets_memory);
	VK_DESTROY_BUFFER(r->core.device, r->anim.channels.edge_events, r->anim.channels.edge_events_memory);
}
