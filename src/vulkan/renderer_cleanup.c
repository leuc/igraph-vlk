/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "vulkan/renderer_cleanup.h"

#include <stdlib.h>

#include "vulkan/buffers.h"
#include "vulkan/commands.h"
#include "vulkan/device.h"
#include "vulkan/render_pass.h"
#include "vulkan/renderer_bcgl.h"
#include "vulkan/swapchain.h"
#include "vulkan/text.h"
#include "vulkan/utils.h"

void cleanup_uniform_buffers(Renderer *r)
{
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT * MAX_VIEWS; i++)
		VK_DESTROY_BUFFER(r->core.device, r->ubo.buffers[i], r->ubo.memory[i]);
	for (int i = 0; i < GRAPH_UPDATE_RING_SIZE; i++)
		vkDestroyFence(r->core.device, r->graphUpdateFences[i], NULL);
}

void cleanup_compute_context(Renderer *r)
{
	if (r->computeCtx.fence != VK_NULL_HANDLE)
		vkDestroyFence(r->core.device, r->computeCtx.fence, NULL);
	if (r->computeCtx.cmdBuf != VK_NULL_HANDLE)
		vkFreeCommandBuffers(r->core.device, r->computeCtx.cmdPool, 1, &r->computeCtx.cmdBuf);
	if (r->computeCtx.cmdPool != VK_NULL_HANDLE)
		vkDestroyCommandPool(r->core.device, r->computeCtx.cmdPool, NULL);
	if (r->computeCtx.pool != VK_NULL_HANDLE)
		vkDestroyDescriptorPool(r->core.device, r->computeCtx.pool, NULL);
	VK_DESTROY_BUFFER(r->core.device, r->computeCtx.nodeBuf, r->computeCtx.nodeMem);
	VK_DESTROY_BUFFER(r->core.device, r->computeCtx.edgeBuf, r->computeCtx.edgeMem);
	VK_DESTROY_BUFFER(r->core.device, r->computeCtx.hubBuf, r->computeCtx.hubMem);
}

void cleanup_geometry_buffers(Renderer *r)
{
	VK_DESTROY_BUFFER(r->core.device, r->labelVertexBuffer, r->labelVertexBufferMemory);
	VK_DESTROY_BUFFER(r->core.device, r->edge.position, r->edge.position_memory);
	VK_DESTROY_BUFFER(r->core.device, r->edge.attribute, r->edge.attribute_memory);
	VK_DESTROY_BUFFER(r->core.device, r->edge.staging, r->edge.staging_memory);
	VK_DESTROY_BUFFER(r->core.device, r->node.position, r->node.position_memory);
	VK_DESTROY_BUFFER(r->core.device, r->node.attribute, r->node.attribute_memory);
	VK_DESTROY_BUFFER(r->core.device, r->node.staging, r->node.staging_memory);
	VK_DESTROY_BUFFER(r->core.device, r->nodeVertexBuffer, r->nodeVertexBufferMemory);
	VK_DESTROY_BUFFER(r->core.device, r->rayVertexBuffer, r->rayVertexBufferMemory);
	VK_DESTROY_BUFFER(r->core.device, r->uiBgVertexBuffer, r->uiBgVertexBufferMemory);
	VK_DESTROY_BUFFER(r->core.device, r->uiTextInstanceBuffer, r->uiTextInstanceBufferMemory);
	VK_DESTROY_BUFFER(r->core.device, r->uiBgInstanceBuffer, r->uiBgInstanceBufferMemory);
}

void cleanup_menu_label_atlases(Renderer *r)
{
	VK_DESTROY_BUFFER(r->core.device, r->menu.quad_vertex, r->menu.quad_vertex_memory);
	VK_DESTROY_BUFFER(r->core.device, r->menu.quad_index, r->menu.quad_index_memory);
	VK_DESTROY_BUFFER(r->core.device, r->menu.instance, r->menu.instance_memory);
	VK_DESTROY_BUFFER(r->core.device, r->menu.text_quad_instance, r->menu.text_quad_instance_memory);
	text_atlas_destroy(&r->menu.text_atlas, r->core.device);
	text_atlas_destroy(&r->nodeTextAtlas, r->core.device);
	text_atlas_destroy(&r->detailCardAtlas, r->core.device);
	igraph_bh_tree_destroy(&r->labelTree);
	VK_DESTROY_BUFFER(r->core.device, r->nodeLabelInstanceBuffer, r->nodeLabelInstanceBufferMemory);
	VK_DESTROY_BUFFER(r->core.device, r->detailCardInstanceBuffer, r->detailCardInstanceBufferMemory);
}

