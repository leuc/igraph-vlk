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
#include "vulkan/rt_layout.h"
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

	VkDescriptorSetLayoutBinding descriptorSetLayoutBindings[] = {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, NULL}, {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL}, {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, NULL}, {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, NULL}};
	VkDescriptorBindingFlags bindingFlags[] = {0, 0, VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT, VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT};
	VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO, .bindingCount = 4, .pBindingFlags = bindingFlags};
	VkDescriptorSetLayoutCreateInfo layoutInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .pNext = &bindingFlagsInfo, .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT, .bindingCount = 4, .pBindings = descriptorSetLayoutBindings};
	VK_CHECK(vkCreateDescriptorSetLayout(r->core.device, &layoutInfo, NULL, &r->descriptorSetLayout), "Failed to create descriptor set layout");

	VkPushConstantRange pushConstantRange = {.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, .offset = 0, .size = sizeof(mat4) * 2 + sizeof(float) + sizeof(uint32_t) + sizeof(float) * 2};
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
	VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, imgSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &stagingBuffer, &stagingBufferMemory);
	void *dataPtr;
	VK_CHECK(vkMapMemory(r->core.device, stagingBufferMemory, 0, imgSize, 0, &dataPtr), "Failed to map texture staging buffer memory");
	memcpy(dataPtr, globalAtlas.atlasData, imgSize);
	vkUnmapMemory(r->core.device, stagingBufferMemory);
	transition_image_layout(r->core.device, r->commands.commandPool, r->core.graphicsQueue, r->textureImage, VK_FORMAT_R8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	VkCommandBuffer commandBuffer = begin_single_time_commands(r->core.device, r->commands.commandPool);
	VkBufferImageCopy bufferImageCopy = {.bufferOffset = 0, .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageExtent = {(uint32_t)globalAtlas.width, (uint32_t)globalAtlas.height, 1}};
	vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, r->textureImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bufferImageCopy);
	end_single_time_commands(r->core.device, r->commands.commandPool, r->core.graphicsQueue, commandBuffer);
	VK_DESTROY_BUFFER(r->core.device, stagingBuffer, stagingBufferMemory);
	transition_image_layout(r->core.device, r->commands.commandPool, r->core.graphicsQueue, r->textureImage, VK_FORMAT_R8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	VK_CHECK(vkCreateImageView(r->core.device, &VK_IMAGE_VIEW_2D(r->textureImage, VK_FORMAT_R8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT), NULL, &r->textureImageView), "Failed to create texture image view");
	VkSamplerCreateInfo samplerInfo = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, .magFilter = VK_FILTER_LINEAR, .minFilter = VK_FILTER_LINEAR, .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR, .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
	VK_CHECK(vkCreateSampler(r->core.device, &samplerInfo, NULL, &r->textureSampler), "Failed to create texture sampler");

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

	// Shared quad vertex/index buffers for menus, text quads, and node labels
	QuadVertex qv[] = {{{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f}}, {{0.5f, -0.5f, 0.0f}, {1.0f, 0.0f}}, {{0.5f, 0.5f, 0.0f}, {1.0f, 1.0f}}, {{-0.5f, 0.5f, 0.0f}, {0.0f, 1.0f}}};
	uint32_t qi[] = {0, 1, 2, 2, 3, 0};
	VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, sizeof(qv), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &r->menuQuadVertexBuffer, &r->menuQuadVertexBufferMemory);
	update_buffer(r->core.device, r->menuQuadVertexBufferMemory, sizeof(qv), qv);
	VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, sizeof(qi), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, &r->menuQuadIndexBuffer, &r->menuQuadIndexBufferMemory);
	update_buffer(r->core.device, r->menuQuadIndexBufferMemory, sizeof(qi), qi);
	r->menuQuadIndexCount = 6;

	UIVertex uiBg[] = {{{0, 0, 0}, {0, 0}}, {{1, 0, 0}, {1, 0}}, {{0, 1, 0}, {0, 1}}, {{1, 1, 0}, {1, 1}}};
	VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, sizeof(uiBg), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &r->uiBgVertexBuffer, &r->uiBgVertexBufferMemory);
	update_buffer(r->core.device, r->uiBgVertexBufferMemory, sizeof(uiBg), uiBg);
	VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, sizeof(UIInstance) * 1024, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &r->uiTextInstanceBuffer, &r->uiTextInstanceBufferMemory);

	UIInstance bgInst = {.color = {0, 0, 0, -1.0f}};
	VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, sizeof(UIInstance), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &r->uiBgInstanceBuffer, &r->uiBgInstanceBufferMemory);
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

	r->splc_nodes_buffer = VK_NULL_HANDLE;
	r->splc_nodes_memory = VK_NULL_HANDLE;
	r->splc_edges_buffer = VK_NULL_HANDLE;
	r->splc_edges_memory = VK_NULL_HANDLE;
	r->splc_traffic_buffer = VK_NULL_HANDLE;
	r->splc_traffic_memory = VK_NULL_HANDLE;
	r->splc_level_buffer = VK_NULL_HANDLE;
	r->splc_level_memory = VK_NULL_HANDLE;
	r->splc_max_buffer = VK_NULL_HANDLE;
	r->splc_max_memory = VK_NULL_HANDLE;
	r->splc_level_groups = NULL;
	r->splc_num_levels = 0;
	r->splc_current_level = 0;
	r->splc_last_level_time = 0.0;
	r->splc_level_interval = 0.5f;
	r->splc_active = false;

	// YHRT initialization handled by yhrt_init_pipelines()

	renderer_update_graph(r, graph);
	r->labelTreeNeedsRebuild = true;

	r->uniformBuffers = malloc(sizeof(VkBuffer) * MAX_FRAMES_IN_FLIGHT * MAX_VIEWS);
	r->uniformBuffersMemory = malloc(sizeof(VkDeviceMemory) * MAX_FRAMES_IN_FLIGHT * MAX_VIEWS);
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT * MAX_VIEWS; i++) {
		VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, sizeof(UniformBufferObject), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, &r->uniformBuffers[i], &r->uniformBuffersMemory[i]);
		VK_CHECK(vkMapMemory(r->core.device, r->uniformBuffersMemory[i], 0, sizeof(UniformBufferObject), 0, &r->uboMapped[i]), "Failed to map UBO memory");
	}

	VkDescriptorPoolSize descriptorPoolSizes[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MAX_FRAMES_IN_FLIGHT * MAX_VIEWS * 4}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_FRAMES_IN_FLIGHT * MAX_VIEWS * 4}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, MAX_FRAMES_IN_FLIGHT * MAX_VIEWS * 8}};
	VkDescriptorPoolCreateInfo descriptorPoolInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT, .poolSizeCount = 3, .pPoolSizes = descriptorPoolSizes, .maxSets = MAX_FRAMES_IN_FLIGHT * MAX_VIEWS * 4};
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
		VkWriteDescriptorSet descriptorWrites[] = {VK_WRITE_DESC_BUFFER(r->descriptorSets[i], 0, &bufferInfo, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER), VK_WRITE_DESC_IMAGE(r->descriptorSets[i], 1, &imageInfo, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)};
		vkUpdateDescriptorSets(r->core.device, 2, descriptorWrites, 0, NULL);
	}

	// Create a dummy SPLC max weight buffer so binding 3 is always valid
	VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, &r->splc_max_buffer, &r->splc_max_memory);
	uint32_t zero = 0;
	update_buffer(r->core.device, r->splc_max_memory, sizeof(uint32_t), &zero);

	// Update graphics descriptor sets with SPLC SSBOs (binding 2 = edge weights, binding 3 = max weight)
	if (r->splc_edges_buffer != VK_NULL_HANDLE) {
		VkDescriptorBufferInfo edgeWeightInfo = {r->splc_edges_buffer, 0, VK_WHOLE_SIZE};
		VkDescriptorBufferInfo maxWeightInfo = {r->splc_max_buffer, 0, VK_WHOLE_SIZE};
		for (int i = 0; i < MAX_FRAMES_IN_FLIGHT * MAX_VIEWS; i++) {
			VkWriteDescriptorSet descWrites[] = {
				VK_WRITE_DESC_BUFFER(r->descriptorSets[i], 2, &edgeWeightInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
				VK_WRITE_DESC_BUFFER(r->descriptorSets[i], 3, &maxWeightInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
			};
			vkUpdateDescriptorSets(r->core.device, 2, descWrites, 0, NULL);
		}
	} else {
		VkDescriptorBufferInfo maxWeightInfo = {r->splc_max_buffer, 0, VK_WHOLE_SIZE};
		for (int i = 0; i < MAX_FRAMES_IN_FLIGHT * MAX_VIEWS; i++) {
			VkWriteDescriptorSet maxWrite = VK_WRITE_DESC_BUFFER(r->descriptorSets[i], 3, &maxWeightInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
			vkUpdateDescriptorSets(r->core.device, 1, &maxWrite, 0, NULL);
		}
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

	r->graphUpdateRingIndex = 0;
	for (int i = 0; i < GRAPH_UPDATE_RING_SIZE; i++) {
		VK_CHECK(vkCreateFence(r->core.device, &VK_SIGNALED_FENCE_INFO, NULL, &r->graphUpdateFences[i]), "Failed to create graph update fence");
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

	// Ensure queue is idle before destroying per-image semaphores
	VK_CHECK(vkDeviceWaitIdle(r->core.device), "Failed to wait for device idle during swapchain recreation");

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
	glm_perspective(glm_rad(45.0f), aspect, 0.1f, 1000.0f, r->ubo.proj);
	r->ubo.proj[1][1] *= -1;

	r->framebufferResized = false;
}
