#include "vulkan/renderer_compute.h"

#include <stdlib.h>
#include <string.h>

#include "graph/wrappers_splc.h"
#include "vulkan/buffers.h"
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
		VkFenceCreateInfo fenceInfo = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT};
		VK_CHECK(vkCreateFence(r->core.device, &fenceInfo, NULL, &ctx->fence), "Failed to create compute fence");
		VK_CHECK(vkResetFences(r->core.device, 1, &ctx->fence), "Failed to reset compute fence after creation");

		VkDescriptorPoolSize descriptorPoolSizes = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3};
		VkDescriptorPoolCreateInfo descriptorPoolInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &descriptorPoolSizes};
		VK_CHECK(vkCreateDescriptorPool(r->core.device, &descriptorPoolInfo, NULL, &ctx->pool), "Failed to create compute descriptor pool");

		VkDescriptorSetAllocateInfo descriptorSetAllocInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = ctx->pool, .descriptorSetCount = 1, .pSetLayouts = &r->computeDescriptorSetLayout};
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
			vkDestroyBuffer(r->core.device, ctx->nodeBuf, NULL);
			vkFreeMemory(r->core.device, ctx->nodeMem, NULL);
			ctx->nodeBuf = VK_NULL_HANDLE;
		}
	}
	if (ctx->nodeBuf == VK_NULL_HANDLE) {
		create_buffer(r->core.device, r->core.physicalDevice, nodeSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &ctx->nodeBuf, &ctx->nodeMem);
	}

	if (ctx->edgeBuf != VK_NULL_HANDLE) {
		VkMemoryRequirements memReqs;
		vkGetBufferMemoryRequirements(r->core.device, ctx->edgeBuf, &memReqs);
		if (memReqs.size < edgeSize) {
			vkDestroyBuffer(r->core.device, ctx->edgeBuf, NULL);
			vkFreeMemory(r->core.device, ctx->edgeMem, NULL);
			ctx->edgeBuf = VK_NULL_HANDLE;
		}
	}
	if (ctx->edgeBuf == VK_NULL_HANDLE) {
		create_buffer(r->core.device, r->core.physicalDevice, edgeSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &ctx->edgeBuf, &ctx->edgeMem);
	}

	if (ctx->hubBuf == VK_NULL_HANDLE) {
		create_buffer(r->core.device, r->core.physicalDevice, sizeof(CompHub), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &ctx->hubBuf, &ctx->hubMem);
	}

	// Upload node and edge data to GPU
	update_buffer(r->core.device, ctx->nodeMem, nodeSize, cNodes);
	update_buffer(r->core.device, ctx->edgeMem, edgeSize, cEdges);

	// Update descriptor set with storage buffers
	VkDescriptorBufferInfo nodeBufferInfo = {ctx->nodeBuf, 0, VK_WHOLE_SIZE}, edgeBufferInfo = {ctx->edgeBuf, 0, VK_WHOLE_SIZE}, hubBufferInfo = {ctx->hubBuf, 0, VK_WHOLE_SIZE};
	VkWriteDescriptorSet writes[3] = {{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, ctx->descSet, 0, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, NULL, &nodeBufferInfo, NULL}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, ctx->descSet, 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, NULL, &edgeBufferInfo, NULL}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, ctx->descSet, 2, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, NULL, &hubBufferInfo, NULL}};
	vkUpdateDescriptorSets(r->core.device, 3, writes, 0, NULL);

	// Record command buffer
	VK_CHECK(vkResetCommandBuffer(ctx->cmdBuf, 0), "Failed to reset compute command buffer");
	VkCommandBufferBeginInfo beginInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
	VK_CHECK(vkBeginCommandBuffer(ctx->cmdBuf, &beginInfo), "Failed to begin compute command buffer");

	vkCmdBindPipeline(ctx->cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, r->computeSphericalPipeline);
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