void cleanup_xr_resources(Renderer *r)
{
	if (!r->xrFramebuffers)
		return;
	for (uint32_t i = 0; i < r->xr_view_count; i++) {
		if (r->xrDepthImageViews[i] != VK_NULL_HANDLE)
			vkDestroyImageView(r->core.device, r->xrDepthImageViews[i], NULL);
		if (r->xrDepthImages[i] != VK_NULL_HANDLE)
			vkDestroyImage(r->core.device, r->xrDepthImages[i], NULL);
		if (r->xrDepthImageMemories[i] != VK_NULL_HANDLE)
			vkFreeMemory(r->core.device, r->xrDepthImageMemories[i], NULL);
		for (uint32_t j = 0; j < r->xrFramebufferImageCount[i]; j++)
			vkDestroyFramebuffer(r->core.device, r->xrFramebuffers[i][j], NULL);
		free(r->xrFramebuffers[i]);
	}
	free(r->xrFramebuffers);
	free(r->xrFramebufferImageCount);
	free(r->xrDepthImages);
	free(r->xrDepthImageMemories);
	free(r->xrDepthImageViews);
}

void cleanup_splc_pipelines_core(Renderer *r)
{
	VK_DESTROY_BUFFER(r->core.device, r->splc.nodes_buffer, r->splc.nodes_memory);
	VK_DESTROY_BUFFER(r->core.device, r->splc.edges_buffer, r->splc.edges_memory);
	VK_DESTROY_BUFFER(r->core.device, r->splc.traffic_buffer, r->splc.traffic_memory);
	VK_DESTROY_BUFFER(r->core.device, r->splc.level_buffer, r->splc.level_memory);
	VK_DESTROY_BUFFER(r->core.device, r->splc.max_buffer, r->splc.max_memory);
	if (r->splc.level_groups) {
		for (int i = 0; i < r->splc.num_levels; i++) {
			if (r->splc.level_groups[i])
				igraph_vector_int_destroy(r->splc.level_groups[i]);
			free(r->splc.level_groups[i]);
		}
		free(r->splc.level_groups);
	}
	if (r->descriptors.splc_pool != VK_NULL_HANDLE)
		vkDestroyDescriptorPool(r->core.device, r->descriptors.splc_pool, NULL);
	if (r->pipelines.compute_splc != VK_NULL_HANDLE)
		vkDestroyPipeline(r->core.device, r->pipelines.compute_splc, NULL);
	if (r->splc.pipeline_layout != VK_NULL_HANDLE)
		vkDestroyPipelineLayout(r->core.device, r->splc.pipeline_layout, NULL);
	if (r->descriptors.splc_compute_layout != VK_NULL_HANDLE)
		vkDestroyDescriptorSetLayout(r->core.device, r->descriptors.splc_compute_layout, NULL);

	vkDestroyDescriptorPool(r->core.device, r->descriptors.pool, NULL);
	vkDestroySampler(r->core.device, r->texture.sampler, NULL);
	vkDestroyImageView(r->core.device, r->texture.view, NULL);
	vkDestroyImage(r->core.device, r->texture.image, NULL);
	vkFreeMemory(r->core.device, r->texture.memory, NULL);

	vkDestroyPipeline(r->core.device, r->pipelines.compute_spherical, NULL);
	vkDestroyPipelineLayout(r->core.device, r->computePipelineLayout, NULL);
	vkDestroyDescriptorSetLayout(r->core.device, r->descriptors.compute_layout, NULL);
	vkDestroyPipeline(r->core.device, r->pipelines.ui, NULL);
	vkDestroyPipeline(r->core.device, r->pipelines.label, NULL);
	vkDestroyPipeline(r->core.device, r->pipelines.menu, NULL);
	vkDestroyPipeline(r->core.device, r->pipelines.textQuad, NULL);
	vkDestroyPipeline(r->core.device, r->pipelines.ray, NULL);
	vkDestroyPipeline(r->core.device, r->pipelines.edge, NULL);
	vkDestroyPipeline(r->core.device, r->pipelines.node, NULL);
	vkDestroyPipelineLayout(r->core.device, r->pipelineLayout, NULL);
	vkDestroyDescriptorSetLayout(r->core.device, r->descriptors.layout, NULL);

	vulkan_render_pass_destroy(&r->renderPass, r->core.device);
	if (r->renderPassXR != VK_NULL_HANDLE)
		vkDestroyRenderPass(r->core.device, r->renderPassXR, NULL);
	vulkan_swapchain_destroy(&r->swapchain, r->core.device);
	vulkan_commands_destroy(&r->commands, r->core.device);
	vulkan_device_destroy(&r->core);
}

void renderer_cleanup(Renderer *r)
{
	VK_CHECK(vkDeviceWaitIdle(r->core.device), "Failed to wait for device idle on cleanup");
	cleanup_uniform_buffers(r);
	cleanup_compute_context(r);
	renderer_cleanup_bcgl(r);
	cleanup_geometry_buffers(r);
	cleanup_menu_label_atlases(r);
	cleanup_xr_resources(r);
	cleanup_splc_pipelines_core(r);
}
