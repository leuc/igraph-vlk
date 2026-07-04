/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "vulkan/renderer_compute.h"

#include <stdlib.h>
#include <string.h>

#include "graph/wrappers_splc.h"
#include "vulkan/buffers.h"
#include "vulkan/renderer_init_splc_buffers.h"
#include "vulkan/utils.h"

VkResult renderer_dispatch_edge_routing(Renderer *r, GraphData *graph, CompEdge *edgeResults)
{
	if (r->currentRoutingMode == ROUTING_MODE_STRAIGHT) {
		return VK_SUCCESS;
	}

	ComputeContext *ctx = &r->computeCtx;

	// Wait for previous compute job to complete
	if (ctx->initialized && ctx->fence != VK_NULL_HANDLE) {
		VK_CHECK(vkWaitForFences(r->core.device, 1, &ctx->fence, VK_TRUE, UINT64_MAX), "Failed to wait for compute fences");
		VK_CHECK(vkResetFences(r->core.device, 1, &ctx->fence), "Failed to reset compute fences");
	}

	// Prepare compute shader input data
	CompNode *cNodes = malloc(sizeof(CompNode) * graph->node_count);
	CompEdge *cEdges = malloc(sizeof(CompEdge) * graph->edge_count);

	for (uint32_t i = 0; i < graph->node_count; i++) {
		glm_vec3_scale(graph->nodes[i].position, r->layoutScale, cNodes[i].position);
		cNodes[i].pad1 = 0;
		memcpy(cNodes[i].color, graph->nodes[i].color, sizeof(vec3));
		cNodes[i].size = graph->nodes[i].size;
		cNodes[i].degree = graph->nodes[i].degree;
		cNodes[i].pad2 = cNodes[i].pad3 = cNodes[i].pad4 = 0;
	}
	for (uint32_t i = 0; i < graph->edge_count; i++) {
		cEdges[i].sourceId = graph->edges[i].from;
		cEdges[i].targetId = graph->edges[i].to;
		cEdges[i].elevationLevel = 0;
		cEdges[i].pathLength = 0;
	}

	// Allocate persistent resources on first use
	if (!ctx->initialized) {
		VkCommandPoolCreateInfo commandPoolInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = 0, .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT};
		VK_CHECK(vkCreateCommandPool(r->core.device, &commandPoolInfo, NULL, &ctx->cmdPool), "Failed to create compute command pool");

		VkCommandBufferAllocateInfo commandBufferAllocInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = ctx->cmdPool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
		VK_CHECK(vkAllocateCommandBuffers(r->core.device, &commandBufferAllocInfo, &ctx->cmdBuf), "Failed to allocate compute command buffer");
		VK_CHECK(vkCreateFence(r->core.device, &VK_SIGNALED_FENCE_INFO, NULL, &ctx->fence), "Failed to create compute fence");
		VK_CHECK(vkResetFences(r->core.device, 1, &ctx->fence), "Failed to reset compute fence after creation");

		VkDescriptorPoolSize descriptorPoolSizes = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3};
		VkDescriptorPoolCreateInfo descriptorPoolInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &descriptorPoolSizes};
		VK_CHECK(vkCreateDescriptorPool(r->core.device, &descriptorPoolInfo, NULL, &ctx->pool), "Failed to create compute descriptor pool");

		VkDescriptorSetAllocateInfo descriptorSetAllocInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = ctx->pool, .descriptorSetCount = 1, .pSetLayouts = &r->descriptors.compute_layout};
		VK_CHECK(vkAllocateDescriptorSets(r->core.device, &descriptorSetAllocInfo, &ctx->descSet), "Failed to allocate compute descriptor set");

		ctx->initialized = VK_TRUE;
	}

	// Create or resize storage buffers if needed
	VkDeviceSize nodeSize = sizeof(CompNode) * graph->node_count;
	VkDeviceSize edgeSize = sizeof(CompEdge) * graph->edge_count;

	if (ctx->nodeBuf != VK_NULL_HANDLE) {
		// Check if buffers are large enough
		VkMemoryRequirements memReqs;
		vkGetBufferMemoryRequirements(r->core.device, ctx->nodeBuf, &memReqs);
		if (memReqs.size < nodeSize) {
			VK_DESTROY_BUFFER(r->core.device, ctx->nodeBuf, ctx->nodeMem);
		}
	}
	if (ctx->nodeBuf == VK_NULL_HANDLE) {
		VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, nodeSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &ctx->nodeBuf, &ctx->nodeMem);
	}

	if (ctx->edgeBuf != VK_NULL_HANDLE) {
		VkMemoryRequirements memReqs;
		vkGetBufferMemoryRequirements(r->core.device, ctx->edgeBuf, &memReqs);
		if (memReqs.size < edgeSize) {
			VK_DESTROY_BUFFER(r->core.device, ctx->edgeBuf, ctx->edgeMem);
		}
	}
	if (ctx->edgeBuf == VK_NULL_HANDLE) {
		VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, edgeSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &ctx->edgeBuf, &ctx->edgeMem);
	}

	if (ctx->hubBuf == VK_NULL_HANDLE) {
		VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, sizeof(CompHub), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &ctx->hubBuf, &ctx->hubMem);
	}

	// Upload node and edge data to GPU
	update_buffer(r->core.device, ctx->nodeMem, nodeSize, cNodes);
	update_buffer(r->core.device, ctx->edgeMem, edgeSize, cEdges);

	// Update descriptor set with storage buffers
	VkDescriptorBufferInfo nodeBufferInfo = {ctx->nodeBuf, 0, VK_WHOLE_SIZE}, edgeBufferInfo = {ctx->edgeBuf, 0, VK_WHOLE_SIZE}, hubBufferInfo = {ctx->hubBuf, 0, VK_WHOLE_SIZE};
	VkWriteDescriptorSet writes[3] = {VK_WRITE_DESC_BUFFER(ctx->descSet, 0, &nodeBufferInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER), VK_WRITE_DESC_BUFFER(ctx->descSet, 1, &edgeBufferInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER), VK_WRITE_DESC_BUFFER(ctx->descSet, 2, &hubBufferInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)};
	vkUpdateDescriptorSets(r->core.device, 3, writes, 0, NULL);

	// Record command buffer
	VK_CHECK(vkResetCommandBuffer(ctx->cmdBuf, 0), "Failed to reset compute command buffer");
	VK_CHECK(vkBeginCommandBuffer(ctx->cmdBuf, &VK_CMD_BEGIN_INFO_ONETIME), "Failed to begin compute command buffer");

	vkCmdBindPipeline(ctx->cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, r->pipelines.compute_spherical);
	vkCmdBindDescriptorSets(ctx->cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, r->computePipelineLayout, 0, 1, &ctx->descSet, 0, NULL);

	struct
	{
		int maxE;
		float baseR;
		int numHubs;
	} pcVals = {graph->edge_count, 5.0f * r->layoutScale, 0};
	vkCmdPushConstants(ctx->cmdBuf, r->computePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pcVals), &pcVals);

	vkCmdDispatch(ctx->cmdBuf, (graph->edge_count + 255) / 256, 1, 1);
	VK_CHECK(vkEndCommandBuffer(ctx->cmdBuf), "Failed to end compute command buffer");

	// Submit with fence (no vkQueueWaitIdle)
	VkSubmitInfo submitInfo = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &ctx->cmdBuf};
	VkResult result = vkQueueSubmit(r->core.graphicsQueue, 1, &submitInfo, ctx->fence);
	if (result != VK_SUCCESS) {
		free(cNodes);
		free(cEdges);
		return result;
	}

	// Wait for completion and read back
	VK_CHECK(vkWaitForFences(r->core.device, 1, &ctx->fence, VK_TRUE, UINT64_MAX), "Failed to wait for compute fence on readback");

	void *mapped;
	VK_CHECK(vkMapMemory(r->core.device, ctx->edgeMem, 0, edgeSize, 0, &mapped), "Failed to map compute edge buffer memory");
	memcpy(edgeResults, mapped, sizeof(CompEdge) * graph->edge_count);
	vkUnmapMemory(r->core.device, ctx->edgeMem);

	free(cNodes);
	free(cEdges);

	return VK_SUCCESS;
}