void renderer_init_splc_buffers(Renderer *r, GraphData *graph)
{
	// Destroy old buffers if they exist
	if (r->splc_nodes_buffer != VK_NULL_HANDLE) {
		vkDestroyBuffer(r->core.device, r->splc_nodes_buffer, NULL);
		vkFreeMemory(r->core.device, r->splc_nodes_memory, NULL);
	}
	if (r->splc_edges_buffer != VK_NULL_HANDLE) {
		vkDestroyBuffer(r->core.device, r->splc_edges_buffer, NULL);
		vkFreeMemory(r->core.device, r->splc_edges_memory, NULL);
	}
	if (r->splc_traffic_buffer != VK_NULL_HANDLE) {
		vkDestroyBuffer(r->core.device, r->splc_traffic_buffer, NULL);
		vkFreeMemory(r->core.device, r->splc_traffic_memory, NULL);
	}
	if (r->splc_level_buffer != VK_NULL_HANDLE) {
		vkDestroyBuffer(r->core.device, r->splc_level_buffer, NULL);
		vkFreeMemory(r->core.device, r->splc_level_memory, NULL);
	}
	// Free old level groups
	if (r->splc_level_groups) {
		for (int i = 0; i < r->splc_num_levels; i++) {
			if (r->splc_level_groups[i])
				igraph_vector_int_destroy(r->splc_level_groups[i]);
			free(r->splc_level_groups[i]);
		}
		free(r->splc_level_groups);
		r->splc_level_groups = NULL;
	}

	r->splc_active = false;
	r->splc_current_level = 0;
	r->splc_timer = 0;
	r->splc_frames_per_level = 5;
	r->splc_max_weight = 0.0f;

	igraph_integer_t n = graph->node_count;
	igraph_integer_t m = graph->edge_count;
	if (n == 0 || !igraph_is_directed(&graph->g))
		return;

	// Compute levels
	igraph_vector_int_t levels;
	igraph_vector_int_init(&levels, 0);
	igraph_integer_t max_level = calculate_dag_levels(&graph->g, &levels);
	if (max_level < 0) {
		igraph_vector_int_destroy(&levels);
		return;
	}
	r->splc_num_levels = (int)max_level + 1;

	// Build level groups
	r->splc_level_groups = calloc(r->splc_num_levels, sizeof(igraph_vector_int_t *));
	for (int l = 0; l < r->splc_num_levels; l++) {
		r->splc_level_groups[l] = malloc(sizeof(igraph_vector_int_t));
		igraph_vector_int_init(r->splc_level_groups[l], 0);
	}
	for (igraph_integer_t i = 0; i < n; i++) {
		int lvl = (int)VECTOR(levels)[i];
		if (lvl >= 0 && lvl < r->splc_num_levels)
			igraph_vector_int_push_back(r->splc_level_groups[lvl], i);
	}

	// Build SPLC node and edge arrays (flattened)
	// First pass: compute edge_offset for each node
	SPLCNode *splc_nodes = calloc(n, sizeof(SPLCNode));
	igraph_vector_int_t out_neis;
	igraph_vector_int_init(&out_neis, 0);
	uint32_t edge_offset = 0;
	for (igraph_integer_t i = 0; i < n; i++) {
		splc_nodes[i].edge_offset = edge_offset;
		igraph_neighbors(&graph->g, &out_neis, i, IGRAPH_OUT, IGRAPH_LOOPS, IGRAPH_NO_MULTIPLE);
		splc_nodes[i].out_degree = (uint32_t)igraph_vector_int_size(&out_neis);
		edge_offset += (uint32_t)igraph_vector_int_size(&out_neis);
	}

	// Second pass: fill edges
	SPLCEdge *splc_edges = calloc(m, sizeof(SPLCEdge));
	uint32_t e_idx = 0;
	for (igraph_integer_t i = 0; i < n; i++) {
		igraph_vector_int_clear(&out_neis);
		igraph_neighbors(&graph->g, &out_neis, i, IGRAPH_OUT, IGRAPH_LOOPS, IGRAPH_NO_MULTIPLE);
		for (igraph_integer_t j = 0; j < igraph_vector_int_size(&out_neis); j++) {
			splc_edges[e_idx].target_node = (uint32_t)VECTOR(out_neis)[j];
			splc_edges[e_idx].weight = 0.0f;
			e_idx++;
		}
	}
	igraph_vector_int_destroy(&out_neis);

	// Initialize traffic: 1.0 for source nodes (in-degree 0), 0.0 otherwise
	float *traffic = calloc(n, sizeof(float));
	for (igraph_integer_t i = 0; i < n; i++) {
		igraph_vector_int_clear(&out_neis);
		igraph_neighbors(&graph->g, &out_neis, i, IGRAPH_IN, IGRAPH_LOOPS, IGRAPH_NO_MULTIPLE);
		if (igraph_vector_int_size(&out_neis) == 0)
			traffic[i] = 1.0f;
	}
	igraph_vector_int_destroy(&out_neis);

	// Count total edges from the flat array
	uint32_t total_splc_edges = 0;
	for (igraph_integer_t i = 0; i < n; i++)
		total_splc_edges += splc_nodes[i].out_degree;

	// Create GPU buffers
	VkDeviceSize node_buf_size = sizeof(SPLCNode) * n;
	VkDeviceSize edge_buf_size = sizeof(SPLCEdge) * total_splc_edges;
	VkDeviceSize traffic_buf_size = sizeof(float) * n;
	VkDeviceSize level_buf_size = sizeof(uint32_t) * n;

	create_buffer(r->core.device, r->core.physicalDevice, node_buf_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->splc_nodes_buffer, &r->splc_nodes_memory);
	create_buffer(r->core.device, r->core.physicalDevice, edge_buf_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->splc_edges_buffer, &r->splc_edges_memory);
	create_buffer(r->core.device, r->core.physicalDevice, traffic_buf_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->splc_traffic_buffer, &r->splc_traffic_memory);
	create_buffer(r->core.device, r->core.physicalDevice, level_buf_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->splc_level_buffer, &r->splc_level_memory);

	// Upload data
	update_buffer(r->core.device, r->splc_nodes_memory, node_buf_size, splc_nodes);
	update_buffer(r->core.device, r->splc_edges_memory, edge_buf_size, splc_edges);
	update_buffer(r->core.device, r->splc_traffic_memory, traffic_buf_size, traffic);

	// Update SPLC compute descriptor set
	VkDescriptorBufferInfo nodeInfo = {r->splc_nodes_buffer, 0, VK_WHOLE_SIZE};
	VkDescriptorBufferInfo edgeInfo = {r->splc_edges_buffer, 0, VK_WHOLE_SIZE};
	VkDescriptorBufferInfo trafficInfo = {r->splc_traffic_buffer, 0, VK_WHOLE_SIZE};
	VkDescriptorBufferInfo levelInfo = {r->splc_level_buffer, 0, VK_WHOLE_SIZE};
	VkWriteDescriptorSet splcWrites[] = {
		{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, r->splc_descriptor_set, 0, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, NULL, &nodeInfo, NULL},
		{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, r->splc_descriptor_set, 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, NULL, &edgeInfo, NULL},
		{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, r->splc_descriptor_set, 2, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, NULL, &trafficInfo, NULL},
		{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, r->splc_descriptor_set, 3, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, NULL, &levelInfo, NULL},
	};
	vkUpdateDescriptorSets(r->core.device, 4, splcWrites, 0, NULL);

	// Update the graphics pipeline edge weight SSBO descriptor set (binding 2 of the main descriptor set).
	// Same buffer as the compute edges buffer - barrier ensures correctness.
	VkDescriptorBufferInfo edgeWeightInfo = {r->splc_edges_buffer, 0, VK_WHOLE_SIZE};
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT * MAX_VIEWS; i++) {
		VkWriteDescriptorSet weightWrite = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, r->descriptorSets[i], 2, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, NULL, &edgeWeightInfo, NULL};
		vkUpdateDescriptorSets(r->core.device, 1, &weightWrite, 0, NULL);
	}

	free(splc_nodes);
	free(splc_edges);
	free(traffic);
	igraph_vector_int_destroy(&levels);

	// Set max weight as number of source nodes (level 0 count) for normalization
	if (r->splc_num_levels > 0)
		r->splc_max_weight = (float)igraph_vector_int_size(r->splc_level_groups[0]);
	else
		r->splc_max_weight = 1.0f;

	r->splc_active = true;
	r->splc_current_level = 0;
	r->splc_timer = 0;
}

