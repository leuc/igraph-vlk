#include "vulkan/renderer_bcgl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vulkan/buffers.h"
#include "vulkan/utils.h"

// ============================================================================
// BCGL default hyperparameters
// ============================================================================
static const BCGLPushConstants BCGL_DEFAULTS = {
	.lambda_bc = 1.0f,
	.lambda_compact = 0.01f,
	.lambda_length = 0.01f,
	.learning_rate = 0.01f,
	.momentum = 0.9f,
	.b = 1.0f,
};

// ============================================================================
// Build CSR topology from igraph
// ============================================================================
static void bcgl_build_topology(const igraph_t *g, igraph_integer_t n, BCGLTopoNode **out_nodes, BCGLTopoEdge **out_edges, uint32_t *out_total_edges)
{
	BCGLTopoNode *topo_nodes = calloc(n, sizeof(BCGLTopoNode));
	igraph_vector_int_t out_neis;
	igraph_vector_int_init(&out_neis, 0);

	uint32_t edge_offset = 0;
	for (igraph_integer_t i = 0; i < n; i++) {
		topo_nodes[i].edge_offset = edge_offset;
		igraph_neighbors(g, &out_neis, i, IGRAPH_OUT, IGRAPH_LOOPS, IGRAPH_NO_MULTIPLE);
		topo_nodes[i].out_degree = (uint32_t)igraph_vector_int_size(&out_neis);
		edge_offset += (uint32_t)igraph_vector_int_size(&out_neis);
	}

	BCGLTopoEdge *topo_edges = calloc(edge_offset, sizeof(BCGLTopoEdge));
	uint32_t e_idx = 0;
	for (igraph_integer_t i = 0; i < n; i++) {
		igraph_vector_int_clear(&out_neis);
		igraph_neighbors(g, &out_neis, i, IGRAPH_OUT, IGRAPH_LOOPS, IGRAPH_NO_MULTIPLE);
		for (igraph_integer_t j = 0; j < igraph_vector_int_size(&out_neis); j++) {
			topo_edges[e_idx].target_node = (uint32_t)VECTOR(out_neis)[j];
			topo_edges[e_idx].pad = 0;
			e_idx++;
		}
	}

	igraph_vector_int_destroy(&out_neis);
	*out_nodes = topo_nodes;
	*out_edges = topo_edges;
	*out_total_edges = edge_offset;
}

// ============================================================================
// Destroy old buffers if they exist
// ============================================================================
static void bcgl_destroy_old_buffers(Renderer *r)
{
	BCGLComputeContext *ctx = &r->bcgl_ctx;

	if (ctx->fence != VK_NULL_HANDLE) {
		vkWaitForFences(r->core.device, 1, &ctx->fence, VK_TRUE, UINT64_MAX);
	}

	VK_DESTROY_BUFFER(r->core.device, ctx->node_buf, ctx->node_mem);
	VK_DESTROY_BUFFER(r->core.device, ctx->topo_nodes_buf, ctx->topo_nodes_mem);
	VK_DESTROY_BUFFER(r->core.device, ctx->topo_edges_buf, ctx->topo_edges_mem);

	ctx->capacity_nodes = 0;
	ctx->capacity_edges = 0;
	ctx->active = false;
}

// ============================================================================
// Create or resize GPU buffers
// ============================================================================
static void bcgl_create_gpu_buffers(Renderer *r, igraph_integer_t n, uint32_t total_edges)
{
	BCGLComputeContext *ctx = &r->bcgl_ctx;

	// Destroy if capacity is insufficient
	if (ctx->capacity_nodes < (uint32_t)n || ctx->capacity_edges < total_edges) {
		bcgl_destroy_old_buffers(r);
	}

	if (ctx->node_buf == VK_NULL_HANDLE) {
		VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, sizeof(BCGLNodeData) * n, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &ctx->node_buf, &ctx->node_mem);
		ctx->capacity_nodes = (uint32_t)n;
	}
	if (ctx->topo_nodes_buf == VK_NULL_HANDLE) {
		VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, sizeof(BCGLTopoNode) * n, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, &ctx->topo_nodes_buf, &ctx->topo_nodes_mem);
	}
	if (ctx->topo_edges_buf == VK_NULL_HANDLE && total_edges > 0) {
		VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, sizeof(BCGLTopoEdge) * total_edges, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, &ctx->topo_edges_buf, &ctx->topo_edges_mem);
		ctx->capacity_edges = total_edges;
	}
}

