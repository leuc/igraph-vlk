/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "vulkan/renderer_criticality.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "graph/graph_core.h"
#include "vulkan/buffers.h"
#include "vulkan/utils.h"

#define CRIT_WORKGROUP_SIZE 64

// ============================================================================
// Topology construction
// ============================================================================

// Builds a CSR adjacency in one direction. Mirrors splc_build_topology()'s
// two-pass shape (count degrees, then fill), but is direction-parameterised so
// the same code produces both the forward (IGRAPH_OUT) and reverse (IGRAPH_IN)
// adjacency the gather sweeps need.
static bool crit_build_csr(const igraph_t *g, igraph_integer_t n, igraph_neimode_t mode, CritNode **out_nodes, CritEdge **out_edges, uint32_t *out_edge_count)
{
	CritNode *nodes = calloc((size_t)n, sizeof(CritNode));
	if (!nodes)
		return false;

	igraph_integer_t m = igraph_ecount(g);
	for (igraph_integer_t e = 0; e < m; e++) {
		igraph_integer_t from, to;
		igraph_edge(g, e, &from, &to);
		nodes[mode == IGRAPH_OUT ? from : to].degree++;
	}

	uint32_t offset = 0;
	for (igraph_integer_t i = 0; i < n; i++) {
		nodes[i].edge_offset = offset;
		offset += nodes[i].degree;
	}

	CritEdge *edges = calloc(offset > 0 ? offset : 1, sizeof(CritEdge));
	if (!edges) {
		free(nodes);
		return false;
	}
	uint32_t *cursor = calloc((size_t)n, sizeof(uint32_t));
	if (!cursor) {
		free(edges);
		free(nodes);
		return false;
	}
	for (igraph_integer_t e = 0; e < m; e++) {
		igraph_integer_t from, to;
		igraph_edge(g, e, &from, &to);
		igraph_integer_t node = mode == IGRAPH_OUT ? from : to;
		igraph_integer_t neighbour = mode == IGRAPH_OUT ? to : from;
		uint32_t index = nodes[node].edge_offset + cursor[node]++;
		edges[index] = (CritEdge){(uint32_t)neighbour, (uint32_t)e};
	}
	free(cursor);

	*out_nodes = nodes;
	*out_edges = edges;
	*out_edge_count = offset;
	return true;
}

// Flattens the levels into a single permutation of node ids grouped by level,
// plus per-level (offset, size). Uploading this once is what lets every
// dispatch of the run be recorded into one command buffer.
static bool crit_build_level_permutation(CritComputeContext *ctx, const igraph_vector_int_t *levels, igraph_integer_t n, int num_levels, uint32_t **out_perm)
{
	uint32_t *perm = malloc(sizeof(uint32_t) * (size_t)(n > 0 ? n : 1));
	ctx->level_offsets = calloc((size_t)num_levels, sizeof(uint32_t));
	ctx->level_sizes = calloc((size_t)num_levels, sizeof(uint32_t));
	if (!perm || !ctx->level_offsets || !ctx->level_sizes) {
		free(perm);
		free(ctx->level_offsets);
		free(ctx->level_sizes);
		ctx->level_offsets = NULL;
		ctx->level_sizes = NULL;
		return false;
	}

	for (igraph_integer_t i = 0; i < n; i++) {
		int lvl = (int)VECTOR(*levels)[i];
		if (lvl >= 0 && lvl < num_levels)
			ctx->level_sizes[lvl]++;
	}

	uint32_t running = 0;
	for (int l = 0; l < num_levels; l++) {
		ctx->level_offsets[l] = running;
		running += ctx->level_sizes[l];
	}

	uint32_t *cursor = calloc((size_t)num_levels, sizeof(uint32_t));
	if (!cursor) {
		free(perm);
		return false;
	}
	for (igraph_integer_t i = 0; i < n; i++) {
		int lvl = (int)VECTOR(*levels)[i];
		if (lvl >= 0 && lvl < num_levels)
			perm[ctx->level_offsets[lvl] + cursor[lvl]++] = (uint32_t)i;
	}
	free(cursor);

	*out_perm = perm;
	return true;
}