// ============================================================================
// SPLC buffer helpers
// ============================================================================

static void splc_destroy_old_buffers(Renderer *r)
{
	r->splc.nodes_buffer = VK_NULL_HANDLE;
	r->splc.nodes_memory = VK_NULL_HANDLE;
	r->splc.edges_buffer = VK_NULL_HANDLE;
	r->splc.edges_memory = VK_NULL_HANDLE;
	r->splc.traffic_buffer = VK_NULL_HANDLE;
	r->splc.traffic_memory = VK_NULL_HANDLE;
	r->splc.level_buffer = VK_NULL_HANDLE;
	r->splc.level_memory = VK_NULL_HANDLE;
	r->splc.max_buffer = VK_NULL_HANDLE;
	r->splc.max_memory = VK_NULL_HANDLE;

	if (r->splc.level_groups) {
		for (int i = 0; i < r->splc.num_levels; i++) {
			if (r->splc.level_groups[i])
				igraph_vector_int_destroy(r->splc.level_groups[i]);
			free(r->splc.level_groups[i]);
		}
		free(r->splc.level_groups);
		r->splc.level_groups = NULL;
	}

	r->splc.active = false;
	r->splc.current_level = 0;
	r->splc.last_level_time = 0.0;
}

static void splc_destroy_buffer(VkDevice dev, VkBuffer buf, VkDeviceMemory mem)
{
	if (buf != VK_NULL_HANDLE)
		vkDestroyBuffer(dev, buf, NULL);
	if (mem != VK_NULL_HANDLE)
		vkFreeMemory(dev, mem, NULL);
}

