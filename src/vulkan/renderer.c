#include "vulkan/renderer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "interaction/state.h"
#include "vulkan/pipeline_compute.h"
#include "vulkan/pipeline_graphics.h"
#include "vulkan/pipeline_ui.h"
#include "vulkan/renderer_compute.h"
#include "vulkan/renderer_geometry.h"
#include "vulkan/renderer_pipelines.h"
#include "vulkan/text.h"
#include "vulkan/utils.h"

#define FONT_PATH "/usr/share/fonts/truetype/inconsolata/Inconsolata.otf"

FontAtlas globalAtlas;
bool atlasLoaded = false;

int renderer_init(Renderer *r, GLFWwindow *window, GraphData *graph, XrContext *xr)
{
	r->window = window;
	r->nodeCount = graph->node_count;
	r->edgeCount = graph->edge_count;
	r->edgeVertexCount = 0;
	r->showLabels = true;
	r->showNodes = true;
	r->showEdges = true;
	r->showUI = true;
	r->showSpheres = true;
	r->layoutScale = 1.0f;
	r->numSpheres = 0;
	r->sphereIndexCounts = NULL;
	r->sphereIndexOffsets = NULL;
	r->xrFramebuffers = NULL;
	r->xrFramebufferImageCount = NULL;
	r->xr_view_count = 0;
	r->xrDepthImages = NULL;
	r->xrDepthImageMemories = NULL;
	r->xrDepthImageViews = NULL;
	r->renderPassXR = VK_NULL_HANDLE;
	r->xrFormat = VK_FORMAT_UNDEFINED;
	r->currentRoutingMode = ROUTING_MODE_SPHERICAL_PCB;
	r->sphereVertexBuffer = VK_NULL_HANDLE;
	r->sphereIndexBuffer = VK_NULL_HANDLE;

	glfwSetWindowTitle(window, "igraph-vlk");

	vulkan_device_create(&r->core, window, xr);
	vulkan_swapchain_create(&r->swapchain, &r->core, window);
	vulkan_render_pass_create(&r->renderPass, &r->core, &r->swapchain);
	vulkan_commands_create(&r->commands, &r->core, r->swapchain.imageCount);

	VkDescriptorSetLayoutBinding descriptorSetLayoutBindings[] = {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, NULL}, {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL}};
	VkDescriptorSetLayoutCreateInfo layoutInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 2, .pBindings = descriptorSetLayoutBindings};
	VK_CHECK(vkCreateDescriptorSetLayout(r->core.device, &layoutInfo, NULL, &r->descriptorSetLayout), "Failed to create descriptor set layout");

	VkPushConstantRange pushConstantRange = {.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, .offset = 0, .size = sizeof(float)};
	VkPipelineLayoutCreateInfo pipelineLayoutInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1, .pSetLayouts = &r->descriptorSetLayout, .pushConstantRangeCount = 1, .pPushConstantRanges = &pushConstantRange};
	VK_CHECK(vkCreatePipelineLayout(r->core.device, &pipelineLayoutInfo, NULL, &r->pipelineLayout), "Failed to create pipeline layout");

	if (!atlasLoaded) {
		text_generate_atlas(FONT_PATH, &globalAtlas);
		atlasLoaded = true;
	}
	createImage(r->core.device, r->core.physicalDevice, globalAtlas.width, globalAtlas.height, VK_FORMAT_R8_UNORM, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &r->textureImage, &r->textureImageMemory);
	VkDeviceSize imgSize = globalAtlas.width * globalAtlas.height;
	VkBuffer stagingBuffer;
	VkDeviceMemory stagingBufferMemory;
	createBuffer(r->core.device, r->core.physicalDevice, imgSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &stagingBuffer, &stagingBufferMemory);
	void *dataPtr;
	vkMapMemory(r->core.device, stagingBufferMemory, 0, imgSize, 0, &dataPtr);
	memcpy(dataPtr, globalAtlas.atlasData, imgSize);
	vkUnmapMemory(r->core.device, stagingBufferMemory);
	transitionImageLayout(r->core.device, r->commands.commandPool, r->core.graphicsQueue, r->textureImage, VK_FORMAT_R8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	VkCommandBufferAllocateInfo allocInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandPool = r->commands.commandPool, .commandBufferCount = 1};
	VkCommandBuffer commandBuffer;
	VK_CHECK(vkAllocateCommandBuffers(r->core.device, &allocInfo, &commandBuffer), "Failed to allocate command buffer for texture upload");
	VkCommandBufferBeginInfo beginInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
	vkBeginCommandBuffer(commandBuffer, &beginInfo);
	VkBufferImageCopy bufferImageCopy = {.bufferOffset = 0, .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageExtent = {(uint32_t)globalAtlas.width, (uint32_t)globalAtlas.height, 1}};
	vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, r->textureImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bufferImageCopy);
	vkEndCommandBuffer(commandBuffer);
	VkSubmitInfo submitInfo = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &commandBuffer};
	VK_CHECK(vkQueueSubmit(r->core.graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE), "Failed to submit texture upload command buffer");
	vkQueueWaitIdle(r->core.graphicsQueue);
	vkFreeCommandBuffers(r->core.device, r->commands.commandPool, 1, &commandBuffer);
	vkDestroyBuffer(r->core.device, stagingBuffer, NULL);
	vkFreeMemory(r->core.device, stagingBufferMemory, NULL);
	transitionImageLayout(r->core.device, r->commands.commandPool, r->core.graphicsQueue, r->textureImage, VK_FORMAT_R8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	VkImageViewCreateInfo imageViewInfo = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = r->textureImage, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = VK_FORMAT_R8_UNORM, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
	vkCreateImageView(r->core.device, &imageViewInfo, NULL, &r->textureImageView);
	VkSamplerCreateInfo samplerInfo = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, .magFilter = VK_FILTER_LINEAR, .minFilter = VK_FILTER_LINEAR, .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR, .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
	vkCreateSampler(r->core.device, &samplerInfo, NULL, &r->textureSampler);

	renderer_create_pipelines(r);

	for (int i = 0; i < PLATONIC_COUNT; i++) {
		Vertex *v;
		uint32_t vc;
		uint32_t *idx;
		polyhedron_generate_platonic(i, &v, &vc, &idx, &r->platonicIndexCounts[i]);
		createBuffer(r->core.device, r->core.physicalDevice, sizeof(Vertex) * vc, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->vertexBuffers[i], &r->vertexBufferMemories[i]);
		updateBuffer(r->core.device, r->vertexBufferMemories[i], sizeof(Vertex) * vc, v);
		createBuffer(r->core.device, r->core.physicalDevice, sizeof(uint32_t) * r->platonicIndexCounts[i], VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->indexBuffers[i], &r->indexBufferMemories[i]);
		updateBuffer(r->core.device, r->indexBufferMemories[i], sizeof(uint32_t) * r->platonicIndexCounts[i], idx);
		free(v);
		free(idx);
	}

	LabelVertex lvs[] = {{{0, 0, 0}, {0, 0}}, {{1, 0, 0}, {1, 0}}, {{0, 1, 0}, {0, 1}}, {{1, 1, 0}, {1, 1}}};
	createBuffer(r->core.device, r->core.physicalDevice, sizeof(lvs), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->labelVertexBuffer, &r->labelVertexBufferMemory);
	updateBuffer(r->core.device, r->labelVertexBufferMemory, sizeof(lvs), lvs);

	UIVertex uiBg[] = {{{0, 0, 0}, {0, 0}}, {{1, 0, 0}, {1, 0}}, {{0, 1, 0}, {0, 1}}, {{1, 1, 0}, {1, 1}}};
	createBuffer(r->core.device, r->core.physicalDevice, sizeof(uiBg), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->uiBgVertexBuffer, &r->uiBgVertexBufferMemory);
	updateBuffer(r->core.device, r->uiBgVertexBufferMemory, sizeof(uiBg), uiBg);
	createBuffer(r->core.device, r->core.physicalDevice, sizeof(UIInstance) * 1024, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->uiTextInstanceBuffer, &r->uiTextInstanceBufferMemory);

	UIInstance bgInst = {.color = {0, 0, 0, -1.0f}};
	createBuffer(r->core.device, r->core.physicalDevice, sizeof(UIInstance), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->uiBgInstanceBuffer, &r->uiBgInstanceBufferMemory);
	updateBuffer(r->core.device, r->uiBgInstanceBufferMemory, sizeof(UIInstance), &bgInst);

	r->menuQuadVertexBuffer = VK_NULL_HANDLE;
	r->menuQuadIndexBuffer = VK_NULL_HANDLE;
	r->menuInstanceBuffer = VK_NULL_HANDLE;
	r->menuTextInstanceBuffer = VK_NULL_HANDLE;
	r->menuNodeCount = 0;
	r->menuQuadIndexCount = 0;

	QuadVertex numV[] = {{{-0.5f, -0.02f, 0}, {0, 0}}, {{0.5f, -0.02f, 0}, {1, 0}}, {{-0.5f, 0.02f, 0}, {0, 1}}, {{0.5f, 0.02f, 0}, {1, 1}}, {{-0.03f, -0.05f, 0}, {0, 0}}, {{0.03f, -0.05f, 0}, {1, 0}}, {{-0.03f, 0.05f, 0}, {0, 1}}, {{0.03f, 0.05f, 0}, {1, 1}}};
	uint32_t numI[] = {0, 1, 2, 2, 1, 3, 4, 5, 6, 6, 5, 7};
	createBuffer(r->core.device, r->core.physicalDevice, sizeof(numV), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->numericQuadVertexBuffer, &r->numericQuadVertexBufferMemory);
	updateBuffer(r->core.device, r->numericQuadVertexBufferMemory, sizeof(numV), numV);
	createBuffer(r->core.device, r->core.physicalDevice, sizeof(numI), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->numericQuadIndexBuffer, &r->numericQuadIndexBufferMemory);
	updateBuffer(r->core.device, r->numericQuadIndexBufferMemory, sizeof(numI), numI);
	r->numericQuadIndexCount = 12;
	r->numericInstanceBuffer = VK_NULL_HANDLE;
	r->numericInstanceCount = 0;

	r->nodePositionBuffer = VK_NULL_HANDLE;
	r->nodeAttributeBuffer = VK_NULL_HANDLE;
	r->nodeAttributeStagingBuffer = VK_NULL_HANDLE;
	r->nodeCapacity = 0;
	r->edgePositionBuffer = VK_NULL_HANDLE;
	r->edgeAttributeBuffer = VK_NULL_HANDLE;
	r->edgeAttributeStagingBuffer = VK_NULL_HANDLE;
	r->edgeCapacity = 0;
	r->needsAttributeUpload = VK_TRUE;

	renderer_update_graph(r, graph);

	r->uniformBuffers = malloc(sizeof(VkBuffer) * MAX_FRAMES_IN_FLIGHT * MAX_VIEWS);
	r->uniformBuffersMemory = malloc(sizeof(VkDeviceMemory) * MAX_FRAMES_IN_FLIGHT * MAX_VIEWS);
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT * MAX_VIEWS; i++) {
		createBuffer(r->core.device, r->core.physicalDevice, sizeof(UniformBufferObject), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->uniformBuffers[i], &r->uniformBuffersMemory[i]);
		vkMapMemory(r->core.device, r->uniformBuffersMemory[i], 0, sizeof(UniformBufferObject), 0, &r->uboMapped[i]);
	}

	VkDescriptorPoolSize descriptorPoolSizes[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MAX_FRAMES_IN_FLIGHT * MAX_VIEWS}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_FRAMES_IN_FLIGHT * MAX_VIEWS}};
	VkDescriptorPoolCreateInfo descriptorPoolInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .poolSizeCount = 2, .pPoolSizes = descriptorPoolSizes, .maxSets = MAX_FRAMES_IN_FLIGHT * MAX_VIEWS};
	VK_CHECK(vkCreateDescriptorPool(r->core.device, &descriptorPoolInfo, NULL, &r->descriptorPool), "Failed to create descriptor pool");

	VkDescriptorSetLayout descriptorSetLayouts[MAX_FRAMES_IN_FLIGHT * MAX_VIEWS];
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT * MAX_VIEWS; i++)
		descriptorSetLayouts[i] = r->descriptorSetLayout;
	VkDescriptorSetAllocateInfo descriptorSetAllocInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = r->descriptorPool, .descriptorSetCount = MAX_FRAMES_IN_FLIGHT * MAX_VIEWS, .pSetLayouts = descriptorSetLayouts};
	r->descriptorSets = malloc(sizeof(VkDescriptorSet) * MAX_FRAMES_IN_FLIGHT * MAX_VIEWS);
	VK_CHECK(vkAllocateDescriptorSets(r->core.device, &descriptorSetAllocInfo, r->descriptorSets), "Failed to allocate descriptor sets");

	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT * MAX_VIEWS; i++) {
		VkDescriptorBufferInfo bufferInfo = {r->uniformBuffers[i], 0, sizeof(UniformBufferObject)};
		VkDescriptorImageInfo imageInfo = {r->textureSampler, r->textureImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
		VkWriteDescriptorSet descriptorWrites[] = {{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, r->descriptorSets[i], 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, NULL, &bufferInfo, NULL}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, r->descriptorSets[i], 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imageInfo, NULL, NULL}};
		vkUpdateDescriptorSets(r->core.device, 2, descriptorWrites, 0, NULL);
	}

	VkFenceCreateInfo fenceInfo = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT};
	r->graphUpdateRingIndex = 0;
	for (int i = 0; i < GRAPH_UPDATE_RING_SIZE; i++) {
		VK_CHECK(vkCreateFence(r->core.device, &fenceInfo, NULL, &r->graphUpdateFences[i]), "Failed to create graph update fence");
	}

	r->computeCtx.initialized = VK_FALSE;
	glm_mat4_identity(r->ubo.model);
	glm_mat4_identity(r->ubo.view);
	int w, h;
	glfwGetFramebufferSize(window, &w, &h);
	glm_perspective(glm_rad(45.0f), (float)w / (float)h, 0.1f, 1000.0f, r->ubo.proj);
	r->ubo.proj[1][1] *= -1;
	return 0;
}

