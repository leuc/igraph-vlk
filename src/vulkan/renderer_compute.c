#include "vulkan/renderer_compute.h"

#include <stdlib.h>
#include <string.h>

#include "graph/layered_sphere.h"
#include "vulkan/utils.h"

VkResult renderer_dispatch_edge_routing(Renderer *r, GraphData *graph, CompEdge *edgeResults) {
	if (r->currentRoutingMode == ROUTING_MODE_STRAIGHT ||
		r->currentRoutingMode == ROUTING_MODE_3D_VOXEL) {
		// No compute needed for straight edges or voxel mode (not yet implemented)
		return VK_SUCCESS;
	}

	// Prepare compute shader input data
	CompNode *cNodes = malloc(sizeof(CompNode) * graph->node_count);
	CompEdge *cEdges = malloc(sizeof(CompEdge) * graph->edge_count);

	for (uint32_t i = 0; i < graph->node_count; i++) {
		glm_vec3_scale(graph->nodes[i].position, r->layoutScale,
					   cNodes[i].position);
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
		if (graph->active_layout == LAYOUT_LAYERED_SPHERE &&
			graph->layered_sphere && graph->layered_sphere->initialized) {
			int s1 = graph->layered_sphere
						 ->node_to_sphere_id[graph->edges[i].from];
			int s2 = graph->layered_sphere
						 ->node_to_sphere_id[graph->edges[i].to];
			cEdges[i].elevationLevel = (s1 != s2) ? 1 : 0;
		}
		cEdges[i].pathLength = 0;
	}

	// Create storage buffers for compute shader
	VkBuffer nBuf, eBuf, hBuf = VK_NULL_HANDLE;
	VkDeviceMemory nMem, eMem, hMem = VK_NULL_HANDLE;
	
	createBuffer(r->device, r->physicalDevice,
				 sizeof(CompNode) * graph->node_count,
				 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
					 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				 &nBuf, &nMem);
	createBuffer(r->device, r->physicalDevice,
				 sizeof(CompEdge) * graph->edge_count,
				 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
					 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				 &eBuf, &eMem);

	// Upload node and edge data to GPU
	updateBuffer(r->device, nMem, sizeof(CompNode) * graph->node_count, cNodes);
	updateBuffer(r->device, eMem, sizeof(CompEdge) * graph->edge_count, cEdges);

	// Prepare hub data if needed
	CompHub *cHubs = NULL;
	if (r->currentRoutingMode == ROUTING_MODE_3D_HUB_SPOKE &&
		graph->hub_count > 0) {
		cHubs = malloc(sizeof(CompHub) * graph->hub_count);
		for (int i = 0; i < graph->hub_count; i++) {
			glm_vec3_scale(graph->hubs[i].position, r->layoutScale,
						   cHubs[i].position);
			cHubs[i].pad = 0;
		}
		createBuffer(r->device, r->physicalDevice,
					 sizeof(CompHub) * graph->hub_count,
					 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
					 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
						 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
					 &hBuf, &hMem);
		updateBuffer(r->device, hMem, sizeof(CompHub) * graph->hub_count, cHubs);
	} else {
		// Create dummy hub buffer
		createBuffer(r->device, r->physicalDevice, sizeof(CompHub),
					 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
					 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
						 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
					 &hBuf, &hMem);
	}

	// Create transient descriptor pool and set for compute
	VkDescriptorPoolSize dps = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3};
	VkDescriptorPoolCreateInfo dpInfo = {
		VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, NULL, 0, 1, 1, &dps};
	VkDescriptorPool cPool;
	VkResult result = vkCreateDescriptorPool(r->device, &dpInfo, NULL, &cPool);
	if (result != VK_SUCCESS) {
		free(cNodes);
		free(cEdges);
		if (cHubs) free(cHubs);
		return result;
	}

	VkDescriptorSetAllocateInfo dsAlloc = {
		VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, NULL, cPool, 1,
		&r->computeDescriptorSetLayout};
	VkDescriptorSet cSet;
	result = vkAllocateDescriptorSets(r->device, &dsAlloc, &cSet);
	if (result != VK_SUCCESS) {
		vkDestroyDescriptorPool(r->device, cPool, NULL);
		free(cNodes);
		free(cEdges);
		if (cHubs) free(cHubs);
		return result;
	}

	// Update descriptor set with storage buffers
	VkDescriptorBufferInfo nbi = {nBuf, 0, VK_WHOLE_SIZE},
						   ebi = {eBuf, 0, VK_WHOLE_SIZE},
						   hbi = {hBuf, 0, VK_WHOLE_SIZE};
	VkWriteDescriptorSet writes[3] = {
		{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, cSet, 0, 0, 1,
		 VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, NULL, &nbi, NULL},
		{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, cSet, 1, 0, 1,
		 VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, NULL, &ebi, NULL},
		{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, cSet, 2, 0, 1,
		 VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, NULL, &hbi, NULL}};
	vkUpdateDescriptorSets(r->device, 3, writes, 0, NULL);

	// Allocate and record compute command buffer
	VkCommandBufferAllocateInfo cbAlloc = {
		VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, NULL,
		r->commandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1};
	VkCommandBuffer cBuf;
	result = vkAllocateCommandBuffers(r->device, &cbAlloc, &cBuf);
	if (result != VK_SUCCESS) {
		vkDestroyDescriptorPool(r->device, cPool, NULL);
		free(cNodes);
		free(cEdges);
		if (cHubs) free(cHubs);
		return result;
	}

	VkCommandBufferBeginInfo bInfo = {
		VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, NULL,
		VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, NULL};
	vkBeginCommandBuffer(cBuf, &bInfo);

	// Bind the appropriate compute pipeline
	if (r->currentRoutingMode == ROUTING_MODE_SPHERICAL_PCB)
		vkCmdBindPipeline(cBuf, VK_PIPELINE_BIND_POINT_COMPUTE,
						  r->computeSphericalPipeline);
	else if (r->currentRoutingMode == ROUTING_MODE_3D_HUB_SPOKE)
		vkCmdBindPipeline(cBuf, VK_PIPELINE_BIND_POINT_COMPUTE,
						  r->computeHubSpokePipeline);

	vkCmdBindDescriptorSets(cBuf, VK_PIPELINE_BIND_POINT_COMPUTE,
							r->computePipelineLayout, 0, 1, &cSet, 0, NULL);

	// Push constants for compute shader
	struct {
		int maxE;
		float baseR;
		int numHubs;
	} pcVals = {graph->edge_count, 5.0f * r->layoutScale, graph->hub_count};
	vkCmdPushConstants(cBuf, r->computePipelineLayout,
					   VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pcVals), &pcVals);

	// Dispatch compute shader
	vkCmdDispatch(cBuf, (graph->edge_count + 255) / 256, 1, 1);
	vkEndCommandBuffer(cBuf);

	// Submit and wait for completion
	VkSubmitInfo sInfo = {VK_STRUCTURE_TYPE_SUBMIT_INFO,
						  NULL,
						  0,
						  NULL,
						  NULL,
						  1,
						  &cBuf,
						  0,
						  NULL};
	result = vkQueueSubmit(r->graphicsQueue, 1, &sInfo, VK_NULL_HANDLE);
	if (result != VK_SUCCESS) {
		vkFreeCommandBuffers(r->device, r->commandPool, 1, &cBuf);
		vkDestroyDescriptorPool(r->device, cPool, NULL);
		free(cNodes);
		free(cEdges);
		if (cHubs) free(cHubs);
		return result;
	}

	vkQueueWaitIdle(r->graphicsQueue);

	// Read back computed edge results
	void *mapped;
	vkMapMemory(r->device, eMem, 0, VK_WHOLE_SIZE, 0, &mapped);
	memcpy(edgeResults, mapped, sizeof(CompEdge) * graph->edge_count);
	vkUnmapMemory(r->device, eMem);

	// Cleanup
	vkDestroyBuffer(r->device, hBuf, NULL);
	vkFreeMemory(r->device, hMem, NULL);
	if (cHubs)
		free(cHubs);

	vkFreeCommandBuffers(r->device, r->commandPool, 1, &cBuf);
	vkDestroyDescriptorPool(r->device, cPool, NULL);
	vkDestroyBuffer(r->device, nBuf, NULL);
	vkFreeMemory(r->device, nMem, NULL);
	vkDestroyBuffer(r->device, eBuf, NULL);
	vkFreeMemory(r->device, eMem, NULL);

	free(cNodes);
	free(cEdges);

	return VK_SUCCESS;
}