void renderer_reset_splc(Renderer *r)
{
	if (r->splc_traffic_buffer == VK_NULL_HANDLE)
		return;
	// Reset traffic: source nodes get 1.0, others 0.0
	// Re-read from level groups since we don't store in-degrees
	uint32_t n = 0;
	for (int i = 0; i < r->splc_num_levels; i++)
		n += (uint32_t)igraph_vector_int_size(r->splc_level_groups[i]);

	float *traffic = calloc(n > 0 ? n : 1, sizeof(float));
	// Source nodes are in level 0
	if (r->splc_num_levels > 0) {
		for (int i = 0; i < igraph_vector_int_size(r->splc_level_groups[0]); i++) {
			uint32_t src = (uint32_t)VECTOR(*r->splc_level_groups[0])[i];
			if (src < n)
				traffic[src] = 1.0f;
		}
	}
	update_buffer(r->core.device, r->splc_traffic_memory, sizeof(float) * n, traffic);
	free(traffic);

	// Zero out edge weights
	uint32_t edge_count = 0;
	if (r->splc_edges_buffer != VK_NULL_HANDLE) {
		// Estimate edge count from node buffer (we don't store total count separately)
		// Read back node buffer to compute total edges
		void *mapped;
		vkMapMemory(r->core.device, r->splc_nodes_memory, 0, VK_WHOLE_SIZE, 0, &mapped);
		SPLCNode *nodes = (SPLCNode *)mapped;
		for (uint32_t i = 0; i < n; i++)
			edge_count += nodes[i].out_degree;
		vkUnmapMemory(r->core.device, r->splc_nodes_memory);
	}

	if (edge_count > 0) {
		float *zero_weights = calloc(edge_count, sizeof(SPLCEdge));
		update_buffer(r->core.device, r->splc_edges_memory, sizeof(SPLCEdge) * edge_count, zero_weights);
		free(zero_weights);
	}

	r->splc_current_level = 0;
	r->splc_timer = 0;
	r->splc_max_weight = 0.0f;
	r->splc_active = true;
}