static void splc_create_fallback_buffers(Renderer *r, uint32_t m)
{
	VkDeviceSize edge_buf_size = sizeof(SPLCEdge) * m;
	VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, edge_buf_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, &r->splc.edges_buffer, &r->splc.edges_memory);
	VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, &r->splc.max_buffer, &r->splc.max_memory);
	SPLCEdge *zero_edges = calloc(m, sizeof(SPLCEdge));
	update_buffer(r->core.device, r->splc.edges_memory, edge_buf_size, zero_edges);
	free(zero_edges);
	uint32_t zero_max = 0;
	update_buffer(r->core.device, r->splc.max_memory, sizeof(uint32_t), &zero_max);
}

static void splc_write_graphics_descriptors(Renderer *r)
{
	if (r->descriptors.sets == NULL)
		return;
	VkDescriptorBufferInfo edgeWeightInfo = {r->splc.edges_buffer, 0, VK_WHOLE_SIZE};
	VkDescriptorBufferInfo maxWeightInfo = {r->splc.max_buffer, 0, VK_WHOLE_SIZE};
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT * MAX_VIEWS; i++) {
		VkWriteDescriptorSet descWrites[] = {
			VK_WRITE_DESC_BUFFER(r->descriptors.sets[i], 2, &edgeWeightInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
			VK_WRITE_DESC_BUFFER(r->descriptors.sets[i], 3, &maxWeightInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
		};
		vkUpdateDescriptorSets(r->core.device, 2, descWrites, 0, NULL);
	}
}

static void splc_build_level_groups(Renderer *r, igraph_vector_int_t *levels, igraph_integer_t n)
{
	r->splc.level_groups = calloc(r->splc.num_levels, sizeof(igraph_vector_int_t *));
	for (int l = 0; l < r->splc.num_levels; l++) {
		r->splc.level_groups[l] = malloc(sizeof(igraph_vector_int_t));
		igraph_vector_int_init(r->splc.level_groups[l], 0);
	}
	for (igraph_integer_t i = 0; i < n; i++) {
		int lvl = (int)VECTOR(*levels)[i];
		if (lvl >= 0 && lvl < r->splc.num_levels)
			igraph_vector_int_push_back(r->splc.level_groups[lvl], i);
	}
}

static void splc_build_topology(const igraph_t *g, igraph_integer_t n, SPLCNode **out_nodes, SPLCEdge **out_edges, float **out_traffic, uint32_t *out_total_edges)
{
	SPLCNode *splc_nodes = calloc(n, sizeof(SPLCNode));
	igraph_vector_int_t out_neis;
	igraph_vector_int_init(&out_neis, 0);
	uint32_t edge_offset = 0;
	for (igraph_integer_t i = 0; i < n; i++) {
		splc_nodes[i].edge_offset = edge_offset;
		igraph_neighbors(g, &out_neis, i, IGRAPH_OUT, IGRAPH_LOOPS, IGRAPH_NO_MULTIPLE);
		splc_nodes[i].out_degree = (uint32_t)igraph_vector_int_size(&out_neis);
		edge_offset += (uint32_t)igraph_vector_int_size(&out_neis);
	}

	SPLCEdge *splc_edges = calloc(edge_offset, sizeof(SPLCEdge));
	uint32_t e_idx = 0;
	for (igraph_integer_t i = 0; i < n; i++) {
		igraph_vector_int_clear(&out_neis);
		igraph_neighbors(g, &out_neis, i, IGRAPH_OUT, IGRAPH_LOOPS, IGRAPH_NO_MULTIPLE);
		for (igraph_integer_t j = 0; j < igraph_vector_int_size(&out_neis); j++) {
			splc_edges[e_idx].target_node = (uint32_t)VECTOR(out_neis)[j];
			splc_edges[e_idx].weight = 0.0f;
			e_idx++;
		}
	}
	igraph_vector_int_destroy(&out_neis);

	igraph_vector_int_t in_neis;
	igraph_vector_int_init(&in_neis, 0);
	float *traffic = calloc(n, sizeof(float));
	for (igraph_integer_t i = 0; i < n; i++) {
		igraph_vector_int_clear(&in_neis);
		igraph_neighbors(g, &in_neis, i, IGRAPH_IN, IGRAPH_LOOPS, IGRAPH_NO_MULTIPLE);
		if (igraph_vector_int_size(&in_neis) == 0)
			traffic[i] = 1.0f;
	}
	igraph_vector_int_destroy(&in_neis);

	*out_nodes = splc_nodes;
	*out_edges = splc_edges;
	*out_traffic = traffic;
	*out_total_edges = edge_offset;
}

static void splc_create_gpu_buffers(Renderer *r, igraph_integer_t n, uint32_t total_edges, SPLCNode *splc_nodes, SPLCEdge *splc_edges, float *traffic)
{
	VkDeviceSize node_buf_size = sizeof(SPLCNode) * n;
	VkDeviceSize edge_buf_size = sizeof(SPLCEdge) * total_edges;
	VkDeviceSize traffic_buf_size = sizeof(float) * n;
	VkDeviceSize level_buf_size = sizeof(uint32_t) * n;

	VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, node_buf_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, &r->splc.nodes_buffer, &r->splc.nodes_memory);
	VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, edge_buf_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, &r->splc.edges_buffer, &r->splc.edges_memory);
	VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, traffic_buf_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, &r->splc.traffic_buffer, &r->splc.traffic_memory);
	VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, level_buf_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, &r->splc.level_buffer, &r->splc.level_memory);
	VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, &r->splc.max_buffer, &r->splc.max_memory);

	update_buffer(r->core.device, r->splc.nodes_memory, node_buf_size, splc_nodes);
	update_buffer(r->core.device, r->splc.edges_memory, edge_buf_size, splc_edges);
	update_buffer(r->core.device, r->splc.traffic_memory, traffic_buf_size, traffic);
	uint32_t zero_max = 0;
	update_buffer(r->core.device, r->splc.max_memory, sizeof(uint32_t), &zero_max);
}