// ============================================================================
// Write topology to GPU (one-time after graph load)
// ============================================================================
static void bcgl_upload_topology(Renderer *r, igraph_integer_t n, uint32_t total_edges, BCGLTopoNode *topo_nodes, BCGLTopoEdge *topo_edges)
{
	BCGLComputeContext *ctx = &r->bcgl_ctx;

	update_buffer(r->core.device, ctx->topo_nodes_mem, sizeof(BCGLTopoNode) * n, topo_nodes);
	if (total_edges > 0)
		update_buffer(r->core.device, ctx->topo_edges_mem, sizeof(BCGLTopoEdge) * total_edges, topo_edges);
}

// ============================================================================
// Seed node positions from current graph data
// ============================================================================
static void bcgl_seed_positions(Renderer *r, GraphData *graph)
{
	BCGLComputeContext *ctx = &r->bcgl_ctx;
	igraph_integer_t n = graph->node_count;

	BCGLNodeData *nodes = calloc(n, sizeof(BCGLNodeData));
	for (igraph_integer_t i = 0; i < n; i++) {
		nodes[i].pos[0] = graph->nodes[i].position[0];
		nodes[i].pos[1] = graph->nodes[i].position[1];
		nodes[i].pos[2] = graph->nodes[i].position[2];
		nodes[i].pad0 = 0;
		nodes[i].velocity[0] = nodes[i].velocity[1] = nodes[i].velocity[2] = 0;
		nodes[i].pad1 = 0;
	}
	update_buffer(r->core.device, ctx->node_mem, sizeof(BCGLNodeData) * n, nodes);
	free(nodes);
}

// ============================================================================
// Update descriptor set with storage buffers
// ============================================================================
static void bcgl_write_descriptors(Renderer *r)
{
	BCGLComputeContext *ctx = &r->bcgl_ctx;
	igraph_integer_t n = r->nodeCount;

	VkDescriptorBufferInfo nodeInfo = {ctx->node_buf, 0, VK_WHOLE_SIZE};
	VkDescriptorBufferInfo topoNodesInfo = {ctx->topo_nodes_buf, 0, VK_WHOLE_SIZE};
	VkDescriptorBufferInfo topoEdgesInfo = {ctx->topo_edges_buf, 0, VK_WHOLE_SIZE};

	VkWriteDescriptorSet writes[] = {
		VK_WRITE_DESC_BUFFER(ctx->desc_set, 0, &nodeInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
		VK_WRITE_DESC_BUFFER(ctx->desc_set, 1, &topoNodesInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
		VK_WRITE_DESC_BUFFER(ctx->desc_set, 2, &topoEdgesInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
	};
	vkUpdateDescriptorSets(r->core.device, 3, writes, 0, NULL);
}

// ============================================================================
// PUBLIC: Create BCGL compute pipeline
// ============================================================================
void renderer_create_bcgl_compute_pipeline(Renderer *r)
{
	BCGLComputeContext *ctx = &r->bcgl_ctx;

	// Descriptor set layout: 3 storage buffer bindings
	VkDescriptorSetLayoutBinding bindings[] = {
		{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
		{1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
		{2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
	};
	VkDescriptorSetLayoutCreateInfo layoutInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 3,
		.pBindings = bindings,
	};
	VK_CHECK(vkCreateDescriptorSetLayout(r->core.device, &layoutInfo, NULL, &ctx->desc_layout), "Failed to create BCGL descriptor set layout");

	// Pipeline layout with push constants
	VkPushConstantRange pushConstant = {
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
		.offset = 0,
		.size = sizeof(BCGLPushConstants),
	};
	VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &ctx->desc_layout,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &pushConstant,
	};
	VK_CHECK(vkCreatePipelineLayout(r->core.device, &pipelineLayoutInfo, NULL, &ctx->layout), "Failed to create BCGL pipeline layout");

	// Shader module
	VkShaderModule shaderModule = VK_NULL_HANDLE;
	VK_CHECK(create_shader_module(r->core.device, BCGL_COMP_SHADER_PATH, &shaderModule), "Failed to create BCGL compute shader module");
	VkPipelineShaderStageCreateInfo stage = VK_SHADER_STAGE_COMP(shaderModule);
	VkComputePipelineCreateInfo pipelineInfo = {
		.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
		.stage = stage,
		.layout = ctx->layout,
	};
	VK_CHECK(vkCreateComputePipelines(r->core.device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &ctx->pipeline), "Failed to create BCGL compute pipeline");
	vkDestroyShaderModule(r->core.device, shaderModule, NULL);

	// Descriptor pool
	VkDescriptorPoolSize poolSizes = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3};
	VkDescriptorPoolCreateInfo poolInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = 1,
		.poolSizeCount = 1,
		.pPoolSizes = &poolSizes,
	};
	VK_CHECK(vkCreateDescriptorPool(r->core.device, &poolInfo, NULL, &ctx->pool), "Failed to create BCGL descriptor pool");

	// Descriptor set
	VkDescriptorSetAllocateInfo setInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = ctx->pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &ctx->desc_layout,
	};
	VK_CHECK(vkAllocateDescriptorSets(r->core.device, &setInfo, &ctx->desc_set), "Failed to allocate BCGL descriptor set");

	// Command pool + buffer + fence — uses compute queue family
	VkCommandPoolCreateInfo cmdPoolInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.queueFamilyIndex = r->core.computeQueueFamily,
		.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
	};
	VK_CHECK(vkCreateCommandPool(r->core.device, &cmdPoolInfo, NULL, &ctx->cmd_pool), "Failed to create BCGL command pool");

	VkCommandBufferAllocateInfo cmdBufInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = ctx->cmd_pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1,
	};
	VK_CHECK(vkAllocateCommandBuffers(r->core.device, &cmdBufInfo, &ctx->cmd_buf), "Failed to allocate BCGL command buffer");

	VK_CHECK(vkCreateFence(r->core.device, &VK_SIGNALED_FENCE_INFO, NULL, &ctx->fence), "Failed to create BCGL fence");
}

