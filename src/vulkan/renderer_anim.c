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
	VK_DESTROY_BUFFER(r->core.device, r->edge_vis.buf, r->edge_vis.mem);
}

// ── Generic GPU upload helpers ───────────────────────────────────────────────

void renderer_anim_upload_node_ranks(Renderer *r, const int *ranks, uint32_t node_count, float total_duration)
{
	if (!r || !ranks || node_count == 0)
		return;

	VkDeviceSize size = sizeof(int) * node_count;

	if (r->bfs.rank_buf == VK_NULL_HANDLE || r->bfs.node_count < node_count) {
		VK_DESTROY_BUFFER(r->core.device, r->bfs.rank_buf, r->bfs.rank_mem);
		VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &r->bfs.rank_buf, &r->bfs.rank_mem);
	}

	update_buffer(r->core.device, r->bfs.rank_mem, size, ranks);
	r->bfs.node_count = node_count;

	int max_rank = 0;
	for (uint32_t i = 0; i < node_count; i++)
		if (ranks[i] > max_rank)
			max_rank = ranks[i];
	r->anim.data._pad = (max_rank > 0 && total_duration > 0.0f) ? total_duration / (float)max_rank : 0.0f;

	VkDescriptorBufferInfo info = {r->bfs.rank_buf, 0, size};
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT * MAX_VIEWS * 4; i++) {
		VkWriteDescriptorSet write = VK_WRITE_DESC_BUFFER(r->descriptors.sets[i], 5, &info, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
		vkUpdateDescriptorSets(r->core.device, 1, &write, 0, NULL);
	}
}

void renderer_anim_upload_edge_from(Renderer *r, const uint32_t *from, uint32_t edge_count)
{
	if (!r || !from || edge_count == 0)
		return;

	VkDeviceSize size = sizeof(uint32_t) * edge_count;

	if (r->bfs.from_buf == VK_NULL_HANDLE || r->bfs.edge_count < edge_count) {
		VK_DESTROY_BUFFER(r->core.device, r->bfs.from_buf, r->bfs.from_mem);
		VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &r->bfs.from_buf, &r->bfs.from_mem);
	}

	update_buffer(r->core.device, r->bfs.from_mem, size, from);
	r->bfs.edge_count = edge_count;

	VkDescriptorBufferInfo info = {r->bfs.from_buf, 0, size};
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT * MAX_VIEWS * 4; i++) {
		VkWriteDescriptorSet write = VK_WRITE_DESC_BUFFER(r->descriptors.sets[i], 6, &info, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
		vkUpdateDescriptorSets(r->core.device, 1, &write, 0, NULL);
	}
}

void renderer_anim_upload_edge_floats(Renderer *r, const float *data, uint32_t edge_count, float max_value)
{
	if (!r || !data || edge_count == 0)
		return;

	// Pack [max_value, v0, v1, ...] so shader reads max from index 0
	VkDeviceSize data_size = sizeof(float) * (edge_count + 1);
	float *packed = malloc(data_size);
	if (!packed)
		return;
	packed[0] = max_value;
	memcpy(packed + 1, data, sizeof(float) * edge_count);

	if (r->edge_vis.buf == VK_NULL_HANDLE || r->edge_vis.edge_count < edge_count) {
		VK_DESTROY_BUFFER(r->core.device, r->edge_vis.buf, r->edge_vis.mem);
		VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, data_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &r->edge_vis.buf, &r->edge_vis.mem);
	}

	update_buffer(r->core.device, r->edge_vis.mem, data_size, packed);
	r->edge_vis.edge_count = edge_count;
	r->edge_vis.max_value = max_value;

	VkDescriptorBufferInfo info = {r->edge_vis.buf, 0, data_size};
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT * MAX_VIEWS * 4; i++) {
		VkWriteDescriptorSet write = VK_WRITE_DESC_BUFFER(r->descriptors.sets[i], 7, &info, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
		vkUpdateDescriptorSets(r->core.device, 1, &write, 0, NULL);
	}

	free(packed);
}

void renderer_anim_reset_timer(Renderer *r)
{
	g_bfs_start_time = r->anim.data.time;
}

// ── BFS ──────────────────────────────────────────────────────────────────────

