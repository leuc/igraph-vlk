#include "vulkan/renderer_lifecycle.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vulkan/app_path.h"
#include "vulkan/buffers.h"
#include "vulkan/images.h"
#include "vulkan/pipeline_compute.h"
#include "vulkan/pipeline_graphics.h"
#include "vulkan/pipeline_ui.h"
#include "vulkan/renderer_compute.h"
#include "vulkan/renderer_geometry.h"
#include "vulkan/renderer_pipelines.h"
#include "vulkan/swapchain.h"
#include "vulkan/text.h"
#include "vulkan/utils.h"

#define FONT_PATH "/usr/share/fonts/truetype/inconsolata/Inconsolata.otf"

FontAtlas globalAtlas;
static bool atlasLoaded = false;

bool renderer_init(Renderer *r, GLFWwindow *window, GraphData *graph, void *xr)
{
	r->window = window;
	r->nodeCount = graph->node_count;
	r->edgeCount = graph->edge_count;
	r->edgeVertexCount = 0;
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
	r->currentRoutingMode = ROUTING_MODE_SPHERICAL_PCB;
	glfwSetWindowTitle(window, "igraph-vlk");

	vulkan_device_create(&r->core, window, xr);
	vulkan_swapchain_create(&r->swapchain, &r->core, window);
	vulkan_render_pass_create(&r->renderPass, &r->core, &r->swapchain);
	vulkan_commands_create(&r->commands, &r->core, r->swapchain.imageCount);

	VkDescriptorSetLayoutBinding descriptorSetLayoutBindings[] = {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, NULL}, {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL}};
	VkDescriptorSetLayoutCreateInfo layoutInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 2, .pBindings = descriptorSetLayoutBindings};
	VK_CHECK(vkCreateDescriptorSetLayout(r->core.device, &layoutInfo, NULL, &r->descriptorSetLayout), "Failed to create descriptor set layout");

	VkPushConstantRange pushConstantRange = {.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, .offset = 0, .size = sizeof(mat4) * 2};
	VkPipelineLayoutCreateInfo pipelineLayoutInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1, .pSetLayouts = &r->descriptorSetLayout, .pushConstantRangeCount = 1, .pPushConstantRanges = &pushConstantRange};
	VK_CHECK(vkCreatePipelineLayout(r->core.device, &pipelineLayoutInfo, NULL, &r->pipelineLayout), "Failed to create pipeline layout");

	if (!atlasLoaded) {
		text_generate_atlas(app_path_resolve(FONT_PATH), &globalAtlas);
		atlasLoaded = true;
	}
	create_image(r->core.device, r->core.physicalDevice, globalAtlas.width, globalAtlas.height, VK_FORMAT_R8_UNORM, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &r->textureImage, &r->textureImageMemory);
	VkDeviceSize imgSize = globalAtlas.width * globalAtlas.height;
	VkBuffer stagingBuffer;
	VkDeviceMemory stagingBufferMemory;
	create_buffer(r->core.device, r->core.physicalDevice, imgSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &stagingBuffer, &stagingBufferMemory);
	void *dataPtr;
	VK_CHECK(vkMapMemory(r->core.device, stagingBufferMemory, 0, imgSize, 0, &dataPtr), "Failed to map texture staging buffer memory");
	memcpy(dataPtr, globalAtlas.atlasData, imgSize);
	vkUnmapMemory(r->core.device, stagingBufferMemory);
	transition_image_layout(r->core.device, r->commands.commandPool, r->core.graphicsQueue, r->textureImage, VK_FORMAT_R8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	VkCommandBuffer commandBuffer = begin_single_time_commands(r->core.device, r->commands.commandPool);
	VkBufferImageCopy bufferImageCopy = {.bufferOffset = 0, .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageExtent = {(uint32_t)globalAtlas.width, (uint32_t)globalAtlas.height, 1}};
	vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, r->textureImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bufferImageCopy);
	end_single_time_commands(r->core.device, r->commands.commandPool, r->core.graphicsQueue, commandBuffer);
	vkDestroyBuffer(r->core.device, stagingBuffer, NULL);
	vkFreeMemory(r->core.device, stagingBufferMemory, NULL);
	transition_image_layout(r->core.device, r->commands.commandPool, r->core.graphicsQueue, r->textureImage, VK_FORMAT_R8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	VkImageViewCreateInfo imageViewInfo = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = r->textureImage, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = VK_FORMAT_R8_UNORM, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
	VK_CHECK(vkCreateImageView(r->core.device, &imageViewInfo, NULL, &r->textureImageView), "Failed to create texture image view");
	VkSamplerCreateInfo samplerInfo = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, .magFilter = VK_FILTER_LINEAR, .minFilter = VK_FILTER_LINEAR, .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR, .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
	VK_CHECK(vkCreateSampler(r->core.device, &samplerInfo, NULL, &r->textureSampler), "Failed to create texture sampler");

	renderer_create_pipelines(r);

	for (int i = 0; i < PLATONIC_COUNT; i++) {
		Vertex *v;
		uint32_t vc;
		uint32_t *idx;
		polyhedron_generate_platonic(i, &v, &vc, &idx, &r->platonicIndexCounts[i]);
		create_buffer(r->core.device, r->core.physicalDevice, sizeof(Vertex) * vc, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->vertexBuffers[i], &r->vertexBufferMemories[i]);
		update_buffer(r->core.device, r->vertexBufferMemories[i], sizeof(Vertex) * vc, v);
		create_buffer(r->core.device, r->core.physicalDevice, sizeof(uint32_t) * r->platonicIndexCounts[i], VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->indexBuffers[i], &r->indexBufferMemories[i]);
		update_buffer(r->core.device, r->indexBufferMemories[i], sizeof(uint32_t) * r->platonicIndexCounts[i], idx);
		free(v);
		free(idx);
	}

	LabelVertex lvs[] = {{{0, 0, 0}, {0, 0}}, {{1, 0, 0}, {1, 0}}, {{0, 1, 0}, {0, 1}}, {{1, 1, 0}, {1, 1}}};
	create_buffer(r->core.device, r->core.physicalDevice, sizeof(lvs), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->labelVertexBuffer, &r->labelVertexBufferMemory);
	update_buffer(r->core.device, r->labelVertexBufferMemory, sizeof(lvs), lvs);

	// Shared quad vertex/index buffers for menus, text quads, and node labels
	QuadVertex qv[] = {{{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f}}, {{0.5f, -0.5f, 0.0f}, {1.0f, 0.0f}}, {{0.5f, 0.5f, 0.0f}, {1.0f, 1.0f}}, {{-0.5f, 0.5f, 0.0f}, {0.0f, 1.0f}}};
	uint32_t qi[] = {0, 1, 2, 2, 3, 0};
	create_buffer(r->core.device, r->core.physicalDevice, sizeof(qv), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->menuQuadVertexBuffer, &r->menuQuadVertexBufferMemory);
	update_buffer(r->core.device, r->menuQuadVertexBufferMemory, sizeof(qv), qv);
	create_buffer(r->core.device, r->core.physicalDevice, sizeof(qi), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->menuQuadIndexBuffer, &r->menuQuadIndexBufferMemory);
	update_buffer(r->core.device, r->menuQuadIndexBufferMemory, sizeof(qi), qi);
	r->menuQuadIndexCount = 6;

	UIVertex uiBg[] = {{{0, 0, 0}, {0, 0}}, {{1, 0, 0}, {1, 0}}, {{0, 1, 0}, {0, 1}}, {{1, 1, 0}, {1, 1}}};
	create_buffer(r->core.device, r->core.physicalDevice, sizeof(uiBg), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->uiBgVertexBuffer, &r->uiBgVertexBufferMemory);
	update_buffer(r->core.device, r->uiBgVertexBufferMemory, sizeof(uiBg), uiBg);
	create_buffer(r->core.device, r->core.physicalDevice, sizeof(UIInstance) * 1024, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->uiTextInstanceBuffer, &r->uiTextInstanceBufferMemory);

	UIInstance bgInst = {.color = {0, 0, 0, -1.0f}};
	create_buffer(r->core.device, r->core.physicalDevice, sizeof(UIInstance), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->uiBgInstanceBuffer, &r->uiBgInstanceBufferMemory);
	update_buffer(r->core.device, r->uiBgInstanceBufferMemory, sizeof(UIInstance), &bgInst);

	r->menuInstanceBuffer = VK_NULL_HANDLE;
	r->textQuadInstanceBuffer = VK_NULL_HANDLE;
	r->menuNodeCount = 0;
	r->textQuadInstanceCount = 0;

	r->nodePositionBuffer = VK_NULL_HANDLE;
	r->nodeAttributeBuffer = VK_NULL_HANDLE;
	r->nodeAttributeStagingBuffer = VK_NULL_HANDLE;
	r->nodeCapacity = 0;
	r->edgePositionBuffer = VK_NULL_HANDLE;
	r->edgeAttributeBuffer = VK_NULL_HANDLE;
	r->edgeAttributeStagingBuffer = VK_NULL_HANDLE;
	r->edgeCapacity = 0;
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

	renderer_update_graph(r, graph);
	r->labelTreeNeedsRebuild = true;

	r->uniformBuffers = malloc(sizeof(VkBuffer) * MAX_FRAMES_IN_FLIGHT * MAX_VIEWS);
	r->uniformBuffersMemory = malloc(sizeof(VkDeviceMemory) * MAX_FRAMES_IN_FLIGHT * MAX_VIEWS);
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT * MAX_VIEWS; i++) {
		create_buffer(r->core.device, r->core.physicalDevice, sizeof(UniformBufferObject), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->uniformBuffers[i], &r->uniformBuffersMemory[i]);
		VK_CHECK(vkMapMemory(r->core.device, r->uniformBuffersMemory[i], 0, sizeof(UniformBufferObject), 0, &r->uboMapped[i]), "Failed to map UBO memory");
	}

	VkDescriptorPoolSize descriptorPoolSizes[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MAX_FRAMES_IN_FLIGHT * MAX_VIEWS * 4}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_FRAMES_IN_FLIGHT * MAX_VIEWS * 4}};
	VkDescriptorPoolCreateInfo descriptorPoolInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .poolSizeCount = 2, .pPoolSizes = descriptorPoolSizes, .maxSets = MAX_FRAMES_IN_FLIGHT * MAX_VIEWS * 4};
	VK_CHECK(vkCreateDescriptorPool(r->core.device, &descriptorPoolInfo, NULL, &r->descriptorPool), "Failed to create descriptor pool");

	VkDescriptorSetLayout descriptorSetLayouts[MAX_FRAMES_IN_FLIGHT * MAX_VIEWS * 4];
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT * MAX_VIEWS * 4; i++)
		descriptorSetLayouts[i] = r->descriptorSetLayout;
	VkDescriptorSetAllocateInfo descriptorSetAllocInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = r->descriptorPool, .descriptorSetCount = MAX_FRAMES_IN_FLIGHT * MAX_VIEWS * 4, .pSetLayouts = descriptorSetLayouts};
	r->descriptorSets = malloc(sizeof(VkDescriptorSet) * MAX_FRAMES_IN_FLIGHT * MAX_VIEWS * 4);
	VK_CHECK(vkAllocateDescriptorSets(r->core.device, &descriptorSetAllocInfo, r->descriptorSets), "Failed to allocate descriptor sets");

	// Initialize text quad descriptor set pointers (set after atlas upload)
	r->textQuadDescriptorSets = &r->descriptorSets[MAX_FRAMES_IN_FLIGHT * MAX_VIEWS];
	r->nodeLabelDescSets = &r->descriptorSets[MAX_FRAMES_IN_FLIGHT * MAX_VIEWS * 2];
	r->detailCardDescSets = &r->descriptorSets[MAX_FRAMES_IN_FLIGHT * MAX_VIEWS * 3];

	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT * MAX_VIEWS; i++) {
		VkDescriptorBufferInfo bufferInfo = {r->uniformBuffers[i], 0, sizeof(UniformBufferObject)};
		VkDescriptorImageInfo imageInfo = {r->textureSampler, r->textureImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
		VkWriteDescriptorSet descriptorWrites[] = {{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, r->descriptorSets[i], 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, NULL, &bufferInfo, NULL}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, r->descriptorSets[i], 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imageInfo, NULL, NULL}};
		vkUpdateDescriptorSets(r->core.device, 2, descriptorWrites, 0, NULL);
	}

	// Initialize menu text atlas (will be populated on first menu buffer generation)
	if (!text_atlas_init(&r->menuTextAtlas, 2048, 512)) {
		fprintf(stderr, "Failed to initialize menu text atlas\n");
		return false;
	}

	// Initialize node label atlas and instance buffer
	if (!text_atlas_init(&r->nodeTextAtlas, 2048, 4096)) {
		fprintf(stderr, "Failed to initialize node label text atlas\n");
		return false;
	}
	r->nodeLabelInstanceBuffer = VK_NULL_HANDLE;
	r->nodeLabelInstanceBufferMemory = VK_NULL_HANDLE;
	r->nodeLabelInstanceCount = 0;
	r->nodeLabelCapacity = 0;
	memset(&r->labelTree, 0, sizeof(r->labelTree));
	r->labelTreeNeedsRebuild = true;

	// Initialize dedicated detail card atlas and single-instance buffer
	if (!text_atlas_init(&r->detailCardAtlas, 2048, 4096)) {
		fprintf(stderr, "Failed to initialize detail card text atlas\n");
		return false;
	}
	r->detailCardInstanceBuffer = VK_NULL_HANDLE;
	r->detailCardInstanceBufferMemory = VK_NULL_HANDLE;
	r->detailCardVisible = false;
	r->detailCardNode = -1;
	r->labelCacheValid = false;

	VkFenceCreateInfo fenceInfo = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT};
	r->graphUpdateRingIndex = 0;
	for (int i = 0; i < GRAPH_UPDATE_RING_SIZE; i++) {
		VK_CHECK(vkCreateFence(r->core.device, &fenceInfo, NULL, &r->graphUpdateFences[i]), "Failed to create graph update fence");
	}

	glm_mat4_identity(r->ubo.model);
	glm_mat4_identity(r->ubo.view);
	int w, h;
	glfwGetFramebufferSize(window, &w, &h);
	glm_perspective(glm_rad(45.0f), (float)w / (float)h, 0.1f, 1000.0f, r->ubo.proj);
	r->ubo.proj[1][1] *= -1;
	return true;
}

