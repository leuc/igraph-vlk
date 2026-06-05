#include "vulkan/renderer_compute.h"

#include <stdlib.h>
#include <string.h>

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
		createBuffer(r->core.device, r->core.physicalDevice, nodeSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &ctx->nodeBuf, &ctx->nodeMem);
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
		createBuffer(r->core.device, r->core.physicalDevice, edgeSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &ctx->edgeBuf, &ctx->edgeMem);
	}

	if (ctx->hubBuf == VK_NULL_HANDLE) {
		createBuffer(r->core.device, r->core.physicalDevice, sizeof(CompHub), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &ctx->hubBuf, &ctx->hubMem);
	}

	// Upload node and edge data to GPU
	updateBuffer(r->core.device, ctx->nodeMem, nodeSize, cNodes);
	updateBuffer(r->core.device, ctx->edgeMem, edgeSize, cEdges);

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