void renderer_update_view(Renderer *r, vec3 pos, vec3 front, vec3 up)
{
	vec3 c;
	glm_vec3_add(pos, front, c);
	glm_lookat(pos, c, up, r->ubo.view);
}

void renderer_setup_xr(Renderer *r, XrContext *xr)
{
	r->xr_view_count = xr->view_count;
	r->xrFormat = xr->swapchain_format;
	r->xrFramebuffers = malloc(sizeof(VkFramebuffer *) * xr->view_count);
	r->xrFramebufferImageCount = malloc(sizeof(uint32_t) * xr->view_count);

	VkRenderPass xrRenderPass = r->renderPass.renderPass;
	if (xr->swapchain_format != r->swapchain.imageFormat) {
		VkAttachmentDescription cAttXR = {.format = xr->swapchain_format, .samples = VK_SAMPLE_COUNT_1_BIT, .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE, .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
		VkAttachmentDescription dAttXR = {.format = VK_FORMAT_D32_SFLOAT, .samples = VK_SAMPLE_COUNT_1_BIT, .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
		VkAttachmentReference cAttRefXR = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
		VkAttachmentReference dAttRefXR = {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
		VkSubpassDescription subXR = {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, .colorAttachmentCount = 1, .pColorAttachments = &cAttRefXR, .pDepthStencilAttachment = &dAttRefXR};
		VkAttachmentDescription attsXR[] = {cAttXR, dAttXR};
		VkRenderPassCreateInfo rpInfoXR = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, .attachmentCount = 2, .pAttachments = attsXR, .subpassCount = 1, .pSubpasses = &subXR};
		vkCreateRenderPass(r->core.device, &rpInfoXR, NULL, &r->renderPassXR);
		xrRenderPass = r->renderPassXR;
	}

	r->xrDepthImages = malloc(sizeof(VkImage) * xr->view_count);
	r->xrDepthImageMemories = malloc(sizeof(VkDeviceMemory) * xr->view_count);
	r->xrDepthImageViews = malloc(sizeof(VkImageView) * xr->view_count);

	for (uint32_t i = 0; i < xr->view_count; i++) {
		r->xrFramebufferImageCount[i] = xr->swapchains[i].image_count;
		r->xrFramebuffers[i] = malloc(sizeof(VkFramebuffer) * xr->swapchains[i].image_count);
		xr->swapchains[i].image_views = malloc(sizeof(VkImageView) * xr->swapchains[i].image_count);

		createImage(r->core.device, r->core.physicalDevice, xr->swapchains[i].width, xr->swapchains[i].height, r->swapchain.depthFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &r->xrDepthImages[i], &r->xrDepthImageMemories[i]);
		VkImageViewCreateInfo depthViewInfo = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = r->xrDepthImages[i], .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = r->swapchain.depthFormat, .subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1}};
		vkCreateImageView(r->core.device, &depthViewInfo, NULL, &r->xrDepthImageViews[i]);
		transitionImageLayout(r->core.device, r->commands.commandPool, r->core.graphicsQueue, r->xrDepthImages[i], r->swapchain.depthFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

		for (uint32_t j = 0; j < xr->swapchains[i].image_count; j++) {
			VkImageViewCreateInfo ivInfo = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = xr->swapchains[i].images[j], .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = xr->swapchain_format, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
			vkCreateImageView(r->core.device, &ivInfo, NULL, &xr->swapchains[i].image_views[j]);
			VkImageView attachments[] = {xr->swapchains[i].image_views[j], r->xrDepthImageViews[i]};
			VkFramebufferCreateInfo fbInfo = {.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, .renderPass = xrRenderPass, .attachmentCount = 2, .pAttachments = attachments, .width = xr->swapchains[i].width, .height = xr->swapchains[i].height, .layers = 1};
			vkCreateFramebuffer(r->core.device, &fbInfo, NULL, &r->xrFramebuffers[i][j]);
		}
	}
}

void renderer_render_scene(Renderer *r, VkCommandBuffer cmd, VkRenderPass rp, VkFramebuffer fb, VkExtent2D extent, mat4 view, mat4 proj, uint32_t view_index, bool has_ray, vec3 ray_origin, vec3 ray_dir)
{
	uint32_t ubo_idx = r->commands.currentFrame * MAX_VIEWS + view_index;
	UniformBufferObject eye_ubo = r->ubo;
	glm_mat4_copy(view, eye_ubo.view);
	glm_mat4_copy(proj, eye_ubo.proj);
	memcpy(r->uboMapped[ubo_idx], &eye_ubo, sizeof(UniformBufferObject));

	VkClearValue cv[2];
	cv[0].color = (VkClearColorValue){0.01f, 0.01f, 0.02f, 1.0f};
	cv[1].depthStencil = (VkClearDepthStencilValue){1.0f, 0};
	VkRenderPassBeginInfo rpi = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, .renderPass = rp, .framebuffer = fb, .renderArea = {{0, 0}, {extent.width, extent.height}}, .clearValueCount = 2, .pClearValues = cv};
	vkCmdBeginRenderPass(cmd, &rpi, VK_SUBPASS_CONTENTS_INLINE);

	VkViewport vp = {0.0f, 0.0f, (float)extent.width, (float)extent.height, 0.0f, 1.0f};
	vkCmdSetViewport(cmd, 0, 1, &vp);
	VkRect2D sc = {{0, 0}, {extent.width, extent.height}};
	vkCmdSetScissor(cmd, 0, 1, &sc);

	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipelineLayout, 0, 1, &r->descriptorSets[ubo_idx], 0, NULL);

	if (r->showEdges && r->edgeCount > 0) {
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->edgePipeline);
		VkBuffer eBs[] = {r->edgePositionBuffer, r->edgeAttributeBuffer};
		VkDeviceSize eOs[] = {0, 0};
		vkCmdBindVertexBuffers(cmd, 0, 2, eBs, eOs);
		vkCmdDraw(cmd, r->edgeVertexCount, 1, 0, 0);
	}
	if (r->showNodes && r->nodeCount > 0) {
		float a = 1.0f;
		vkCmdPushConstants(cmd, r->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 4, &a);
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->graphicsPipeline);
		for (int i = 0; i < PLATONIC_COUNT; i++) {
			if (r->platonicDrawCalls[i].count == 0)
				continue;
			VkBuffer vbs[] = {r->vertexBuffers[i], r->nodePositionBuffer, r->nodeAttributeBuffer};
			VkDeviceSize vos[] = {0, 0, 0};
			vkCmdBindVertexBuffers(cmd, 0, 3, vbs, vos);
			vkCmdBindIndexBuffer(cmd, r->indexBuffers[i], 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(cmd, r->platonicIndexCounts[i], r->platonicDrawCalls[i].count, 0, 0, r->platonicDrawCalls[i].firstInstance);
		}
	}
	if (r->showLabels && r->labelCharCount > 0 && r->labelInstanceBuffer != VK_NULL_HANDLE) {
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->labelPipeline);
		VkBuffer lbs[] = {r->labelVertexBuffer, r->labelInstanceBuffer};
		VkDeviceSize los[] = {0, 0};
		vkCmdBindVertexBuffers(cmd, 0, 2, lbs, los);
		vkCmdDraw(cmd, 4, r->labelCharCount, 0, 0);
	}
	if (r->menuNodeCount > 0 && r->menuInstanceBuffer != VK_NULL_HANDLE) {
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->menuPipeline);
		VkBuffer mVs[] = {r->menuQuadVertexBuffer, r->menuInstanceBuffer};
		VkDeviceSize mOs[] = {0, 0};
		vkCmdBindVertexBuffers(cmd, 0, 2, mVs, mOs);
		vkCmdBindIndexBuffer(cmd, r->menuQuadIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
		vkCmdDrawIndexed(cmd, r->menuQuadIndexCount, r->menuNodeCount, 0, 0, 0);
		if (r->menuTextCharCount > 0 && r->menuTextInstanceBuffer != VK_NULL_HANDLE) {
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->labelPipeline);
			VkBuffer mTs[] = {r->labelVertexBuffer, r->menuTextInstanceBuffer};
			VkDeviceSize mTOs[] = {0, 0};
			vkCmdBindVertexBuffers(cmd, 0, 2, mTs, mTOs);
			vkCmdDraw(cmd, 4, r->menuTextCharCount, 0, 0);
		}
	}
	if (r->numericInstanceCount > 0 && r->numericInstanceBuffer != VK_NULL_HANDLE) {
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->menuPipeline);
		VkBuffer nVs[] = {r->numericQuadVertexBuffer, r->numericInstanceBuffer};
		VkDeviceSize nOs[] = {0, 0};
		vkCmdBindVertexBuffers(cmd, 0, 2, nVs, nOs);
		vkCmdBindIndexBuffer(cmd, r->numericQuadIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
		vkCmdDrawIndexed(cmd, r->numericQuadIndexCount, r->numericInstanceCount, 0, 0, 0);
	}
	if (r->showSpheres && r->numSpheres > 0 && r->sphereVertexBuffer != VK_NULL_HANDLE) {
		float as = fmaxf(0.02f, 0.2f / (float)r->numSpheres);
		vkCmdPushConstants(cmd, r->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 4, &as);
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->spherePipeline);
		VkBuffer svbs[] = {r->sphereVertexBuffer};
		VkDeviceSize svos[] = {0};
		vkCmdBindVertexBuffers(cmd, 0, 1, svbs, svos);
		vkCmdBindIndexBuffer(cmd, r->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
		for (int s = 0; s < r->numSpheres; s++)
			vkCmdDrawIndexed(cmd, r->sphereIndexCounts[s], 1, r->sphereIndexOffsets[s], 0, 0);
	}
	if (r->showUI) {
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->uiPipeline);
		VkBuffer bVs[] = {r->uiBgVertexBuffer, r->uiBgInstanceBuffer};
		VkDeviceSize bOs[] = {0, 0};
		vkCmdBindVertexBuffers(cmd, 0, 2, bVs, bOs);
		vkCmdDraw(cmd, 4, 1, 0, 0);
		if (r->uiTextCharCount > 0) {
			VkBuffer tVs[] = {r->labelVertexBuffer, r->uiTextInstanceBuffer};
			VkDeviceSize tOs[] = {0, 0};
			vkCmdBindVertexBuffers(cmd, 0, 2, tVs, tOs);
			vkCmdDraw(cmd, 4, r->uiTextCharCount, 0, 0);
		}
	}
	if (has_ray)
		renderer_render_ray(r, cmd, ray_origin, ray_dir, view, proj);
	vkCmdEndRenderPass(cmd);
}