void renderer_recreate_swapchain(Renderer *r)
{
	int w = 0, h = 0;
	glfwGetFramebufferSize(r->window, &w, &h);
	while (w == 0 || h == 0) {
		glfwGetFramebufferSize(r->window, &w, &h);
		glfwWaitEvents();
	}

	// Wait only for in-flight frames to finish (not a full device idle)
	VK_CHECK(vkWaitForFences(r->core.device, MAX_FRAMES_IN_FLIGHT, r->commands.inFlightFences, VK_TRUE, UINT64_MAX), "Failed to wait for in-flight fences during swapchain recreation");

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
		VkFramebufferCreateInfo framebufferInfo = {
			.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.renderPass = r->renderPass.renderPass,
			.attachmentCount = 2,
			.pAttachments = attachmentViews,
			.width = r->swapchain.extent.width,
			.height = r->swapchain.extent.height,
			.layers = 1,
		};
		VK_CHECK(vkCreateFramebuffer(r->core.device, &framebufferInfo, NULL, &r->renderPass.framebuffers[i]), "Failed to create framebuffer");
	}

	// Recreate renderFinishedSemaphores for new image count
	r->commands.imageCount = r->swapchain.imageCount;
	r->commands.renderFinishedSemaphores = malloc(sizeof(VkSemaphore) * r->commands.imageCount);
	VkSemaphoreCreateInfo semaphoreInfo = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
	for (uint32_t i = 0; i < r->commands.imageCount; i++) {
		VK_CHECK(vkCreateSemaphore(r->core.device, &semaphoreInfo, NULL, &r->commands.renderFinishedSemaphores[i]), "Failed to create render finished semaphore");
	}

	// Update projection matrix for new aspect ratio
	float aspect = (float)r->swapchain.extent.width / (float)r->swapchain.extent.height;
	glm_perspective(glm_rad(45.0f), aspect, 0.1f, 1000.0f, r->ubo.proj);
	r->ubo.proj[1][1] *= -1;

	r->framebufferResized = false;
}

