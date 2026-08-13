/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "vulkan/renderer_lifecycle.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "os/path.h"
#include "vulkan/buffers.h"
#include "vulkan/images.h"
#include "vulkan/menu.h"
#include "vulkan/pipeline_compute.h"
#include "vulkan/pipeline_graphics.h"
#include "vulkan/pipeline_ui.h"
#include "vulkan/renderer_anim.h"
#include "vulkan/renderer_geometry.h"
#include "vulkan/renderer_pipelines.h"
#include "vulkan/renderer_transition.h"
#include "vulkan/swapchain.h"
#include "vulkan/text.h"
#include "vulkan/utils.h"

FontAtlas globalAtlas;
static bool atlasLoaded = false;

void renderer_wait_frames_idle(Renderer *r)
{
	if (!r || !r->core.device)
		return;
	VK_CHECK(vkWaitForFences(r->core.device, MAX_FRAMES_IN_FLIGHT, r->commands.inFlightFences, VK_TRUE, UINT64_MAX), "Failed to wait for in-flight frames");
}

bool renderer_init(Renderer *r, GLFWwindow *window, GraphData *graph, void *xr)
{
	// BCGL zero-init — must come before any pipeline/buffer creation so we start clean
	memset(&r->bcgl_ctx, 0, sizeof(BCGLComputeContext));
	r->bcgl_ctx.node_buf = VK_NULL_HANDLE;
	r->bcgl_ctx.topo_nodes_buf = VK_NULL_HANDLE;
	r->bcgl_ctx.topo_edges_buf = VK_NULL_HANDLE;
	r->bcgl_ctx.pool = VK_NULL_HANDLE;
	r->bcgl_ctx.desc_set = VK_NULL_HANDLE;
	r->bcgl_ctx.pipeline = VK_NULL_HANDLE;
	r->bcgl_ctx.layout = VK_NULL_HANDLE;
	r->bcgl_ctx.desc_layout = VK_NULL_HANDLE;
	r->bcgl_ctx.fence = VK_NULL_HANDLE;
	r->bcgl_ctx.cmd_pool = VK_NULL_HANDLE;
	r->bcgl_ctx.cmd_buf = VK_NULL_HANDLE;

	r->window = window;
	r->node.count = graph->node_count;
	r->edge.count = graph->edge_count;
	r->edge.vertex_count = 0;
	r->showNodes = true;
	r->showEdges = true;
	r->showUI = true;
	r->layoutScale = 1.0f;
	r->xrFramebuffers = NULL;
	r->xrFramebufferImageCount = NULL;
	r->xr_view_count = 0;
	r->xrDepthImages = NULL;
	r->xrDepthImageMemories = NULL;
	r->xrDepthImageViews = NULL;
	r->renderPassXR = VK_NULL_HANDLE;
	r->xrFormat = VK_FORMAT_UNDEFINED;
	r->currentRoutingMode = ROUTING_MODE_STRAIGHT;
	glfwSetWindowTitle(window, "igraph-vlk");

	vulkan_device_create(&r->core, window, xr);
	vulkan_swapchain_create(&r->swapchain, &r->core, window);
	vulkan_render_pass_create(&r->renderPass, &r->core, &r->swapchain);
	vulkan_commands_create(&r->commands, &r->core, r->swapchain.imageCount);

	VkDescriptorSetLayoutBinding descriptorSetLayoutBindings[] = {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, NULL}, {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL}, {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, NULL}, {4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, NULL}, {5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, NULL}};
	VkDescriptorSetLayoutCreateInfo layoutInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 5, .pBindings = descriptorSetLayoutBindings};
	VK_CHECK(vkCreateDescriptorSetLayout(r->core.device, &layoutInfo, NULL, &r->descriptors.layout), "Failed to create descriptor set layout");

	VkPushConstantRange pushConstantRange = {.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, .offset = 0, .size = sizeof(mat4) * 2 + sizeof(float) + sizeof(uint32_t) + sizeof(float) * 2};
	VkPipelineLayoutCreateInfo pipelineLayoutInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1, .pSetLayouts = &r->descriptors.layout, .pushConstantRangeCount = 1, .pPushConstantRanges = &pushConstantRange};
	VK_CHECK(vkCreatePipelineLayout(r->core.device, &pipelineLayoutInfo, NULL, &r->pipelineLayout), "Failed to create pipeline layout");

	if (!atlasLoaded) {
		text_generate_atlas(os_find_monospace_font(), &globalAtlas);
		atlasLoaded = true;
	}
	create_image(r->core.device, r->core.physicalDevice, globalAtlas.width, globalAtlas.height, VK_FORMAT_R8_UNORM, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &r->texture.image, &r->texture.memory);
	VkDeviceSize imgSize = globalAtlas.width * globalAtlas.height;
	VkBuffer stagingBuffer;
	VkDeviceMemory stagingBufferMemory;
	VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, imgSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &stagingBuffer, &stagingBufferMemory);
	void *dataPtr;
	VK_CHECK(vkMapMemory(r->core.device, stagingBufferMemory, 0, imgSize, 0, &dataPtr), "Failed to map texture staging buffer memory");
	memcpy(dataPtr, globalAtlas.atlasData, imgSize);
	vkUnmapMemory(r->core.device, stagingBufferMemory);
	transition_image_layout(r->core.device, r->commands.commandPool, r->core.graphicsQueue, r->texture.image, VK_FORMAT_R8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	VkCommandBuffer commandBuffer = begin_single_time_commands(r->core.device, r->commands.commandPool);
	VkBufferImageCopy bufferImageCopy = {.bufferOffset = 0, .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageExtent = {(uint32_t)globalAtlas.width, (uint32_t)globalAtlas.height, 1}};
	vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, r->texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bufferImageCopy);
	end_single_time_commands(r->core.device, r->commands.commandPool, r->core.graphicsQueue, commandBuffer);
	VK_DESTROY_BUFFER(r->core.device, stagingBuffer, stagingBufferMemory);
	transition_image_layout(r->core.device, r->commands.commandPool, r->core.graphicsQueue, r->texture.image, VK_FORMAT_R8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	VK_CHECK(vkCreateImageView(r->core.device, &VK_IMAGE_VIEW_2D(r->texture.image, VK_FORMAT_R8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT), NULL, &r->texture.view), "Failed to create texture image view");
	VkSamplerCreateInfo samplerInfo = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, .magFilter = VK_FILTER_LINEAR, .minFilter = VK_FILTER_LINEAR, .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR, .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
	VK_CHECK(vkCreateSampler(r->core.device, &samplerInfo, NULL, &r->texture.sampler), "Failed to create texture sampler");

	renderer_create_pipelines(r);

	{
		// Single triangle covering [-1,1]x[-1,1] for SDF node shapes
		float tri[3][3] = {{-1.0f, -1.0f, 0.0f}, {3.0f, -1.0f, 0.0f}, {-1.0f, 3.0f, 0.0f}};
		VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, sizeof(tri), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &r->nodeVertexBuffer, &r->nodeVertexBufferMemory);
		update_buffer(r->core.device, r->nodeVertexBufferMemory, sizeof(tri), tri);
	}

	LabelVertex lvs[] = {{{0, 0, 0}, {0, 0}}, {{1, 0, 0}, {1, 0}}, {{0, 1, 0}, {0, 1}}, {{1, 1, 0}, {1, 1}}};
	VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, sizeof(lvs), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &r->labelVertexBuffer, &r->labelVertexBufferMemory);
	update_buffer(r->core.device, r->labelVertexBufferMemory, sizeof(lvs), lvs);

	QuadVertex qv[] = {{{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f}}, {{0.5f, -0.5f, 0.0f}, {1.0f, 0.0f}}, {{0.5f, 0.5f, 0.0f}, {1.0f, 1.0f}}, {{-0.5f, 0.5f, 0.0f}, {0.0f, 1.0f}}};
	uint32_t qi[] = {0, 1, 2, 2, 3, 0};
	VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, sizeof(qv), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &r->quad.vertex, &r->quad.vertex_memory);
	update_buffer(r->core.device, r->quad.vertex_memory, sizeof(qv), qv);
	VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, sizeof(qi), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, &r->quad.index, &r->quad.index_memory);
	update_buffer(r->core.device, r->quad.index_memory, sizeof(qi), qi);
	r->quad.index_count = 6;

	UIVertex uiBg[] = {{{0, 0, 0}, {0, 0}}, {{1, 0, 0}, {1, 0}}, {{0, 1, 0}, {0, 1}}, {{1, 1, 0}, {1, 1}}};
	VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, sizeof(uiBg), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &r->uiBgVertexBuffer, &r->uiBgVertexBufferMemory);
	update_buffer(r->core.device, r->uiBgVertexBufferMemory, sizeof(uiBg), uiBg);
	VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, sizeof(UIInstance) * 1024, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &r->uiTextInstanceBuffer, &r->uiTextInstanceBufferMemory);

	UIInstance bgInst = {.color = {0, 0, 0, -1.0f}};
	VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, sizeof(UIInstance), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &r->uiBgInstanceBuffer, &r->uiBgInstanceBufferMemory);
	update_buffer(r->core.device, r->uiBgInstanceBufferMemory, sizeof(UIInstance), &bgInst);

	r->node.position = VK_NULL_HANDLE;
	r->node.attribute = VK_NULL_HANDLE;
	r->node.staging = VK_NULL_HANDLE;
	r->node.capacity = 0;
	r->edge.position = VK_NULL_HANDLE;
	r->edge.attribute = VK_NULL_HANDLE;
	r->edge.staging = VK_NULL_HANDLE;
	r->edge.capacity = 0;
	r->needsAttributeUpload = VK_TRUE;

	r->computeCtx.nodeBuf = VK_NULL_HANDLE;
	r->computeCtx.nodeMem = VK_NULL_HANDLE;
	r->computeCtx.edgeBuf = VK_NULL_HANDLE;
	r->computeCtx.edgeMem = VK_NULL_HANDLE;
	r->computeCtx.hubBuf = VK_NULL_HANDLE;
	r->computeCtx.hubMem = VK_NULL_HANDLE;
	r->computeCtx.pool = VK_NULL_HANDLE;
	r->computeCtx.descSet = VK_NULL_HANDLE;
	r->computeCtx.cmdBuf = VK_NULL_HANDLE;
	r->computeCtx.cmdPool = VK_NULL_HANDLE;
	r->computeCtx.fence = VK_NULL_HANDLE;
	r->computeCtx.initialized = VK_FALSE;

	// Field-by-field rather than a memset: renderer_create_pipelines() above
	// has already filled in r->crit.pipeline_layout, which must survive.
	r->crit.out_nodes_buffer = VK_NULL_HANDLE;
	r->crit.out_nodes_memory = VK_NULL_HANDLE;
	r->crit.out_edges_buffer = VK_NULL_HANDLE;
	r->crit.out_edges_memory = VK_NULL_HANDLE;
	r->crit.in_nodes_buffer = VK_NULL_HANDLE;
	r->crit.in_nodes_memory = VK_NULL_HANDLE;
	r->crit.in_edges_buffer = VK_NULL_HANDLE;
	r->crit.in_edges_memory = VK_NULL_HANDLE;
	r->crit.level_buffer = VK_NULL_HANDLE;
	r->crit.level_memory = VK_NULL_HANDLE;
	r->crit.lnw_buffer = VK_NULL_HANDLE;
	r->crit.lnw_memory = VK_NULL_HANDLE;
	r->crit.lnx_buffer = VK_NULL_HANDLE;
	r->crit.lnx_memory = VK_NULL_HANDLE;
	r->crit.height_buffer = VK_NULL_HANDLE;
	r->crit.height_memory = VK_NULL_HANDLE;
	r->crit.depth_buffer = VK_NULL_HANDLE;
	r->crit.depth_memory = VK_NULL_HANDLE;
	r->crit.reachability_buffer = VK_NULL_HANDLE;
	r->crit.reachability_memory = VK_NULL_HANDLE;
	r->crit.result_buffer = VK_NULL_HANDLE;
	r->crit.result_memory = VK_NULL_HANDLE;
	r->crit.level_offsets = NULL;
	r->crit.level_sizes = NULL;
	r->crit.num_levels = 0;
	r->crit.node_count = 0;
	r->crit.weight_mode = CRIT_WEIGHT_SPLC;
	r->crit.active = false;
	r->crit.readback_pending = false;
	r->crit.current_level = 0;
	r->crit.stage = CRIT_STAGE_LNW;
	r->crit.last_level_time = 0.0;
	r->crit.level_interval = 0.0;
	r->crit.graph_edge_count = 0;

	r->graphUpdateRingIndex = 0;
	for (int i = 0; i < GRAPH_UPDATE_RING_SIZE; i++) {
		VK_CHECK(vkCreateFence(r->core.device, &VK_SIGNALED_FENCE_INFO, NULL, &r->graphUpdateFences[i]), "Failed to create graph update fence");
	}

	renderer_transition_init(r);
	renderer_update_graph(r, graph);
	r->label.tree_needs_rebuild = true;

	r->ubo.buffers = malloc(sizeof(VkBuffer) * MAX_FRAMES_IN_FLIGHT * MAX_VIEWS);
	r->ubo.memory = malloc(sizeof(VkDeviceMemory) * MAX_FRAMES_IN_FLIGHT * MAX_VIEWS);
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT * MAX_VIEWS; i++) {
		VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, sizeof(UniformBufferObject), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, &r->ubo.buffers[i], &r->ubo.memory[i]);
		VK_CHECK(vkMapMemory(r->core.device, r->ubo.memory[i], 0, sizeof(UniformBufferObject), 0, &r->ubo.mapped[i]), "Failed to map UBO memory");
	}

	VkDescriptorPoolSize descriptorPoolSizes[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MAX_FRAMES_IN_FLIGHT * MAX_VIEWS * 8}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_FRAMES_IN_FLIGHT * MAX_VIEWS * 4}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, MAX_FRAMES_IN_FLIGHT * MAX_VIEWS * 32}};
	VkDescriptorPoolCreateInfo descriptorPoolInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .poolSizeCount = 3, .pPoolSizes = descriptorPoolSizes, .maxSets = MAX_FRAMES_IN_FLIGHT * MAX_VIEWS * 4};
	VK_CHECK(vkCreateDescriptorPool(r->core.device, &descriptorPoolInfo, NULL, &r->descriptors.pool), "Failed to create descriptor pool");

	VkDescriptorSetLayout descriptorSetLayouts[MAX_FRAMES_IN_FLIGHT * MAX_VIEWS * 4];
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT * MAX_VIEWS * 4; i++)
		descriptorSetLayouts[i] = r->descriptors.layout;
	VkDescriptorSetAllocateInfo descriptorSetAllocInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = r->descriptors.pool, .descriptorSetCount = MAX_FRAMES_IN_FLIGHT * MAX_VIEWS * 4, .pSetLayouts = descriptorSetLayouts};
	r->descriptors.sets = malloc(sizeof(VkDescriptorSet) * MAX_FRAMES_IN_FLIGHT * MAX_VIEWS * 4);
	VK_CHECK(vkAllocateDescriptorSets(r->core.device, &descriptorSetAllocInfo, r->descriptors.sets), "Failed to allocate descriptor sets");

	// Initialize text quad descriptor set pointers (set after atlas upload)
	r->descriptors.text_quad_sets = &r->descriptors.sets[MAX_FRAMES_IN_FLIGHT * MAX_VIEWS];
	r->descriptors.node_label_sets = &r->descriptors.sets[MAX_FRAMES_IN_FLIGHT * MAX_VIEWS * 2];
	r->descriptors.detail_card_sets = &r->descriptors.sets[MAX_FRAMES_IN_FLIGHT * MAX_VIEWS * 3];

	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT * MAX_VIEWS; i++) {
		VkDescriptorBufferInfo bufferInfo = {r->ubo.buffers[i], 0, sizeof(UniformBufferObject)};
		VkDescriptorImageInfo imageInfo = {r->texture.sampler, r->texture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
		VkWriteDescriptorSet descriptorWrites[] = {VK_WRITE_DESC_BUFFER(r->descriptors.sets[i], 0, &bufferInfo, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER), VK_WRITE_DESC_IMAGE(r->descriptors.sets[i], 1, &imageInfo, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)};
		vkUpdateDescriptorSets(r->core.device, 2, descriptorWrites, 0, NULL);
	}

	renderer_anim_init(r);
	renderer_anim_clear(r, graph);

	if (!renderer_menu_init(r)) {
		return false;
	}

	// Initialize node label atlas and instance buffer
	if (!text_atlas_init(&r->label.atlas, 2048, 4096)) {
		fprintf(stderr, "Failed to initialize node label text atlas\n");
		return false;
	}
	r->label.instance = VK_NULL_HANDLE;
	r->label.instance_memory = VK_NULL_HANDLE;
	r->label.count = 0;
	r->label.capacity = 0;
	memset(&r->label.tree, 0, sizeof(r->label.tree));
	r->label.tree_needs_rebuild = true;

	// Initialize dedicated detail card atlas and single-instance buffer
	if (!text_atlas_init(&r->detail.atlas, 2048, 4096)) {
		fprintf(stderr, "Failed to initialize detail card text atlas\n");
		return false;
	}
	r->detail.instance = VK_NULL_HANDLE;
	r->detail.instance_memory = VK_NULL_HANDLE;
	r->detail.visible = false;
	r->detail.node = -1;
	r->label.cache_valid = false;

	glm_mat4_identity(r->ubo.data.model);
	glm_mat4_identity(r->ubo.data.view);
	int w, h;
	glfwGetFramebufferSize(window, &w, &h);
	glm_perspective(glm_rad(45.0f), (float)w / (float)h, 0.1f, 1000.0f, r->ubo.data.proj);
	r->ubo.data.proj[1][1] *= -1;
	return true;
}

