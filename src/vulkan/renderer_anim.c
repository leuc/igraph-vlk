/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "vulkan/renderer_anim.h"

#include <stdlib.h>
#include <string.h>

#include "vulkan/buffers.h"
#include "vulkan/utils.h"

static float g_bfs_start_time = 0.0f;

void renderer_anim_init(Renderer *r)
{
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT * MAX_VIEWS; i++) {
		VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, sizeof(GlobalAnimState), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, &r->anim.buffers[i], &r->anim.memory[i]);
		VK_CHECK(vkMapMemory(r->core.device, r->anim.memory[i], 0, sizeof(GlobalAnimState), 0, &r->anim.mapped[i]), "Failed to map anim UBO memory");
	}

	// Write binding 4 for ALL descriptor sets (4 groups * MAX_FRAMES_IN_FLIGHT * MAX_VIEWS).
	// Groups 1-3 (text_quad, node_label, detail_card) will have bindings 0-1
	// rewritten later by menu.c / renderer_update_node_labels.c, but binding 4
	// is never touched again after this.
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT * MAX_VIEWS * 4; i++) {
		int buf_idx = i % (MAX_FRAMES_IN_FLIGHT * MAX_VIEWS);
		VkDescriptorBufferInfo bufInfo = {r->anim.buffers[buf_idx], 0, sizeof(GlobalAnimState)};
		VkWriteDescriptorSet write = VK_WRITE_DESC_BUFFER(r->descriptors.sets[i], 4, &bufInfo, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
		vkUpdateDescriptorSets(r->core.device, 1, &write, 0, NULL);
	}

	memset(&r->anim.data, 0, sizeof(GlobalAnimState));
}

void renderer_anim_update(Renderer *r, float time, float delta_time, uint32_t frame_count)
{
	r->anim.data.time = time;
	r->anim.data.delta_time = delta_time;
	r->anim.data.frame_count = frame_count;
}

void renderer_anim_upload(Renderer *r, uint32_t ubo_idx)
{
	GlobalAnimState upload = r->anim.data;
	upload.time = r->anim.data.time - g_bfs_start_time;
	memcpy(r->anim.mapped[ubo_idx], &upload, sizeof(GlobalAnimState));
}

void renderer_anim_cleanup(Renderer *r)
{
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT * MAX_VIEWS; i++)
		VK_DESTROY_BUFFER(r->core.device, r->anim.buffers[i], r->anim.memory[i]);
	VK_DESTROY_BUFFER(r->core.device, r->bfs.rank_buf, r->bfs.rank_mem);
	VK_DESTROY_BUFFER(r->core.device, r->bfs.from_buf, r->bfs.from_mem);
}