void renderer_cleanup(Renderer *r)
{
	VK_CHECK(vkDeviceWaitIdle(r->core.device), "Failed to wait for device idle on cleanup");
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT * MAX_VIEWS; i++) {
		vkDestroyBuffer(r->core.device, r->uniformBuffers[i], NULL);
		vkFreeMemory(r->core.device, r->uniformBuffersMemory[i], NULL);
	}
	for (int i = 0; i < GRAPH_UPDATE_RING_SIZE; i++)
		vkDestroyFence(r->core.device, r->graphUpdateFences[i], NULL);

	if (r->computeCtx.fence != VK_NULL_HANDLE)
		vkDestroyFence(r->core.device, r->computeCtx.fence, NULL);
	if (r->computeCtx.cmdBuf != VK_NULL_HANDLE)
		vkFreeCommandBuffers(r->core.device, r->computeCtx.cmdPool, 1, &r->computeCtx.cmdBuf);
	if (r->computeCtx.cmdPool != VK_NULL_HANDLE)
		vkDestroyCommandPool(r->core.device, r->computeCtx.cmdPool, NULL);
	if (r->computeCtx.pool != VK_NULL_HANDLE)
		vkDestroyDescriptorPool(r->core.device, r->computeCtx.pool, NULL);
	if (r->computeCtx.nodeBuf != VK_NULL_HANDLE) {
		vkDestroyBuffer(r->core.device, r->computeCtx.nodeBuf, NULL);
		vkFreeMemory(r->core.device, r->computeCtx.nodeMem, NULL);
	}
	if (r->computeCtx.edgeBuf != VK_NULL_HANDLE) {
		vkDestroyBuffer(r->core.device, r->computeCtx.edgeBuf, NULL);
		vkFreeMemory(r->core.device, r->computeCtx.edgeMem, NULL);
	}
	if (r->computeCtx.hubBuf != VK_NULL_HANDLE) {
		vkDestroyBuffer(r->core.device, r->computeCtx.hubBuf, NULL);
		vkFreeMemory(r->core.device, r->computeCtx.hubMem, NULL);
	}

	vkDestroyBuffer(r->core.device, r->labelVertexBuffer, NULL);
	vkFreeMemory(r->core.device, r->labelVertexBufferMemory, NULL);

	vkDestroyBuffer(r->core.device, r->edgePositionBuffer, NULL);
	vkFreeMemory(r->core.device, r->edgePositionMemory, NULL);
	vkDestroyBuffer(r->core.device, r->edgeAttributeBuffer, NULL);
	vkFreeMemory(r->core.device, r->edgeAttributeMemory, NULL);
	if (r->edgeAttributeStagingBuffer != VK_NULL_HANDLE) {
		vkDestroyBuffer(r->core.device, r->edgeAttributeStagingBuffer, NULL);
		vkFreeMemory(r->core.device, r->edgeAttributeStagingMemory, NULL);
	}

	vkDestroyBuffer(r->core.device, r->nodePositionBuffer, NULL);
	vkFreeMemory(r->core.device, r->nodePositionMemory, NULL);
	vkDestroyBuffer(r->core.device, r->nodeAttributeBuffer, NULL);
	vkFreeMemory(r->core.device, r->nodeAttributeMemory, NULL);
	if (r->nodeAttributeStagingBuffer != VK_NULL_HANDLE) {
		vkDestroyBuffer(r->core.device, r->nodeAttributeStagingBuffer, NULL);
		vkFreeMemory(r->core.device, r->nodeAttributeStagingMemory, NULL);
	}

	for (int i = 0; i < PLATONIC_COUNT; i++) {
		vkDestroyBuffer(r->core.device, r->vertexBuffers[i], NULL);
		vkFreeMemory(r->core.device, r->vertexBufferMemories[i], NULL);
		vkDestroyBuffer(r->core.device, r->indexBuffers[i], NULL);
		vkFreeMemory(r->core.device, r->indexBufferMemories[i], NULL);
	}
	if (r->rayVertexBuffer != VK_NULL_HANDLE) {
		vkDestroyBuffer(r->core.device, r->rayVertexBuffer, NULL);
		vkFreeMemory(r->core.device, r->rayVertexBufferMemory, NULL);
	}
	vkDestroyBuffer(r->core.device, r->uiBgVertexBuffer, NULL);
	vkFreeMemory(r->core.device, r->uiBgVertexBufferMemory, NULL);
	vkDestroyBuffer(r->core.device, r->uiTextInstanceBuffer, NULL);
	vkFreeMemory(r->core.device, r->uiTextInstanceBufferMemory, NULL);
	if (r->uiBgInstanceBuffer != VK_NULL_HANDLE) {
		vkDestroyBuffer(r->core.device, r->uiBgInstanceBuffer, NULL);
		vkFreeMemory(r->core.device, r->uiBgInstanceBufferMemory, NULL);
	}

	if (r->menuQuadVertexBuffer != VK_NULL_HANDLE) {
		vkDestroyBuffer(r->core.device, r->menuQuadVertexBuffer, NULL);
		vkFreeMemory(r->core.device, r->menuQuadVertexBufferMemory, NULL);
	}
	if (r->menuQuadIndexBuffer != VK_NULL_HANDLE) {
		vkDestroyBuffer(r->core.device, r->menuQuadIndexBuffer, NULL);
		vkFreeMemory(r->core.device, r->menuQuadIndexBufferMemory, NULL);
	}
	if (r->menuInstanceBuffer != VK_NULL_HANDLE) {
		vkDestroyBuffer(r->core.device, r->menuInstanceBuffer, NULL);
		vkFreeMemory(r->core.device, r->menuInstanceBufferMemory, NULL);
	}
	if (r->textQuadInstanceBuffer != VK_NULL_HANDLE) {
		vkDestroyBuffer(r->core.device, r->textQuadInstanceBuffer, NULL);
		vkFreeMemory(r->core.device, r->textQuadInstanceBufferMemory, NULL);
	}
	text_atlas_destroy(&r->menuTextAtlas, r->core.device);
	text_atlas_destroy(&r->nodeTextAtlas, r->core.device);
	text_atlas_destroy(&r->detailCardAtlas, r->core.device);
	igraph_bh_tree_destroy(&r->labelTree);
	if (r->nodeLabelInstanceBuffer != VK_NULL_HANDLE) {
		vkDestroyBuffer(r->core.device, r->nodeLabelInstanceBuffer, NULL);
		vkFreeMemory(r->core.device, r->nodeLabelInstanceBufferMemory, NULL);
	}
	if (r->detailCardInstanceBuffer != VK_NULL_HANDLE) {
		vkDestroyBuffer(r->core.device, r->detailCardInstanceBuffer, NULL);
		vkFreeMemory(r->core.device, r->detailCardInstanceBufferMemory, NULL);
	}
	if (r->xrFramebuffers) {
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