// ============================================================================
// GPU resources
// ============================================================================

static void crit_destroy_buffer(VkDevice dev, VkBuffer *buf, VkDeviceMemory *mem)
{
	if (*buf != VK_NULL_HANDLE)
		vkDestroyBuffer(dev, *buf, NULL);
	if (*mem != VK_NULL_HANDLE)
		vkFreeMemory(dev, *mem, NULL);
	*buf = VK_NULL_HANDLE;
	*mem = VK_NULL_HANDLE;
}

void renderer_destroy_criticality_buffers(Renderer *r)
{
	CritComputeContext *ctx = &r->crit;
	VkDevice dev = r->core.device;

	crit_destroy_buffer(dev, &ctx->out_nodes_buffer, &ctx->out_nodes_memory);
	crit_destroy_buffer(dev, &ctx->out_edges_buffer, &ctx->out_edges_memory);
	crit_destroy_buffer(dev, &ctx->in_nodes_buffer, &ctx->in_nodes_memory);
	crit_destroy_buffer(dev, &ctx->in_edges_buffer, &ctx->in_edges_memory);
	crit_destroy_buffer(dev, &ctx->level_buffer, &ctx->level_memory);
	crit_destroy_buffer(dev, &ctx->lnw_buffer, &ctx->lnw_memory);
	crit_destroy_buffer(dev, &ctx->lnx_buffer, &ctx->lnx_memory);
	crit_destroy_buffer(dev, &ctx->height_buffer, &ctx->height_memory);
	crit_destroy_buffer(dev, &ctx->depth_buffer, &ctx->depth_memory);
	crit_destroy_buffer(dev, &ctx->display_edges_buffer, &ctx->display_edges_memory);
	crit_destroy_buffer(dev, &ctx->display_max_buffer, &ctx->display_max_memory);

	free(ctx->level_offsets);
	free(ctx->level_sizes);
	ctx->level_offsets = NULL;
	ctx->level_sizes = NULL;
	ctx->num_levels = 0;
	ctx->node_count = 0;
	ctx->graph_edge_count = 0;
	ctx->active = false;
	ctx->readback_pending = false;
}