static void splc_write_compute_descriptors(Renderer *r)
{
	VkDescriptorBufferInfo nodeInfo = {r->splc.nodes_buffer, 0, VK_WHOLE_SIZE};
	VkDescriptorBufferInfo edgeInfo = {r->splc.edges_buffer, 0, VK_WHOLE_SIZE};
	VkDescriptorBufferInfo trafficInfo = {r->splc.traffic_buffer, 0, VK_WHOLE_SIZE};
	VkDescriptorBufferInfo levelInfo = {r->splc.level_buffer, 0, VK_WHOLE_SIZE};
	VkDescriptorBufferInfo maxInfo = {r->splc.max_buffer, 0, VK_WHOLE_SIZE};
	VkWriteDescriptorSet splcWrites[] = {
		VK_WRITE_DESC_BUFFER(r->descriptors.splc_set, 0, &nodeInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER), VK_WRITE_DESC_BUFFER(r->descriptors.splc_set, 1, &edgeInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER), VK_WRITE_DESC_BUFFER(r->descriptors.splc_set, 2, &trafficInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER), VK_WRITE_DESC_BUFFER(r->descriptors.splc_set, 3, &levelInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER), VK_WRITE_DESC_BUFFER(r->descriptors.splc_set, 4, &maxInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
	};
	vkUpdateDescriptorSets(r->core.device, 5, splcWrites, 0, NULL);
}

// ============================================================================
// Initialize SPLC buffers from a GraphData instance
// ============================================================================
void renderer_init_splc_buffers(Renderer *r, GraphData *graph)
{
	if (!r->core.has_atomic_float)
		return;

	BufPair old_bufs[5];
	splc_save_old_buffers(r, old_bufs);
	splc_destroy_old_buffers(r);

	igraph_integer_t n = graph->node_count;
	igraph_integer_t m = graph->edge_count;
	if (n == 0 || !igraph_is_directed(&graph->g))
		return;

	igraph_vector_int_t levels;
	igraph_vector_int_init(&levels, 0);
	igraph_integer_t max_level = calculate_dag_levels(&graph->g, &levels);
	if (max_level < 0) {
		igraph_vector_int_destroy(&levels);
		splc_create_fallback_buffers(r, m);
		splc_write_graphics_descriptors(r);
		splc_destroy_buffer(r->core.device, old_bufs[1].buf, old_bufs[1].mem);
		splc_destroy_buffer(r->core.device, old_bufs[4].buf, old_bufs[4].mem);
		return;
	}

	r->splc.num_levels = (int)max_level + 1;
	r->splc.level_interval = r->splc.num_levels > 0 ? 5.0f / r->splc.num_levels : 0.5f;
	if (r->splc.level_interval < 0.016f)
		r->splc.level_interval = 0.016f;

	splc_build_level_groups(r, &levels, n);

	SPLCNode *splc_nodes;
	SPLCEdge *splc_edges;
	float *traffic;
	uint32_t total_splc_edges;
	splc_build_topology(&graph->g, n, &splc_nodes, &splc_edges, &traffic, &total_splc_edges);

	splc_create_gpu_buffers(r, n, total_splc_edges, splc_nodes, splc_edges, traffic);
	splc_write_compute_descriptors(r);
	splc_write_graphics_descriptors(r);

	free(splc_nodes);
	free(splc_edges);
	free(traffic);
	igraph_vector_int_destroy(&levels);

	for (int i = 0; i < 5; i++)
		splc_destroy_buffer(r->core.device, old_bufs[i].buf, old_bufs[i].mem);

	r->edge.count = graph->edge_count;
	r->splc.active = true;
	r->splc.current_level = 0;
	r->splc.last_level_time = r->anim.data.time;
}

void renderer_dispatch_splc_level(Renderer *r, VkCommandBuffer cmd)
{
	if (!r->core.has_atomic_float || !r->splc.active || r->splc.level_groups == NULL)
		return;
	if (r->splc.current_level >= r->splc.num_levels) {
		r->splc.readback_pending = true;
		return;
	}

	igraph_vector_int_t *level_nodes = r->splc.level_groups[r->splc.current_level];
	uint32_t num_in_level = (uint32_t)igraph_vector_int_size(level_nodes);
	if (num_in_level == 0)
		goto advance;

	// Upload current level node IDs to level buffer
	uint32_t *node_ids = malloc(sizeof(uint32_t) * num_in_level);
	for (uint32_t i = 0; i < num_in_level; i++)
		node_ids[i] = (uint32_t)VECTOR(*level_nodes)[i];
	update_buffer(r->core.device, r->splc.level_memory, sizeof(uint32_t) * num_in_level, node_ids);
	free(node_ids);

	// Barrier: ensure traffic writes from previous animation steps are visible
	VkMemoryBarrier memBarrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_SHADER_READ_BIT};
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &memBarrier, 0, NULL, 0, NULL);

	// Bind SPLC compute pipeline
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, r->pipelines.compute_splc);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, r->splc.pipeline_layout, 0, 1, &r->descriptors.splc_set, 0, NULL);

	// Push constant: num_nodes_in_level
	vkCmdPushConstants(cmd, r->splc.pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t), &num_in_level);

	// Dispatch
	vkCmdDispatch(cmd, (num_in_level + 63) / 64, 1, 1);