void renderer_draw_frame(Renderer *r)
{
	vkWaitForFences(r->core.device, 1, &r->commands.inFlightFences[r->commands.currentFrame], VK_TRUE, UINT64_MAX);
	uint32_t imageIndex;
	VkResult res = vkAcquireNextImageKHR(r->core.device, r->swapchain.swapchain, UINT64_MAX, r->commands.imageAvailableSemaphores[r->commands.currentFrame], VK_NULL_HANDLE, &imageIndex);
	if (res == VK_ERROR_OUT_OF_DATE_KHR)
		return;

	vkResetFences(r->core.device, 1, &r->commands.inFlightFences[r->commands.currentFrame]);
	vkResetCommandBuffer(r->commands.commandBuffers[r->commands.currentFrame], 0);
	VkCommandBufferBeginInfo beginInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
	vkBeginCommandBuffer(r->commands.commandBuffers[r->commands.currentFrame], &beginInfo);
	renderer_render_scene(r, r->commands.commandBuffers[r->commands.currentFrame], r->renderPass.renderPass, r->renderPass.framebuffers[imageIndex], r->swapchain.extent, r->ubo.view, r->ubo.proj, 0, false, (vec3){0}, (vec3){0});
	vkEndCommandBuffer(r->commands.commandBuffers[r->commands.currentFrame]);

	VkPipelineStageFlags waitStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkSubmitInfo submitInfo = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .waitSemaphoreCount = 1, .pWaitSemaphores = &r->commands.imageAvailableSemaphores[r->commands.currentFrame], .pWaitDstStageMask = &waitStages, .commandBufferCount = 1, .pCommandBuffers = &r->commands.commandBuffers[r->commands.currentFrame], .signalSemaphoreCount = 1, .pSignalSemaphores = &r->commands.renderFinishedSemaphores[r->commands.currentFrame]};
	vkQueueSubmit(r->core.graphicsQueue, 1, &submitInfo, r->commands.inFlightFences[r->commands.currentFrame]);

	VkPresentInfoKHR presentInfo = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, .waitSemaphoreCount = 1, .pWaitSemaphores = &r->commands.renderFinishedSemaphores[r->commands.currentFrame], .swapchainCount = 1, .pSwapchains = &r->swapchain.swapchain, .pImageIndices = &imageIndex};
	vkQueuePresentKHR(r->core.presentQueue, &presentInfo);
	r->commands.currentFrame = (r->commands.currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void renderer_cleanup(Renderer *r)
{
	vkDeviceWaitIdle(r->core.device);
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT * MAX_VIEWS; i++) {
		vkDestroyBuffer(r->core.device, r->uniformBuffers[i], NULL);
		vkFreeMemory(r->core.device, r->uniformBuffersMemory[i], NULL);
	}
	for (int i = 0; i < GRAPH_UPDATE_RING_SIZE; i++)
		vkDestroyFence(r->core.device, r->graphUpdateFences[i], NULL);

	if (r->computeCtx.initialized) {
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
	}

	if (r->labelInstanceBuffer != VK_NULL_HANDLE) {
		vkDestroyBuffer(r->core.device, r->labelInstanceBuffer, NULL);
		vkFreeMemory(r->core.device, r->labelInstanceBufferMemory, NULL);
	}
	if (r->labelStagingBuffer != VK_NULL_HANDLE) {
		vkDestroyBuffer(r->core.device, r->labelStagingBuffer, NULL);
		vkFreeMemory(r->core.device, r->labelStagingBufferMemory, NULL);
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
	if (r->menuTextInstanceBuffer != VK_NULL_HANDLE) {
		vkDestroyBuffer(r->core.device, r->menuTextInstanceBuffer, NULL);
		vkFreeMemory(r->core.device, r->menuTextInstanceBufferMemory, NULL);
	}
	if (r->numericQuadVertexBuffer != VK_NULL_HANDLE) {
		vkDestroyBuffer(r->core.device, r->numericQuadVertexBuffer, NULL);
		vkFreeMemory(r->core.device, r->numericQuadVertexBufferMemory, NULL);
	}
	if (r->numericQuadIndexBuffer != VK_NULL_HANDLE) {
		vkDestroyBuffer(r->core.device, r->numericQuadIndexBuffer, NULL);
		vkFreeMemory(r->core.device, r->numericQuadIndexBufferMemory, NULL);
	}
	if (r->numericInstanceBuffer != VK_NULL_HANDLE) {
		vkDestroyBuffer(r->core.device, r->numericInstanceBuffer, NULL);
		vkFreeMemory(r->core.device, r->numericInstanceBufferMemory, NULL);
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
	vkDestroyPipeline(r->core.device, r->edgePipeline, NULL);
	vkDestroyPipeline(r->core.device, r->graphicsPipeline, NULL);
	vkDestroyPipelineLayout(r->core.device, r->pipelineLayout, NULL);
	vkDestroyDescriptorSetLayout(r->core.device, r->descriptorSetLayout, NULL);

	vulkan_render_pass_destroy(&r->renderPass, r->core.device);
	if (r->renderPassXR != VK_NULL_HANDLE)
		vkDestroyRenderPass(r->core.device, r->renderPassXR, NULL);
	vulkan_swapchain_destroy(&r->swapchain, r->core.device);
	vulkan_commands_destroy(&r->commands, r->core.device);
	vulkan_device_destroy(&r->core);
}
