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
		VK_DESTROY_BUFFER(r->core.device, r->uniformBuffers[i], r->uniformBuffersMemory[i]);
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
	VK_DESTROY_BUFFER(r->core.device, r->edgePositionBuffer, r->edgePositionMemory);
	VK_DESTROY_BUFFER(r->core.device, r->edgeAttributeBuffer, r->edgeAttributeMemory);
	VK_DESTROY_BUFFER(r->core.device, r->edgeAttributeStagingBuffer, r->edgeAttributeStagingMemory);
	VK_DESTROY_BUFFER(r->core.device, r->nodePositionBuffer, r->nodePositionMemory);
	VK_DESTROY_BUFFER(r->core.device, r->nodeAttributeBuffer, r->nodeAttributeMemory);
	VK_DESTROY_BUFFER(r->core.device, r->nodeAttributeStagingBuffer, r->nodeAttributeStagingMemory);
	VK_DESTROY_BUFFER(r->core.device, r->nodeVertexBuffer, r->nodeVertexBufferMemory);
	VK_DESTROY_BUFFER(r->core.device, r->rayVertexBuffer, r->rayVertexBufferMemory);
	VK_DESTROY_BUFFER(r->core.device, r->uiBgVertexBuffer, r->uiBgVertexBufferMemory);
	VK_DESTROY_BUFFER(r->core.device, r->uiTextInstanceBuffer, r->uiTextInstanceBufferMemory);
	VK_DESTROY_BUFFER(r->core.device, r->uiBgInstanceBuffer, r->uiBgInstanceBufferMemory);
}

void cleanup_menu_label_atlases(Renderer *r)
{
	VK_DESTROY_BUFFER(r->core.device, r->menuQuadVertexBuffer, r->menuQuadVertexBufferMemory);
	VK_DESTROY_BUFFER(r->core.device, r->menuQuadIndexBuffer, r->menuQuadIndexBufferMemory);
	VK_DESTROY_BUFFER(r->core.device, r->menuInstanceBuffer, r->menuInstanceBufferMemory);
	VK_DESTROY_BUFFER(r->core.device, r->textQuadInstanceBuffer, r->textQuadInstanceBufferMemory);
	text_atlas_destroy(&r->menuTextAtlas, r->core.device);
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
	VK_DESTROY_BUFFER(r->core.device, r->splc_nodes_buffer, r->splc_nodes_memory);
	VK_DESTROY_BUFFER(r->core.device, r->splc_edges_buffer, r->splc_edges_memory);
	VK_DESTROY_BUFFER(r->core.device, r->splc_traffic_buffer, r->splc_traffic_memory);
	VK_DESTROY_BUFFER(r->core.device, r->splc_level_buffer, r->splc_level_memory);
	VK_DESTROY_BUFFER(r->core.device, r->splc_max_buffer, r->splc_max_memory);
	if (r->splc_level_groups) {
		for (int i = 0; i < r->splc_num_levels; i++) {
			if (r->splc_level_groups[i])
				igraph_vector_int_destroy(r->splc_level_groups[i]);
			free(r->splc_level_groups[i]);
		}
		free(r->splc_level_groups);
	}
	if (r->splc_descriptor_pool != VK_NULL_HANDLE)
		vkDestroyDescriptorPool(r->core.device, r->splc_descriptor_pool, NULL);
	if (r->splc_compute_pipeline != VK_NULL_HANDLE)
		vkDestroyPipeline(r->core.device, r->splc_compute_pipeline, NULL);
	if (r->splc_compute_pipeline_layout != VK_NULL_HANDLE)
		vkDestroyPipelineLayout(r->core.device, r->splc_compute_pipeline_layout, NULL);
	if (r->splc_compute_descriptor_set_layout != VK_NULL_HANDLE)
		vkDestroyDescriptorSetLayout(r->core.device, r->splc_compute_descriptor_set_layout, NULL);

	vkDestroyDescriptorPool(r->core.device, r->descriptorPool, NULL);
	vkDestroySampler(r->core.device, r->textureSampler, NULL);
	vkDestroyImageView(r->core.device, r->textureImageView, NULL);
	vkDestroyImage(r->core.device, r->textureImage, NULL);
	vkFreeMemory(r->core.device, r->textureImageMemory, NULL);

	vkDestroyPipeline(r->core.device, r->computeSphericalPipeline, NULL);
	vkDestroyPipelineLayout(r->core.device, r->computePipelineLayout, NULL);
	vkDestroyDescriptorSetLayout(r->core.device, r->computeDescriptorSetLayout, NULL);
	vkDestroyPipeline(r->core.device, r->uiPipeline, NULL);
	vkDestroyPipeline(r->core.device, r->labelPipeline, NULL);
	vkDestroyPipeline(r->core.device, r->menuPipeline, NULL);
	vkDestroyPipeline(r->core.device, r->textQuadPipeline, NULL);
	vkDestroyPipeline(r->core.device, r->rayPipeline, NULL);
	vkDestroyPipeline(r->core.device, r->edgePipeline, NULL);
	vkDestroyPipeline(r->core.device, r->nodePipeline, NULL);
	vkDestroyPipelineLayout(r->core.device, r->pipelineLayout, NULL);
	vkDestroyDescriptorSetLayout(r->core.device, r->descriptorSetLayout, NULL);

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
