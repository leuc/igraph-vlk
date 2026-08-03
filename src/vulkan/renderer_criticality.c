/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "vulkan/renderer_criticality.h"

#include <stdlib.h>
#include <string.h>

#include "graph/graph_core.h"
#include "vulkan/buffers.h"
#include "vulkan/utils.h"

#define CRIT_WORKGROUP_SIZE 64

// The shared VK_CHECK exits the process on failure, which is the right call
// for renderer setup but not for an optional analysis command the user can
// simply run again — a failed submit here aborts the run, nothing more.
#define CRIT_VK_TRY(res, msg) \
	do { \
		VkResult _r = (res); \
		if (_r != VK_SUCCESS) { \
			fprintf(stderr, "[criticality] %s (VkResult %d)\n", msg, _r); \
			return false; \
		} \
	} while (0)

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

	igraph_vector_int_t neis;
	if (igraph_vector_int_init(&neis, 0) != IGRAPH_SUCCESS) {
		free(nodes);
		return false;
	}

	uint32_t offset = 0;
	for (igraph_integer_t i = 0; i < n; i++) {
		igraph_neighbors(g, &neis, i, mode, IGRAPH_LOOPS, IGRAPH_NO_MULTIPLE);
		nodes[i].edge_offset = offset;
		nodes[i].degree = (uint32_t)igraph_vector_int_size(&neis);
		offset += nodes[i].degree;
	}

	CritEdge *edges = calloc(offset > 0 ? offset : 1, sizeof(CritEdge));
	if (!edges) {
		igraph_vector_int_destroy(&neis);
		free(nodes);
		return false;
	}

	uint32_t e_idx = 0;
	for (igraph_integer_t i = 0; i < n; i++) {
		igraph_neighbors(g, &neis, i, mode, IGRAPH_LOOPS, IGRAPH_NO_MULTIPLE);
		for (igraph_integer_t j = 0; j < igraph_vector_int_size(&neis); j++)
			edges[e_idx++].node = (uint32_t)VECTOR(neis)[j];
	}
	igraph_vector_int_destroy(&neis);

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

	if (ctx->submitted && ctx->fence != VK_NULL_HANDLE)
		vkWaitForFences(dev, 1, &ctx->fence, VK_TRUE, UINT64_MAX);
	ctx->submitted = false;

	crit_destroy_buffer(dev, &ctx->out_nodes_buffer, &ctx->out_nodes_memory);
	crit_destroy_buffer(dev, &ctx->out_edges_buffer, &ctx->out_edges_memory);
	crit_destroy_buffer(dev, &ctx->in_nodes_buffer, &ctx->in_nodes_memory);
	crit_destroy_buffer(dev, &ctx->in_edges_buffer, &ctx->in_edges_memory);
	crit_destroy_buffer(dev, &ctx->level_buffer, &ctx->level_memory);
	crit_destroy_buffer(dev, &ctx->lnw_buffer, &ctx->lnw_memory);
	crit_destroy_buffer(dev, &ctx->lnx_buffer, &ctx->lnx_memory);
	crit_destroy_buffer(dev, &ctx->height_buffer, &ctx->height_memory);
	crit_destroy_buffer(dev, &ctx->depth_buffer, &ctx->depth_memory);

	free(ctx->level_offsets);
	free(ctx->level_sizes);
	ctx->level_offsets = NULL;
	ctx->level_sizes = NULL;
	ctx->num_levels = 0;
	ctx->node_count = 0;
}

static void crit_write_descriptors(Renderer *r)
{
	CritComputeContext *ctx = &r->crit;
	VkDescriptorBufferInfo infos[] = {
		{ctx->out_nodes_buffer, 0, VK_WHOLE_SIZE}, {ctx->out_edges_buffer, 0, VK_WHOLE_SIZE}, {ctx->in_nodes_buffer, 0, VK_WHOLE_SIZE}, {ctx->in_edges_buffer, 0, VK_WHOLE_SIZE}, {ctx->level_buffer, 0, VK_WHOLE_SIZE}, {ctx->lnw_buffer, 0, VK_WHOLE_SIZE}, {ctx->lnx_buffer, 0, VK_WHOLE_SIZE}, {ctx->height_buffer, 0, VK_WHOLE_SIZE}, {ctx->depth_buffer, 0, VK_WHOLE_SIZE},
	};
	VkWriteDescriptorSet writes[9];
	for (uint32_t i = 0; i < 9; i++)
		writes[i] = VK_WRITE_DESC_BUFFER(r->descriptors.crit_set, i, &infos[i], VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
	vkUpdateDescriptorSets(r->core.device, 9, writes, 0, NULL);
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

	VK_CREATE_HOST_BUFFER(dev, phys, node_size, usage, &ctx->out_nodes_buffer, &ctx->out_nodes_memory);
	VK_CREATE_HOST_BUFFER(dev, phys, out_edge_size, usage, &ctx->out_edges_buffer, &ctx->out_edges_memory);
	VK_CREATE_HOST_BUFFER(dev, phys, node_size, usage, &ctx->in_nodes_buffer, &ctx->in_nodes_memory);
	VK_CREATE_HOST_BUFFER(dev, phys, in_edge_size, usage, &ctx->in_edges_buffer, &ctx->in_edges_memory);
	VK_CREATE_HOST_BUFFER(dev, phys, level_size, usage, &ctx->level_buffer, &ctx->level_memory);
	VK_CREATE_HOST_BUFFER(dev, phys, value_size, usage, &ctx->lnw_buffer, &ctx->lnw_memory);
	VK_CREATE_HOST_BUFFER(dev, phys, value_size, usage, &ctx->lnx_buffer, &ctx->lnx_memory);
	VK_CREATE_HOST_BUFFER(dev, phys, value_size, usage, &ctx->height_buffer, &ctx->height_memory);
	VK_CREATE_HOST_BUFFER(dev, phys, value_size, usage, &ctx->depth_buffer, &ctx->depth_memory);

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

	free(out_nodes);
	free(out_edges);
	free(in_nodes);
	free(in_edges);
	free(perm);

	crit_write_descriptors(r);

	if (ctx->cmd_pool == VK_NULL_HANDLE) {
		VkCommandPoolCreateInfo poolInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = 0, .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT};
		VK_CHECK(vkCreateCommandPool(dev, &poolInfo, NULL, &ctx->cmd_pool), "Failed to create criticality command pool");

		VkCommandBufferAllocateInfo allocInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = ctx->cmd_pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
		VK_CHECK(vkAllocateCommandBuffers(dev, &allocInfo, &ctx->cmd_buf), "Failed to allocate criticality command buffer");

		VkFenceCreateInfo fenceInfo = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
		VK_CHECK(vkCreateFence(dev, &fenceInfo, NULL, &ctx->fence), "Failed to create criticality fence");
	}

	return true;
}