void renderer_dispatch_splc_level(Renderer *r, VkCommandBuffer cmd)
{
	if (!r->splc_active || r->splc_level_groups == NULL)
		return;
	if (r->splc_current_level >= r->splc_num_levels) {
		r->splc_active = false;
		return;
	}

	igraph_vector_int_t *level_nodes = r->splc_level_groups[r->splc_current_level];
	uint32_t num_in_level = (uint32_t)igraph_vector_int_size(level_nodes);
	if (num_in_level == 0)
		goto advance;

	// Upload current level node IDs to level buffer
	uint32_t *node_ids = malloc(sizeof(uint32_t) * num_in_level);
	for (uint32_t i = 0; i < num_in_level; i++)
		node_ids[i] = (uint32_t)VECTOR(*level_nodes)[i];
	update_buffer(r->core.device, r->splc_level_memory, sizeof(uint32_t) * num_in_level, node_ids);
	free(node_ids);

	// Barrier: ensure traffic writes from previous animation steps are visible
	VkMemoryBarrier memBarrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_SHADER_READ_BIT};
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &memBarrier, 0, NULL, 0, NULL);

	// Bind SPLC compute pipeline
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, r->splc_compute_pipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, r->splc_compute_pipeline_layout, 0, 1, &r->splc_descriptor_set, 0, NULL);

	// Push constant: num_nodes_in_level
	vkCmdPushConstants(cmd, r->splc_compute_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t), &num_in_level);

	// Dispatch
	vkCmdDispatch(cmd, (num_in_level + 63) / 64, 1, 1);

advance:
	r->splc_current_level++;
	r->splc_timer = 0;

	// Barrier: synchronize compute writes (edges, traffic) with vertex shader reads
	VkMemoryBarrier computeToVertexBarrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_SHADER_READ_BIT};
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 1, &computeToVertexBarrier, 0, NULL, 0, NULL);
}