advance:
	r->splc.current_level++;
	r->splc.last_level_time = r->anim.data.time;

	// Barrier: synchronize compute writes (edges, traffic) with vertex shader reads
	VkMemoryBarrier computeToVertexBarrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_SHADER_READ_BIT};
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 1, &computeToVertexBarrier, 0, NULL, 0, NULL);
}

// ============================================================================
// Read back SPLC edge weights from GPU and sync to host GraphData
// ============================================================================
void renderer_readback_splc_weights(Renderer *r, GraphData *graph)
{
	if (!r->core.has_atomic_float || r->splc.edges_memory == VK_NULL_HANDLE)
		return;

	VkDeviceSize edge_buf_size = sizeof(SPLCEdge) * graph->edge_count;
	void *mapped;
	VK_CHECK(vkMapMemory(r->core.device, r->splc.edges_memory, 0, edge_buf_size, 0, &mapped), "Failed to map SPLC edges for readback");
	SPLCEdge *splc_edges = malloc(edge_buf_size);
	memcpy(splc_edges, mapped, edge_buf_size);
	vkUnmapMemory(r->core.device, r->splc.edges_memory);

	igraph_vector_int_t out_neis;
	igraph_vector_int_init(&out_neis, 0);
	uint32_t e_idx = 0;
	float max_w = 0.0f;
	double total_w = 0.0;
	for (igraph_integer_t i = 0; i < graph->node_count; i++) {
		igraph_neighbors(&graph->g, &out_neis, i, IGRAPH_OUT, IGRAPH_LOOPS, IGRAPH_NO_MULTIPLE);
		for (igraph_integer_t j = 0; j < igraph_vector_int_size(&out_neis); j++) {
			igraph_integer_t eid;
			igraph_get_eid(&graph->g, &eid, i, VECTOR(out_neis)[j], IGRAPH_DIRECTED, false);
			float w = splc_edges[e_idx].weight;
			graph->edges[eid].weight = w;
			if (w > max_w)
				max_w = w;
			total_w += w;
			e_idx++;
		}
		igraph_vector_int_clear(&out_neis);
	}
	igraph_vector_int_destroy(&out_neis);
	free(splc_edges);

	printf("SPLC readback: %u edges, max weight: %.2f, total: %.2f\n", e_idx, max_w, total_w);
}