// ============================================================================
// Dispatch
// ============================================================================

// One dispatch per level, ascending for the forward sweeps and descending for
// the backward ones. Because level(v) > level(u) holds for every edge (u, v),
// this ordering means each sweep only ever reads values written by an earlier
// dispatch, so a plain memory barrier between dispatches is sufficient.
static void crit_record_stage(Renderer *r, uint32_t stage, bool ascending)
{
	CritComputeContext *ctx = &r->crit;
	VkMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_SHADER_READ_BIT};

	for (int i = 0; i < ctx->num_levels; i++) {
		int l = ascending ? i : ctx->num_levels - 1 - i;
		if (ctx->level_sizes[l] == 0)
			continue;

		CritPushConstants pc = {.level_offset = ctx->level_offsets[l], .num_nodes_in_level = ctx->level_sizes[l], .stage = stage, .weight_mode = ctx->weight_mode};
		vkCmdPushConstants(ctx->cmd_buf, ctx->pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
		vkCmdDispatch(ctx->cmd_buf, (pc.num_nodes_in_level + CRIT_WORKGROUP_SIZE - 1) / CRIT_WORKGROUP_SIZE, 1, 1);
		vkCmdPipelineBarrier(ctx->cmd_buf, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, NULL, 0, NULL);
	}
}

bool renderer_dispatch_criticality(Renderer *r)
{
	CritComputeContext *ctx = &r->crit;
	if (ctx->cmd_buf == VK_NULL_HANDLE || ctx->num_levels <= 0)
		return false;

	CRIT_VK_TRY(vkResetFences(r->core.device, 1, &ctx->fence), "failed to reset fence");
	CRIT_VK_TRY(vkResetCommandBuffer(ctx->cmd_buf, 0), "failed to reset command buffer");
	CRIT_VK_TRY(vkBeginCommandBuffer(ctx->cmd_buf, &VK_CMD_BEGIN_INFO_ONETIME), "failed to begin command buffer");

	vkCmdBindPipeline(ctx->cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, r->pipelines.compute_criticality);
	vkCmdBindDescriptorSets(ctx->cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, ctx->pipeline_layout, 0, 1, &r->descriptors.crit_set, 0, NULL);

	// Heights and depths read lnW/lnX, so the path counts must run first.
	crit_record_stage(r, CRIT_STAGE_LNW, true);
	crit_record_stage(r, CRIT_STAGE_LNX, false);
	crit_record_stage(r, CRIT_STAGE_HEIGHT, true);
	crit_record_stage(r, CRIT_STAGE_DEPTH, false);

	CRIT_VK_TRY(vkEndCommandBuffer(ctx->cmd_buf), "failed to end command buffer");

	VkSubmitInfo submitInfo = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &ctx->cmd_buf};
	CRIT_VK_TRY(vkQueueSubmit(r->core.graphicsQueue, 1, &submitInfo, ctx->fence), "failed to submit work");

	ctx->submitted = true;
	return true;
}

bool renderer_criticality_ready(Renderer *r)
{
	CritComputeContext *ctx = &r->crit;
	if (!ctx->submitted)
		return false;
	return vkGetFenceStatus(r->core.device, ctx->fence) == VK_SUCCESS;
}

// ============================================================================
// Readback
// ============================================================================

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

bool renderer_readback_criticality(Renderer *r, float **out_height, float **out_depth, float **out_lnw, float **out_lnx)
{
	CritComputeContext *ctx = &r->crit;
	if (ctx->node_count == 0 || ctx->height_memory == VK_NULL_HANDLE)
		return false;

	ctx->submitted = false;

	if (out_height && !(*out_height = crit_copy_values(r, ctx->height_memory, ctx->node_count)))
		return false;
	if (out_depth && !(*out_depth = crit_copy_values(r, ctx->depth_memory, ctx->node_count)))
		return false;
	if (out_lnw && !(*out_lnw = crit_copy_values(r, ctx->lnw_memory, ctx->node_count)))
		return false;
	if (out_lnx && !(*out_lnx = crit_copy_values(r, ctx->lnx_memory, ctx->node_count)))
		return false;

	return true;
}