void renderer_anim_compute_bfs(Renderer *r, GraphData *graph)
{
	if (graph->node_count == 0)
		return;

	igraph_integer_t source = 0;
	for (igraph_integer_t i = 1; i < (igraph_integer_t)graph->node_count; i++)
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
	for (igraph_integer_t i = 0; i < (igraph_integer_t)graph->node_count; i++)
		ranks[i] = -5;

	igraph_integer_t order_len = igraph_vector_int_size(&order);
	for (igraph_integer_t i = 0; i < order_len; i++)
		ranks[VECTOR(order)[i]] = i;
	igraph_vector_int_destroy(&order);

	uint32_t *from = malloc(sizeof(uint32_t) * graph->edge_count);
	for (igraph_integer_t i = 0; i < (igraph_integer_t)graph->edge_count; i++)
		from[i] = graph->edges[i].from;

	renderer_anim_upload_node_ranks(r, ranks, graph->node_count, 3.0f);
	renderer_anim_upload_edge_from(r, from, graph->edge_count);
	renderer_anim_reset_timer(r);

	free(ranks);
	free(from);
}

// ── K-Core Tree reveal is computed off the main thread; see
// src/graph/wrappers_kcore_tree.c (compute_kcore_tree_trigger /
// apply_kcore_tree_trigger), which uploads via the generic helpers above.

// ── Reset nodes (reveal all immediately) ─────────────────────────────────────

void renderer_anim_reset_nodes(Renderer *r, GraphData *graph)
{
	if (graph->node_count == 0)
		return;

	int *ranks = malloc(sizeof(int) * graph->node_count);
	for (int i = 0; i < (int)graph->node_count; i++)
		ranks[i] = 0;

	uint32_t *from = malloc(sizeof(uint32_t) * graph->edge_count);
	for (int i = 0; i < (int)graph->edge_count; i++)
		from[i] = graph->edges[i].from;

	renderer_anim_upload_node_ranks(r, ranks, graph->node_count, 0.0f);
	renderer_anim_upload_edge_from(r, from, graph->edge_count);
	renderer_anim_reset_timer(r);

	free(ranks);
	free(from);
}

// ── Reset edges (clear SPLC / flow guard values) ─────────────────────────────

void renderer_anim_reset_edges(Renderer *r)
{
	if (!r)
		return;

	if (r->splc.max_memory != VK_NULL_HANDLE) {
		uint32_t zero = 0u;
		update_buffer(r->core.device, r->splc.max_memory, sizeof(uint32_t), &zero);
	}

	if (r->edge_vis.buf != VK_NULL_HANDLE) {
		float zero = 0.0f;
		update_buffer(r->core.device, r->edge_vis.mem, sizeof(float), &zero);
	}
}

// ── DFS ──────────────────────────────────────────────────────────────────────

void renderer_anim_compute_dfs(Renderer *r, GraphData *graph)
{
	if (graph->node_count == 0)
		return;

	igraph_integer_t source = 0;
	for (igraph_integer_t i = 1; i < (igraph_integer_t)graph->node_count; i++)
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
	for (igraph_integer_t i = 0; i < (igraph_integer_t)graph->node_count; i++)
		ranks[i] = -5;

	igraph_integer_t order_len = igraph_vector_int_size(&order);
	for (igraph_integer_t i = 0; i < order_len; i++)
		ranks[VECTOR(order)[i]] = i;
	igraph_vector_int_destroy(&order);

	uint32_t *from = malloc(sizeof(uint32_t) * graph->edge_count);
	for (igraph_integer_t i = 0; i < (igraph_integer_t)graph->edge_count; i++)
		from[i] = graph->edges[i].from;

	renderer_anim_upload_node_ranks(r, ranks, graph->node_count, 3.0f);
	renderer_anim_upload_edge_from(r, from, graph->edge_count);
	renderer_anim_reset_timer(r);

	free(ranks);
	free(from);
}

// ── Topological sort ─────────────────────────────────────────────────────────

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
	for (igraph_integer_t i = 0; i < (igraph_integer_t)graph->node_count; i++)
		ranks[i] = -5;

	igraph_integer_t order_len = igraph_vector_int_size(&order);
	for (igraph_integer_t i = 0; i < order_len; i++)
		ranks[VECTOR(order)[i]] = i;
	igraph_vector_int_destroy(&order);

	uint32_t *from = malloc(sizeof(uint32_t) * graph->edge_count);
	for (igraph_integer_t i = 0; i < (igraph_integer_t)graph->edge_count; i++)
		from[i] = graph->edges[i].from;

	renderer_anim_upload_node_ranks(r, ranks, graph->node_count, 3.0f);
	renderer_anim_upload_edge_from(r, from, graph->edge_count);
	renderer_anim_reset_timer(r);

	free(ranks);
	free(from);
}