static void crit_write_descriptors(Renderer *r)
{
	CritComputeContext *ctx = &r->crit;
	VkDescriptorBufferInfo infos[] = {
		{ctx->out_nodes_buffer, 0, VK_WHOLE_SIZE}, {ctx->out_edges_buffer, 0, VK_WHOLE_SIZE}, {ctx->in_nodes_buffer, 0, VK_WHOLE_SIZE}, {ctx->in_edges_buffer, 0, VK_WHOLE_SIZE}, {ctx->level_buffer, 0, VK_WHOLE_SIZE}, {ctx->lnw_buffer, 0, VK_WHOLE_SIZE}, {ctx->lnx_buffer, 0, VK_WHOLE_SIZE}, {ctx->height_buffer, 0, VK_WHOLE_SIZE}, {ctx->depth_buffer, 0, VK_WHOLE_SIZE}, {ctx->display_edges_buffer, 0, VK_WHOLE_SIZE}, {ctx->display_max_buffer, 0, VK_WHOLE_SIZE},
	};
	VkWriteDescriptorSet writes[11];
	for (uint32_t i = 0; i < 11; i++)
		writes[i] = VK_WRITE_DESC_BUFFER(r->descriptors.crit_set, i, &infos[i], VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
	vkUpdateDescriptorSets(r->core.device, 11, writes, 0, NULL);
}

static void crit_write_graphics_descriptors(Renderer *r)
{
	CritComputeContext *ctx = &r->crit;
	if (!r->descriptors.sets)
		return;
	VkDescriptorBufferInfo edge_info = {ctx->display_edges_buffer, 0, VK_WHOLE_SIZE};
	VkDescriptorBufferInfo max_info = {ctx->display_max_buffer, 0, VK_WHOLE_SIZE};
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT * MAX_VIEWS; i++) {
		VkWriteDescriptorSet writes[] = {
			VK_WRITE_DESC_BUFFER(r->descriptors.sets[i], 2, &edge_info, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
			VK_WRITE_DESC_BUFFER(r->descriptors.sets[i], 3, &max_info, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
		};
		vkUpdateDescriptorSets(r->core.device, 2, writes, 0, NULL);
	}
}

bool renderer_init_criticality_buffers(Renderer *r, GraphData *graph, const igraph_vector_int_t *levels, int num_levels, uint32_t weight_mode)
{
	CritComputeContext *ctx = &r->crit;
	renderer_destroy_criticality_buffers(r);

	igraph_integer_t n = igraph_vcount(&graph->g);
	if (n == 0 || num_levels <= 0)
		return false;

	ctx->node_count = (uint32_t)n;
	ctx->num_levels = num_levels;
	ctx->weight_mode = weight_mode;
	ctx->graph_edge_count = (uint32_t)igraph_ecount(&graph->g);

	CritNode *out_nodes = NULL, *in_nodes = NULL;
	CritEdge *out_edges = NULL, *in_edges = NULL;
	uint32_t out_edge_count = 0, in_edge_count = 0;
	uint32_t *perm = NULL;

	if (!crit_build_csr(&graph->g, n, IGRAPH_OUT, &out_nodes, &out_edges, &out_edge_count))
		return false;
	if (!crit_build_csr(&graph->g, n, IGRAPH_IN, &in_nodes, &in_edges, &in_edge_count)) {
		free(out_nodes);
		free(out_edges);
		return false;
	}
	if (!crit_build_level_permutation(ctx, levels, n, num_levels, &perm)) {
		free(out_nodes);
		free(out_edges);
		free(in_nodes);
		free(in_edges);
		return false;
	}

	VkDevice dev = r->core.device;
	VkPhysicalDevice phys = r->core.physicalDevice;
	const VkBufferUsageFlags usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	VkDeviceSize node_size = sizeof(CritNode) * (size_t)n;
	VkDeviceSize out_edge_size = sizeof(CritEdge) * (out_edge_count > 0 ? out_edge_count : 1);
	VkDeviceSize in_edge_size = sizeof(CritEdge) * (in_edge_count > 0 ? in_edge_count : 1);
	VkDeviceSize level_size = sizeof(uint32_t) * (size_t)n;
	VkDeviceSize value_size = sizeof(float) * (size_t)n;
	VkDeviceSize display_edge_size = sizeof(SPLCEdge) * (ctx->graph_edge_count > 0 ? ctx->graph_edge_count : 1);

	VK_CREATE_HOST_BUFFER(dev, phys, node_size, usage, &ctx->out_nodes_buffer, &ctx->out_nodes_memory);
	VK_CREATE_HOST_BUFFER(dev, phys, out_edge_size, usage, &ctx->out_edges_buffer, &ctx->out_edges_memory);
	VK_CREATE_HOST_BUFFER(dev, phys, node_size, usage, &ctx->in_nodes_buffer, &ctx->in_nodes_memory);
	VK_CREATE_HOST_BUFFER(dev, phys, in_edge_size, usage, &ctx->in_edges_buffer, &ctx->in_edges_memory);
	VK_CREATE_HOST_BUFFER(dev, phys, level_size, usage, &ctx->level_buffer, &ctx->level_memory);
	VK_CREATE_HOST_BUFFER(dev, phys, value_size, usage, &ctx->lnw_buffer, &ctx->lnw_memory);
	VK_CREATE_HOST_BUFFER(dev, phys, value_size, usage, &ctx->lnx_buffer, &ctx->lnx_memory);
	VK_CREATE_HOST_BUFFER(dev, phys, value_size, usage, &ctx->height_buffer, &ctx->height_memory);
	VK_CREATE_HOST_BUFFER(dev, phys, value_size, usage, &ctx->depth_buffer, &ctx->depth_memory);
	VK_CREATE_HOST_BUFFER(dev, phys, display_edge_size, usage, &ctx->display_edges_buffer, &ctx->display_edges_memory);
	VK_CREATE_HOST_BUFFER(dev, phys, sizeof(uint32_t), usage, &ctx->display_max_buffer, &ctx->display_max_memory);

	update_buffer(dev, ctx->out_nodes_memory, node_size, out_nodes);
	update_buffer(dev, ctx->out_edges_memory, sizeof(CritEdge) * out_edge_count, out_edges);
	update_buffer(dev, ctx->in_nodes_memory, node_size, in_nodes);
	update_buffer(dev, ctx->in_edges_memory, sizeof(CritEdge) * in_edge_count, in_edges);
	update_buffer(dev, ctx->level_memory, level_size, perm);

	float *zeros = calloc((size_t)n, sizeof(float));
	if (zeros) {
		update_buffer(dev, ctx->lnw_memory, value_size, zeros);
		update_buffer(dev, ctx->lnx_memory, value_size, zeros);
		update_buffer(dev, ctx->height_memory, value_size, zeros);
		update_buffer(dev, ctx->depth_memory, value_size, zeros);
		free(zeros);
	}
	SPLCEdge *display_edges = calloc(ctx->graph_edge_count > 0 ? ctx->graph_edge_count : 1, sizeof(SPLCEdge));
	if (!display_edges) {
		free(out_nodes);
		free(out_edges);
		free(in_nodes);
		free(in_edges);
		free(perm);
		renderer_destroy_criticality_buffers(r);
		return false;
	}
	for (uint32_t e = 0; e < ctx->graph_edge_count; e++)
		display_edges[e].target_node = graph->edges[e].to;
	update_buffer(dev, ctx->display_edges_memory, display_edge_size, display_edges);
	uint32_t zero_max = 0;
	update_buffer(dev, ctx->display_max_memory, sizeof(zero_max), &zero_max);
	free(display_edges);

	free(out_nodes);
	free(out_edges);
	free(in_nodes);
	free(in_edges);
	free(perm);

	crit_write_descriptors(r);
	crit_write_graphics_descriptors(r);

	return true;
}

bool renderer_start_main_path_weighting(Renderer *r)
{
	CritComputeContext *ctx = &r->crit;
	if (ctx->num_levels <= 0 || ctx->display_edges_buffer == VK_NULL_HANDLE)
		return false;
	ctx->active = true;
	ctx->readback_pending = false;
	ctx->stage = CRIT_STAGE_LNW;
	ctx->current_level = 0;
	ctx->level_interval = 5.0 / (double)ctx->num_levels;
	if (ctx->level_interval < 0.016)
		ctx->level_interval = 0.016;
	ctx->last_level_time = r->anim.data.time;
	printf("[MainPath] start: method=%s levels=%d tick=%.3fs\n", ctx->weight_mode == CRIT_WEIGHT_UNIT ? "SPLC" : (ctx->weight_mode == CRIT_WEIGHT_SPC ? "SPC" : "SPE"), ctx->num_levels, ctx->level_interval);
	return true;
}

void renderer_dispatch_main_path_weight_level(Renderer *r, VkCommandBuffer cmd)
{
	CritComputeContext *ctx = &r->crit;
	if (!ctx->active)
		return;

	if ((ctx->stage == CRIT_STAGE_LNW && ctx->current_level >= ctx->num_levels) || (ctx->stage == CRIT_STAGE_LNX && ctx->current_level < 0)) {
		if (ctx->stage == CRIT_STAGE_LNW && ctx->weight_mode != CRIT_WEIGHT_UNIT) {
			printf("[MainPath] phase complete: forward; starting reverse projection\n");
			ctx->stage = CRIT_STAGE_LNX;
			ctx->current_level = ctx->num_levels - 1;
			ctx->last_level_time = r->anim.data.time;
			return;
		}
		printf("[MainPath] compute complete; scheduling readback\n");
		ctx->readback_pending = true;
		return;
	}

	int level = ctx->current_level;
	uint32_t count = ctx->level_sizes[level];
	printf("[MainPath] tick: method=%s phase=%s level=%d/%d nodes=%u\n", ctx->weight_mode == CRIT_WEIGHT_UNIT ? "SPLC" : (ctx->weight_mode == CRIT_WEIGHT_SPC ? "SPC" : "SPE"), ctx->stage == CRIT_STAGE_LNW ? "forward" : "reverse", level, ctx->num_levels - 1, count);
	if (count > 0) {
		VkMemoryBarrier compute_barrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_SHADER_READ_BIT};
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &compute_barrier, 0, NULL, 0, NULL);
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, r->pipelines.compute_criticality);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, ctx->pipeline_layout, 0, 1, &r->descriptors.crit_set, 0, NULL);
		CritPushConstants pc = {.level_offset = ctx->level_offsets[level], .num_nodes_in_level = count, .stage = ctx->stage, .weight_mode = ctx->weight_mode};
		vkCmdPushConstants(cmd, ctx->pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
		vkCmdDispatch(cmd, (count + CRIT_WORKGROUP_SIZE - 1) / CRIT_WORKGROUP_SIZE, 1, 1);
		VkMemoryBarrier vertex_barrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_SHADER_READ_BIT};
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 1, &vertex_barrier, 0, NULL, 0, NULL);
	}
	ctx->current_level += ctx->stage == CRIT_STAGE_LNW ? 1 : -1;
	ctx->last_level_time = r->anim.data.time;
}