void renderer_anim_compute_bfs(Renderer *r, GraphData *graph)
{
	if (graph->node_count == 0)
		return;

	int source = 0;
	for (int i = 1; i < (int)graph->node_count; i++)
		if (graph->nodes[i].degree > graph->nodes[source].degree)
			source = i;

	igraph_vector_int_t order;
	if (igraph_vector_int_init(&order, 0) != IGRAPH_SUCCESS) {
		fprintf(stderr, "[BFS] Failed to init order vector\n");
		return;
	}
	igraph_error_t bfs_ret = igraph_bfs_simple(&graph->g, source, IGRAPH_ALL, &order, NULL, NULL);
	if (bfs_ret != IGRAPH_SUCCESS) {
		fprintf(stderr, "[BFS] igraph_bfs_simple failed: %s\n", igraph_strerror(bfs_ret));
		igraph_vector_int_destroy(&order);
		return;
	}

	int *ranks = malloc(sizeof(int) * graph->node_count);
	for (int i = 0; i < (int)graph->node_count; i++)
		ranks[i] = -5;

	int order_len = igraph_vector_int_size(&order);
	for (int i = 0; i < order_len; i++)
		ranks[VECTOR(order)[i]] = i;
	igraph_vector_int_destroy(&order);

	uint32_t *from = malloc(sizeof(uint32_t) * graph->edge_count);
	for (int i = 0; i < (int)graph->edge_count; i++)
		from[i] = graph->edges[i].from;

	VkDeviceSize rank_size = sizeof(int) * graph->node_count;
	VkDeviceSize from_size = sizeof(uint32_t) * graph->edge_count;

	if (r->bfs.rank_buf == VK_NULL_HANDLE || r->bfs.node_count < graph->node_count) {
		VK_DESTROY_BUFFER(r->core.device, r->bfs.rank_buf, r->bfs.rank_mem);
		VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, rank_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &r->bfs.rank_buf, &r->bfs.rank_mem);
	}
	if (r->bfs.from_buf == VK_NULL_HANDLE || r->bfs.edge_count < graph->edge_count) {
		VK_DESTROY_BUFFER(r->core.device, r->bfs.from_buf, r->bfs.from_mem);
		VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, from_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &r->bfs.from_buf, &r->bfs.from_mem);
	}

	update_buffer(r->core.device, r->bfs.rank_mem, rank_size, ranks);
	update_buffer(r->core.device, r->bfs.from_mem, from_size, from);

	r->bfs.node_count = graph->node_count;
	r->bfs.edge_count = graph->edge_count;

	float total_duration = 3.0f;
	r->anim.data._pad = (order_len > 1) ? total_duration / order_len : total_duration;

	VkDescriptorBufferInfo rank_info = {r->bfs.rank_buf, 0, rank_size};
	VkDescriptorBufferInfo from_info = {r->bfs.from_buf, 0, from_size};
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT * MAX_VIEWS * 4; i++) {
		VkWriteDescriptorSet writes[] = {
			VK_WRITE_DESC_BUFFER(r->descriptors.sets[i], 5, &rank_info, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
			VK_WRITE_DESC_BUFFER(r->descriptors.sets[i], 6, &from_info, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
		};
		vkUpdateDescriptorSets(r->core.device, 2, writes, 0, NULL);
	}

	g_bfs_start_time = r->anim.data.time;

	free(ranks);
	free(from);
}

void renderer_anim_compute_dfs(Renderer *r, GraphData *graph)
{
	if (graph->node_count == 0)
		return;

	int source = 0;
	for (int i = 1; i < (int)graph->node_count; i++)
		if (graph->nodes[i].degree > graph->nodes[source].degree)
			source = i;

	igraph_vector_int_t order;
	if (igraph_vector_int_init(&order, 0) != IGRAPH_SUCCESS) {
		fprintf(stderr, "[DFS] Failed to init order vector\n");
		return;
	}
	igraph_error_t dfs_ret = igraph_dfs(&graph->g, source, IGRAPH_ALL, true, &order, NULL, NULL, NULL, NULL, NULL, NULL);
	if (dfs_ret != IGRAPH_SUCCESS) {
		fprintf(stderr, "[DFS] igraph_dfs failed: %s\n", igraph_strerror(dfs_ret));
		igraph_vector_int_destroy(&order);
		return;
	}

	int *ranks = malloc(sizeof(int) * graph->node_count);
	for (int i = 0; i < (int)graph->node_count; i++)
		ranks[i] = -5;

	int order_len = igraph_vector_int_size(&order);
	for (int i = 0; i < order_len; i++)
		ranks[VECTOR(order)[i]] = i;
	igraph_vector_int_destroy(&order);

	uint32_t *from = malloc(sizeof(uint32_t) * graph->edge_count);
	for (int i = 0; i < (int)graph->edge_count; i++)
		from[i] = graph->edges[i].from;

	VkDeviceSize rank_size = sizeof(int) * graph->node_count;
	VkDeviceSize from_size = sizeof(uint32_t) * graph->edge_count;

	if (r->bfs.rank_buf == VK_NULL_HANDLE || r->bfs.node_count < graph->node_count) {
		VK_DESTROY_BUFFER(r->core.device, r->bfs.rank_buf, r->bfs.rank_mem);
		VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, rank_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &r->bfs.rank_buf, &r->bfs.rank_mem);
	}
	if (r->bfs.from_buf == VK_NULL_HANDLE || r->bfs.edge_count < graph->edge_count) {
		VK_DESTROY_BUFFER(r->core.device, r->bfs.from_buf, r->bfs.from_mem);
		VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, from_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &r->bfs.from_buf, &r->bfs.from_mem);
	}

	update_buffer(r->core.device, r->bfs.rank_mem, rank_size, ranks);
	update_buffer(r->core.device, r->bfs.from_mem, from_size, from);

	r->bfs.node_count = graph->node_count;
	r->bfs.edge_count = graph->edge_count;

	float total_duration = 3.0f;
	r->anim.data._pad = (order_len > 1) ? total_duration / order_len : total_duration;

	VkDescriptorBufferInfo rank_info = {r->bfs.rank_buf, 0, rank_size};
	VkDescriptorBufferInfo from_info = {r->bfs.from_buf, 0, from_size};
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT * MAX_VIEWS * 4; i++) {
		VkWriteDescriptorSet writes[] = {
			VK_WRITE_DESC_BUFFER(r->descriptors.sets[i], 5, &rank_info, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
			VK_WRITE_DESC_BUFFER(r->descriptors.sets[i], 6, &from_info, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
		};
		vkUpdateDescriptorSets(r->core.device, 2, writes, 0, NULL);
	}

	g_bfs_start_time = r->anim.data.time;

	free(ranks);
	free(from);
}

void renderer_anim_compute_topo(Renderer *r, GraphData *graph)
{
	if (graph->node_count == 0)
		return;

	igraph_vector_int_t order;
	if (igraph_vector_int_init(&order, 0) != IGRAPH_SUCCESS) {
		fprintf(stderr, "[Topo] Failed to init order vector\n");
		return;
	}
	igraph_error_t topo_ret = igraph_topological_sorting(&graph->g, &order, IGRAPH_OUT);
	if (topo_ret != IGRAPH_SUCCESS) {
		fprintf(stderr, "[Topo] Graph is not a DAG (topological sort failed)\n");
		igraph_vector_int_destroy(&order);
		return;
	}

	int *ranks = malloc(sizeof(int) * graph->node_count);
	for (int i = 0; i < (int)graph->node_count; i++)
		ranks[i] = -5;

	int order_len = igraph_vector_int_size(&order);
	for (int i = 0; i < order_len; i++)
		ranks[VECTOR(order)[i]] = i;
	igraph_vector_int_destroy(&order);

	uint32_t *from = malloc(sizeof(uint32_t) * graph->edge_count);
	for (int i = 0; i < (int)graph->edge_count; i++)
		from[i] = graph->edges[i].from;

	VkDeviceSize rank_size = sizeof(int) * graph->node_count;
	VkDeviceSize from_size = sizeof(uint32_t) * graph->edge_count;

	if (r->bfs.rank_buf == VK_NULL_HANDLE || r->bfs.node_count < graph->node_count) {
		VK_DESTROY_BUFFER(r->core.device, r->bfs.rank_buf, r->bfs.rank_mem);
		VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, rank_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &r->bfs.rank_buf, &r->bfs.rank_mem);
	}
	if (r->bfs.from_buf == VK_NULL_HANDLE || r->bfs.edge_count < graph->edge_count) {
		VK_DESTROY_BUFFER(r->core.device, r->bfs.from_buf, r->bfs.from_mem);
		VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, from_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &r->bfs.from_buf, &r->bfs.from_mem);
	}

	update_buffer(r->core.device, r->bfs.rank_mem, rank_size, ranks);
	update_buffer(r->core.device, r->bfs.from_mem, from_size, from);

	r->bfs.node_count = graph->node_count;
	r->bfs.edge_count = graph->edge_count;

	float total_duration = 3.0f;
	r->anim.data._pad = (order_len > 1) ? total_duration / order_len : total_duration;

	VkDescriptorBufferInfo rank_info = {r->bfs.rank_buf, 0, rank_size};
	VkDescriptorBufferInfo from_info = {r->bfs.from_buf, 0, from_size};
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT * MAX_VIEWS * 4; i++) {
		VkWriteDescriptorSet writes[] = {
			VK_WRITE_DESC_BUFFER(r->descriptors.sets[i], 5, &rank_info, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
			VK_WRITE_DESC_BUFFER(r->descriptors.sets[i], 6, &from_info, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
		};
		vkUpdateDescriptorSets(r->core.device, 2, writes, 0, NULL);
	}

	g_bfs_start_time = r->anim.data.time;

	free(ranks);
	free(from);
}