// ============================================================================
// PUBLIC: Initialize BCGL buffers from graph
// ============================================================================
void renderer_init_bcgl_buffers(Renderer *r, GraphData *graph)
{
	BCGLComputeContext *ctx = &r->bcgl_ctx;
	if (!ctx->pipeline)
		return;

	igraph_integer_t n = graph->node_count;
	igraph_integer_t m = graph->edge_count;
	if (n == 0)
		return;

	bcgl_destroy_old_buffers(r);

	// Build CSR topology
	BCGLTopoNode *topo_nodes;
	BCGLTopoEdge *topo_edges;
	uint32_t total_edges;
	bcgl_build_topology(&graph->g, n, &topo_nodes, &topo_edges, &total_edges);

	// Create GPU buffers
	bcgl_create_gpu_buffers(r, n, total_edges);

	// Upload topology and seed positions
	bcgl_upload_topology(r, n, total_edges, topo_nodes, topo_edges);
	bcgl_seed_positions(r, graph);

	// Write descriptors
	bcgl_write_descriptors(r);

	free(topo_nodes);
	free(topo_edges);

	ctx->active = true;
	ctx->iterations_dispatched = 0;
	ctx->total_iterations = 0;
}

// ============================================================================
// PUBLIC: Dispatch a chunk of BCGL iterations (non-blocking)
// ============================================================================
void renderer_dispatch_bcgl_chunk(Renderer *r, GraphData *graph, uint32_t iterations)
{
	BCGLComputeContext *ctx = &r->bcgl_ctx;
	if (!ctx->active || !ctx->pipeline)
		return;

	igraph_integer_t n = graph->node_count;
	if (n == 0)
		return;

	// Caller must ensure prior fence is signaled before calling this.
	// Reset the fence for this chunk's submission.
	if (ctx->fence != VK_NULL_HANDLE) {
		vkResetFences(r->core.device, 1, &ctx->fence);
	}

	VK_CHECK(vkResetCommandBuffer(ctx->cmd_buf, 0), "Failed to reset BCGL command buffer");
	VK_CHECK(vkBeginCommandBuffer(ctx->cmd_buf, &VK_CMD_BEGIN_INFO_ONETIME), "Failed to begin BCGL command buffer");

	vkCmdBindPipeline(ctx->cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, ctx->pipeline);
	vkCmdBindDescriptorSets(ctx->cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, ctx->layout, 0, 1, &ctx->desc_set, 0, NULL);

	BCGLPushConstants pcs = BCGL_DEFAULTS;
	pcs.vertexCount = (uint32_t)n;
	pcs.edgeCount = (uint32_t)graph->edge_count;

	vkCmdPushConstants(ctx->cmd_buf, ctx->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(BCGLPushConstants), &pcs);

	// Dispatch iteration loop with compute-to-compute barriers
	VkMemoryBarrier memBarrier = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
		.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
	};

	for (uint32_t iter = 0; iter < iterations; iter++) {
		vkCmdDispatch(ctx->cmd_buf, ((uint32_t)n + 255) / 256, 1, 1);

		if (iter < iterations - 1) {
			vkCmdPipelineBarrier(ctx->cmd_buf, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &memBarrier, 0, NULL, 0, NULL);
		}
	}

	// Final barrier: compute writes -> vertex reads (for subsequent renderer_update_graph)
	VkMemoryBarrier finalBarrier = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
		.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
	};
	vkCmdPipelineBarrier(ctx->cmd_buf, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 1, &finalBarrier, 0, NULL, 0, NULL);

	VK_CHECK(vkEndCommandBuffer(ctx->cmd_buf), "Failed to end BCGL command buffer");

	// Submit — do NOT wait for the fence here
	VkSubmitInfo submitInfo = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = &ctx->cmd_buf,
	};
	VK_CHECK(vkQueueSubmit(r->core.computeQueue, 1, &submitInfo, ctx->fence), "Failed to submit BCGL command buffer");
}