static float *crit_copy_values(Renderer *r, VkDeviceMemory mem, uint32_t n)
{
	VkDeviceSize size = sizeof(float) * (size_t)n;
	void *mapped = NULL;
	if (vkMapMemory(r->core.device, mem, 0, size, 0, &mapped) != VK_SUCCESS)
		return NULL;
	float *values = malloc(size);
	if (values)
		memcpy(values, mapped, size);
	vkUnmapMemory(r->core.device, mem);
	return values;
}

void renderer_readback_main_path_weights(Renderer *r, GraphData *graph)
{
	CritComputeContext *ctx = &r->crit;
	if (!graph || ctx->node_count == 0 || ctx->lnw_memory == VK_NULL_HANDLE || ctx->lnx_memory == VK_NULL_HANDLE)
		return;

	float *lnw = crit_copy_values(r, ctx->lnw_memory, ctx->node_count);
	float *lnx = crit_copy_values(r, ctx->lnx_memory, ctx->node_count);
	igraph_integer_t m = igraph_ecount(&graph->g);
	igraph_vector_t weights;
	if (!lnw || !lnx || igraph_vector_init(&weights, m) != IGRAPH_SUCCESS) {
		free(lnw);
		free(lnx);
		return;
	}

	float max_weight = 0.0f;
	double total = 0.0;
	for (igraph_integer_t e = 0; e < m; e++) {
		igraph_integer_t from, to;
		igraph_edge(&graph->g, e, &from, &to);
		float log_weight = lnw[from] + lnx[to];
		float weight;
		if (ctx->weight_mode == CRIT_WEIGHT_UNIT) {
			weight = lnw[from] > 80.0f ? INFINITY : expf(lnw[from]);
		} else if (ctx->weight_mode == CRIT_WEIGHT_SPC) {
			weight = log_weight > 80.0f ? INFINITY : expf(log_weight);
		} else {
			weight = log_weight;
		}
		VECTOR(weights)[e] = weight;
		graph->edges[e].weight = weight;
		if (isfinite(weight) && weight > max_weight)
			max_weight = weight;
		total += weight;
	}

	const char *attr_name = ctx->weight_mode == CRIT_WEIGHT_UNIT ? "main-path-weight-splc" : (ctx->weight_mode == CRIT_WEIGHT_SPC ? "main-path-weight-spc" : "main-path-weight-spe");
	graph_cache_store_edge_attr(&graph->g, attr_name, &weights);
	printf("Main path %s readback: %lld edges, max weight: %.2f, total: %.2f\n", ctx->weight_mode == CRIT_WEIGHT_UNIT ? "SPLC" : (ctx->weight_mode == CRIT_WEIGHT_SPC ? "SPC" : "SPE"), (long long)m, max_weight, total);
	igraph_vector_destroy(&weights);
	free(lnw);
	free(lnx);
}