bool renderer_recreate_swapchain(Renderer *r)
{
	int w = 0, h = 0;
	glfwGetFramebufferSize(r->window, &w, &h);
	while (w == 0 || h == 0) {
		glfwGetFramebufferSize(r->window, &w, &h);
		glfwWaitEvents();
	}

	// Ensure queue is idle before destroying per-image semaphores
	VkResult wait_res = vkDeviceWaitIdle(r->core.device);
	if (wait_res != VK_SUCCESS) {
		fprintf(stderr, "renderer_recreate_swapchain: vkDeviceWaitIdle failed\n");
		return false;
	}

	// Destroy old framebuffers (render pass survives — format-based, not extent-based)
	for (uint32_t i = 0; i < r->renderPass.imageCount; i++) {
		if (r->renderPass.framebuffers[i] != VK_NULL_HANDLE)
			vkDestroyFramebuffer(r->core.device, r->renderPass.framebuffers[i], NULL);
	}
	free(r->renderPass.framebuffers);
	r->renderPass.framebuffers = NULL;

	// Destroy old renderFinishedSemaphores (sized to old swapchain image count)
	for (uint32_t i = 0; i < r->commands.imageCount; i++) {
		if (r->commands.renderFinishedSemaphores[i] != VK_NULL_HANDLE)
			vkDestroySemaphore(r->core.device, r->commands.renderFinishedSemaphores[i], NULL);
	}
	free(r->commands.renderFinishedSemaphores);
	r->commands.renderFinishedSemaphores = NULL;

	// Recreate swapchain (passes old swapchain handle for driver optimization)
	vulkan_swapchain_recreate(&r->swapchain, &r->core, r->window);

	// Recreate framebuffers with new extent
	r->renderPass.imageCount = r->swapchain.imageCount;
	r->renderPass.framebuffers = malloc(sizeof(VkFramebuffer) * r->swapchain.imageCount);
	for (uint32_t i = 0; i < r->swapchain.imageCount; i++) {
		VkImageView attachmentViews[] = {r->swapchain.views[i], r->swapchain.depthView};
		VK_CHECK(vkCreateFramebuffer(r->core.device, &VK_FRAMEBUFFER_INFO(r->renderPass.renderPass, attachmentViews, r->swapchain.extent.width, r->swapchain.extent.height), NULL, &r->renderPass.framebuffers[i]), "Failed to create framebuffer");
	}

	// Recreate renderFinishedSemaphores for new image count
	r->commands.imageCount = r->swapchain.imageCount;
	r->commands.renderFinishedSemaphores = malloc(sizeof(VkSemaphore) * r->commands.imageCount);
	for (uint32_t i = 0; i < r->commands.imageCount; i++) {
		VK_CHECK(vkCreateSemaphore(r->core.device, &VK_SEMAPHORE_INFO, NULL, &r->commands.renderFinishedSemaphores[i]), "Failed to create render finished semaphore");
	}

	// Update projection matrix for new aspect ratio
	float aspect = (float)r->swapchain.extent.width / (float)r->swapchain.extent.height;
	glm_perspective(glm_rad(45.0f), aspect, 0.1f, 1000.0f, r->ubo.data.proj);
	r->ubo.data.proj[1][1] *= -1;

	r->framebufferResized = false;
	return true;
}