// ============================================================================
// PUBLIC: Check if the most recent BCGL dispatch has completed (non-blocking)
// ============================================================================
VkResult renderer_bcgl_fence_status(Renderer *r)
{
	BCGLComputeContext *ctx = &r->bcgl_ctx;
	if (ctx->fence == VK_NULL_HANDLE)
		return VK_SUCCESS;
	return vkGetFenceStatus(r->core.device, ctx->fence);
}

// ============================================================================
// PUBLIC: Read back positions from GPU to graph
// ============================================================================
void renderer_readback_bcgl_positions(Renderer *r, GraphData *graph)
{
	BCGLComputeContext *ctx = &r->bcgl_ctx;
	if (ctx->node_buf == VK_NULL_HANDLE || ctx->node_mem == VK_NULL_HANDLE)
		return;

	igraph_integer_t n = graph->node_count;
	VkDeviceSize node_buf_size = sizeof(BCGLNodeData) * n;
	void *mapped;
	VK_CHECK(vkMapMemory(r->core.device, ctx->node_mem, 0, node_buf_size, 0, &mapped), "Failed to map BCGL node buffer for readback");

	BCGLNodeData *gpu_nodes = (BCGLNodeData *)mapped;

	// Update current_layout matrix
	if (graph->current_layout.nrow != n || graph->current_layout.ncol < 3) {
		igraph_matrix_destroy(&graph->current_layout);
		igraph_matrix_init(&graph->current_layout, n, 3);
	}

	for (igraph_integer_t i = 0; i < n; i++) {
		graph->nodes[i].position[0] = gpu_nodes[i].pos[0];
		graph->nodes[i].position[1] = gpu_nodes[i].pos[1];
		graph->nodes[i].position[2] = gpu_nodes[i].pos[2];

		MATRIX(graph->current_layout, i, 0) = (igraph_real_t)gpu_nodes[i].pos[0];
		MATRIX(graph->current_layout, i, 1) = (igraph_real_t)gpu_nodes[i].pos[1];
		MATRIX(graph->current_layout, i, 2) = (igraph_real_t)gpu_nodes[i].pos[2];
	}

	vkUnmapMemory(r->core.device, ctx->node_mem);
}

// ============================================================================
// PUBLIC: Cleanup BCGL resources
// ============================================================================
void renderer_cleanup_bcgl(Renderer *r)
{
	BCGLComputeContext *ctx = &r->bcgl_ctx;

	if (ctx->fence != VK_NULL_HANDLE) {
		vkWaitForFences(r->core.device, 1, &ctx->fence, VK_TRUE, UINT64_MAX);
		vkDestroyFence(r->core.device, ctx->fence, NULL);
		ctx->fence = VK_NULL_HANDLE;
	}
	if (ctx->cmd_pool != VK_NULL_HANDLE) {
		vkDestroyCommandPool(r->core.device, ctx->cmd_pool, NULL);
		ctx->cmd_pool = VK_NULL_HANDLE;
		ctx->cmd_buf = VK_NULL_HANDLE;
	}
	VK_DESTROY_BUFFER(r->core.device, ctx->node_buf, ctx->node_mem);
	VK_DESTROY_BUFFER(r->core.device, ctx->topo_nodes_buf, ctx->topo_nodes_mem);
	VK_DESTROY_BUFFER(r->core.device, ctx->topo_edges_buf, ctx->topo_edges_mem);

	if (ctx->pool != VK_NULL_HANDLE) {
		vkDestroyDescriptorPool(r->core.device, ctx->pool, NULL);
		ctx->pool = VK_NULL_HANDLE;
	}
	if (ctx->pipeline != VK_NULL_HANDLE) {
		vkDestroyPipeline(r->core.device, ctx->pipeline, NULL);
		ctx->pipeline = VK_NULL_HANDLE;
	}
	if (ctx->layout != VK_NULL_HANDLE) {
		vkDestroyPipelineLayout(r->core.device, ctx->layout, NULL);
		ctx->layout = VK_NULL_HANDLE;
	}
	if (ctx->desc_layout != VK_NULL_HANDLE) {
		vkDestroyDescriptorSetLayout(r->core.device, ctx->desc_layout, NULL);
		ctx->desc_layout = VK_NULL_HANDLE;
	}
	ctx->active = false;
}
