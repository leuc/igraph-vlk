/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "vulkan/renderer_anim.h"

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
	VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, sizeof(uint32_t) * capacity, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &r->anim.channels.edge_source, &r->anim.channels.edge_source_memory);
	VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, sizeof(float) * capacity, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &r->anim.channels.edge_value, &r->anim.channels.edge_value_memory);
	r->anim.channels.edge_capacity = capacity;
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

	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT * MAX_VIEWS * 4; i++) {
		VkWriteDescriptorSet writes[] = {
			VK_WRITE_DESC_BUFFER(r->descriptors.sets[i], 5, &node_step, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
			VK_WRITE_DESC_BUFFER(r->descriptors.sets[i], 6, &edge_source, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
			VK_WRITE_DESC_BUFFER(r->descriptors.sets[i], 7, &edge_value, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
			VK_WRITE_DESC_BUFFER(r->descriptors.sets[i], 8, &node_value, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
		};
		vkUpdateDescriptorSets(r->core.device, 4, writes, 0, NULL);
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
	renderer_anim_upload_ints(r, r->anim.channels.node_step_memory, clip->node_steps, clip->node_count, 0);
	renderer_anim_upload_floats(r, r->anim.channels.node_value_memory, clip->node_values, clip->node_count, 1.0f);
	renderer_anim_upload_uints(r, r->anim.channels.edge_source_memory, clip->edge_sources, clip->edge_count, 0);
	renderer_anim_upload_floats(r, r->anim.channels.edge_value_memory, clip->edge_values, clip->edge_count, 1.0f);
	renderer_anim_write_channel_descriptors(r);

	int max_step = 0;
	if (clip->node_steps) {
		for (uint32_t i = 0; i < clip->node_count; i++)
			if (clip->node_steps[i] > max_step)
				max_step = clip->node_steps[i];
	}
	r->anim.data.seq_duration = (max_step > 0 && clip->duration > 0.0f) ? clip->duration : 0.0f;
	r->anim.data.seq_stride = max_step > 0 ? r->anim.data.seq_duration / (float)max_step : 0.0f;
	r->anim.seq_start_time = r->anim.data.time;
	r->anim.data.seq_time = 0.0f;
}

void renderer_anim_clear(Renderer *r)
{
	RendererAnimClip clip = {0};
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
}
