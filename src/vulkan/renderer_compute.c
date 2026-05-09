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
		vkWaitForFences(r->core.device, 1, &ctx->fence, VK_TRUE, UINT64_MAX);
		vkResetFences(r->core.device, 1, &ctx->fence);
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
		VkCommandPoolCreateInfo commandPoolInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = 0};
		vkCreateCommandPool(r->core.device, &commandPoolInfo, NULL, &ctx->cmdPool);

		VkCommandBufferAllocateInfo commandBufferAllocInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = ctx->cmdPool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
		vkAllocateCommandBuffers(r->core.device, &commandBufferAllocInfo, &ctx->cmdBuf);

		VkFenceCreateInfo fenceInfo = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT};
		vkCreateFence(r->core.device, &fenceInfo, NULL, &ctx->fence);

		VkDescriptorPoolSize descriptorPoolSizes = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3};
		VkDescriptorPoolCreateInfo descriptorPoolInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &descriptorPoolSizes};
		vkCreateDescriptorPool(r->core.device, &descriptorPoolInfo, NULL, &ctx->pool);

		VkDescriptorSetAllocateInfo descriptorSetAllocInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = ctx->pool, .descriptorSetCount = 1, .pSetLayouts = &r->computeDescriptorSetLayout};
		vkAllocateDescriptorSets(r->core.device, &descriptorSetAllocInfo, &ctx->descSet);

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
	vkResetCommandBuffer(ctx->cmdBuf, 0);
	VkCommandBufferBeginInfo beginInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
	vkBeginCommandBuffer(ctx->cmdBuf, &beginInfo);

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
	vkEndCommandBuffer(ctx->cmdBuf);

	// Submit with fence (no vkQueueWaitIdle)
	VkSubmitInfo submitInfo = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &ctx->cmdBuf};
	VkResult result = vkQueueSubmit(r->core.graphicsQueue, 1, &submitInfo, ctx->fence);
	if (result != VK_SUCCESS) {
		free(cNodes);
		free(cEdges);
		return result;
	}

	// Wait for completion and read back
	vkWaitForFences(r->core.device, 1, &ctx->fence, VK_TRUE, UINT64_MAX);

	void *mapped;
	vkMapMemory(r->core.device, ctx->edgeMem, 0, edgeSize, 0, &mapped);
	memcpy(edgeResults, mapped, sizeof(CompEdge) * graph->edge_count);
	vkUnmapMemory(r->core.device, ctx->edgeMem);

	free(cNodes);
	free(cEdges);

	return VK_SUCCESS;
}
