#include "renderer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "renderer_geometry.h"
#include "renderer_pipelines.h"
#include "text.h"
#include "vulkan_utils.h"

#define MAX_FRAMES_IN_FLIGHT 2
#define FONT_PATH "/usr/share/fonts/truetype/inconsolata/Inconsolata.otf"

FontAtlas globalAtlas;
bool atlasLoaded = false;

int renderer_init(Renderer *r, GLFWwindow *window, GraphData *graph) {
	r->window = window;
	r->currentFrame = 0;
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
	r->currentRoutingMode = ROUTING_MODE_SPHERICAL_PCB;
	r->sphereVertexBuffer = VK_NULL_HANDLE;
	r->sphereIndexBuffer = VK_NULL_HANDLE;

	glfwSetWindowTitle(window, "igraph-vlk");

	// Application Icon
	unsigned char icon_pixels[16 * 16 * 4];
	for (int i = 0; i < 16 * 16; i++) {
		icon_pixels[i * 4 + 0] = 50;
		icon_pixels[i * 4 + 1] = 100;
		icon_pixels[i * 4 + 2] = 255;
		icon_pixels[i * 4 + 3] = 255;
	}
	GLFWimage icon = {16, 16, icon_pixels};
	glfwSetWindowIcon(window, 1, &icon);

	uint32_t glfwExtCount = 0;
	const char **glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);
	VkInstanceCreateInfo instInfo = {.sType =
										 VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
									 .ppEnabledExtensionNames = glfwExts,
									 .enabledExtensionCount = glfwExtCount};
	vkCreateInstance(&instInfo, NULL, &r->instance);
	VkSurfaceKHR surface;
	glfwCreateWindowSurface(r->instance, window, NULL, &surface);
	uint32_t devCount = 0;
	vkEnumeratePhysicalDevices(r->instance, &devCount, NULL);
	VkPhysicalDevice *devs = malloc(sizeof(VkPhysicalDevice) * devCount);
	vkEnumeratePhysicalDevices(r->instance, &devCount, devs);
	r->physicalDevice = devs[0];
	free(devs);
	float qPrio = 1.0f;
	VkDeviceQueueCreateInfo qInfo = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		.queueFamilyIndex = 0,
		.queueCount = 1,
		.pQueuePriorities = &qPrio};
	const char *devExts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
	VkDeviceCreateInfo devInfo = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
								  .queueCreateInfoCount = 1,
								  .pQueueCreateInfos = &qInfo,
								  .enabledExtensionCount = 1,
								  .ppEnabledExtensionNames = devExts};
	vkCreateDevice(r->physicalDevice, &devInfo, NULL, &r->device);
	vkGetDeviceQueue(r->device, 0, 0, &r->graphicsQueue);
	vkGetDeviceQueue(r->device, 0, 0, &r->presentQueue);
	r->swapchainExtent = (VkExtent2D){3440, 1440};
	r->swapchainFormat = VK_FORMAT_B8G8R8A8_UNORM;
	VkSwapchainCreateInfoKHR swpInfo = {
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface = surface,
		.minImageCount = 2,
		.imageFormat = r->swapchainFormat,
		.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
		.imageExtent = r->swapchainExtent,
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
		.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.presentMode = VK_PRESENT_MODE_FIFO_KHR,
		.clipped = VK_TRUE};
	vkCreateSwapchainKHR(r->device, &swpInfo, NULL, &r->swapchain);
	vkGetSwapchainImagesKHR(r->device, r->swapchain, &r->swapchainImageCount,
							NULL);
	r->swapchainImages = malloc(sizeof(VkImage) * r->swapchainImageCount);
	vkGetSwapchainImagesKHR(r->device, r->swapchain, &r->swapchainImageCount,
							r->swapchainImages);
	r->swapchainImageViews =
		malloc(sizeof(VkImageView) * r->swapchainImageCount);
	for (uint32_t i = 0; i < r->swapchainImageCount; i++) {
		VkImageViewCreateInfo vInfo = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = r->swapchainImages[i],
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = r->swapchainFormat,
			.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
		vkCreateImageView(r->device, &vInfo, NULL, &r->swapchainImageViews[i]);
	}
	VkDescriptorSetLayoutBinding dslb[] = {
		{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT,
		 NULL},
		{1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
		 VK_SHADER_STAGE_FRAGMENT_BIT, NULL}};
	VkDescriptorSetLayoutCreateInfo layInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 2,
		.pBindings = dslb};
	vkCreateDescriptorSetLayout(r->device, &layInfo, NULL,
								&r->descriptorSetLayout);

	VkPushConstantRange pushConstantRange = {
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		.offset = 0,
		.size = sizeof(float) // For alpha value
	};

	VkPipelineLayoutCreateInfo plyLayInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &r->descriptorSetLayout,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &pushConstantRange};
	vkCreatePipelineLayout(r->device, &plyLayInfo, NULL, &r->pipelineLayout);
	VkAttachmentDescription cAtt = {
		.format = r->swapchainFormat,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};
	VkAttachmentReference cAttRef = {0,
									 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
	VkSubpassDescription sub = {.pipelineBindPoint =
									VK_PIPELINE_BIND_POINT_GRAPHICS,
								.colorAttachmentCount = 1,
								.pColorAttachments = &cAttRef};
	VkRenderPassCreateInfo rpInfo = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &cAtt,
		.subpassCount = 1,
		.pSubpasses = &sub};
	vkCreateRenderPass(r->device, &rpInfo, NULL, &r->renderPass);

	VkCommandPoolCreateInfo cpI = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.queueFamilyIndex = 0};
	vkCreateCommandPool(r->device, &cpI, NULL, &r->commandPool);

	if (!atlasLoaded) {
		text_generate_atlas(FONT_PATH, &globalAtlas);
		atlasLoaded = true;
	}
	createImage(r->device, r->physicalDevice, globalAtlas.width,
				globalAtlas.height, VK_FORMAT_R8_UNORM, VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &r->textureImage,
				&r->textureImageMemory);
	VkDeviceSize imgSize = globalAtlas.width * globalAtlas.height;
	VkBuffer sBuf;
	VkDeviceMemory sBufM;
	createBuffer(r->device, r->physicalDevice, imgSize,
				 VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
					 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				 &sBuf, &sBufM);
	void *dPtr;
	vkMapMemory(r->device, sBufM, 0, imgSize, 0, &dPtr);
	memcpy(dPtr, globalAtlas.atlasData, imgSize);
	vkUnmapMemory(r->device, sBufM);
	transitionImageLayout(r->device, r->commandPool, r->graphicsQueue,
						  r->textureImage, VK_FORMAT_R8_UNORM,
						  VK_IMAGE_LAYOUT_UNDEFINED,
						  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	VkCommandBufferAllocateInfo aI = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandPool = r->commandPool,
		.commandBufferCount = 1};
	VkCommandBuffer cB;
	vkAllocateCommandBuffers(r->device, &aI, &cB);
	VkCommandBufferBeginInfo bI = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
	vkBeginCommandBuffer(cB, &bI);
	VkBufferImageCopy bReg = {
		.bufferOffset = 0,
		.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
		.imageExtent = {(uint32_t)globalAtlas.width,
						(uint32_t)globalAtlas.height, 1}};
	vkCmdCopyBufferToImage(cB, sBuf, r->textureImage,
						   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bReg);
	vkEndCommandBuffer(cB);
	VkSubmitInfo sI = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
					   .commandBufferCount = 1,
					   .pCommandBuffers = &cB};
	vkQueueSubmit(r->graphicsQueue, 1, &sI, VK_NULL_HANDLE);
	vkQueueWaitIdle(r->graphicsQueue);
	vkFreeCommandBuffers(r->device, r->commandPool, 1, &cB);
	vkDestroyBuffer(r->device, sBuf, NULL);
	vkFreeMemory(r->device, sBufM, NULL);
	transitionImageLayout(r->device, r->commandPool, r->graphicsQueue,
						  r->textureImage, VK_FORMAT_R8_UNORM,
						  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
						  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	VkImageViewCreateInfo viewI = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = r->textureImage,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = VK_FORMAT_R8_UNORM,
		.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
	vkCreateImageView(r->device, &viewI, NULL, &r->textureImageView);
	VkSamplerCreateInfo sampI = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_LINEAR,
		.minFilter = VK_FILTER_LINEAR,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
	vkCreateSampler(r->device, &sampI, NULL, &r->textureSampler);

	// Call out to the newly split pipelines file
	renderer_create_pipelines(r);

	r->framebuffers = malloc(sizeof(VkFramebuffer) * r->swapchainImageCount);
	for (uint32_t i = 0; i < r->swapchainImageCount; i++) {
		VkFramebufferCreateInfo fbi = {
			.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.renderPass = r->renderPass,
			.attachmentCount = 1,
			.pAttachments = &r->swapchainImageViews[i],
			.width = 3440,
			.height = 1440,
			.layers = 1};
		vkCreateFramebuffer(r->device, &fbi, NULL, &r->framebuffers[i]);
	}

	for (int i = 0; i < PLATONIC_COUNT; i++) {
		Vertex *v;
		uint32_t vc;
		uint32_t *idx;
		polyhedron_generate_platonic(i, &v, &vc, &idx,
									 &r->platonicIndexCounts[i]);
		createBuffer(r->device, r->physicalDevice, sizeof(Vertex) * vc,
					 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
					 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
						 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
					 &r->vertexBuffers[i], &r->vertexBufferMemories[i]);
		updateBuffer(r->device, r->vertexBufferMemories[i], sizeof(Vertex) * vc,
					 v);
		createBuffer(r->device, r->physicalDevice,
					 sizeof(uint32_t) * r->platonicIndexCounts[i],
					 VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
					 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
						 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
					 &r->indexBuffers[i], &r->indexBufferMemories[i]);
		updateBuffer(r->device, r->indexBufferMemories[i],
					 sizeof(uint32_t) * r->platonicIndexCounts[i], idx);
		free(v);
		free(idx);
	}

	LabelVertex lvs[] = {{{0, 0, 0}, {0, 0}},
						 {{1, 0, 0}, {1, 0}},
						 {{0, 1, 0}, {0, 1}},
						 {{1, 1, 0}, {1, 1}}};
	createBuffer(r->device, r->physicalDevice, sizeof(lvs),
				 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
				 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
					 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				 &r->labelVertexBuffer, &r->labelVertexBufferMemory);
	updateBuffer(r->device, r->labelVertexBufferMemory, sizeof(lvs), lvs);

	UIVertex uiBg[] = {{{-1, -0.9, 0}, {0, 0}},
					   {{1, -0.9, 0}, {1, 0}},
					   {{-1, -1, 0}, {0, 1}},
					   {{1, -1, 0}, {1, 1}}};
	createBuffer(r->device, r->physicalDevice, sizeof(uiBg),
				 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
				 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
					 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				 &r->uiBgVertexBuffer, &r->uiBgVertexBufferMemory);
	updateBuffer(r->device, r->uiBgVertexBufferMemory, sizeof(uiBg), uiBg);
	createBuffer(r->device, r->physicalDevice, sizeof(UIInstance) * 1024,
				 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
				 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
					 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				 &r->uiTextInstanceBuffer, &r->uiTextInstanceBufferMemory);

	r->instanceBuffer = VK_NULL_HANDLE;
	r->edgeVertexBuffer = VK_NULL_HANDLE;
	r->labelInstanceBuffer = VK_NULL_HANDLE;
	renderer_update_graph(r, graph);

	r->uniformBuffers = malloc(sizeof(VkBuffer) * MAX_FRAMES_IN_FLIGHT);
	r->uniformBuffersMemory =
		malloc(sizeof(VkDeviceMemory) * MAX_FRAMES_IN_FLIGHT);
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
		createBuffer(r->device, r->physicalDevice, sizeof(UniformBufferObject),
					 VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
					 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
						 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
					 &r->uniformBuffers[i], &r->uniformBuffersMemory[i]);
	VkDescriptorPoolSize dps[] = {
		{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MAX_FRAMES_IN_FLIGHT},
		{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_FRAMES_IN_FLIGHT}};
	VkDescriptorPoolCreateInfo dpi = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.poolSizeCount = 2,
		.pPoolSizes = dps,
		.maxSets = MAX_FRAMES_IN_FLIGHT};
	vkCreateDescriptorPool(r->device, &dpi, NULL, &r->descriptorPool);
	VkDescriptorSetLayout dsls[MAX_FRAMES_IN_FLIGHT];
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
		dsls[i] = r->descriptorSetLayout;
	VkDescriptorSetAllocateInfo dsa = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = r->descriptorPool,
		.descriptorSetCount = MAX_FRAMES_IN_FLIGHT,
		.pSetLayouts = dsls};
	r->descriptorSets = malloc(sizeof(VkDescriptorSet) * MAX_FRAMES_IN_FLIGHT);
	vkAllocateDescriptorSets(r->device, &dsa, r->descriptorSets);
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		VkDescriptorBufferInfo bi = {r->uniformBuffers[i], 0,
									 sizeof(UniformBufferObject)};
		VkDescriptorImageInfo ii = {r->textureSampler, r->textureImageView,
									VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
		VkWriteDescriptorSet dw[] = {
			{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, r->descriptorSets[i],
			 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, NULL, &bi, NULL},
			{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, r->descriptorSets[i],
			 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &ii, NULL,
			 NULL}};
		vkUpdateDescriptorSets(r->device, 2, dw, 0, NULL);
	}
	r->commandBuffers = malloc(sizeof(VkCommandBuffer) * MAX_FRAMES_IN_FLIGHT);
	VkCommandBufferAllocateInfo cba = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = r->commandPool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = MAX_FRAMES_IN_FLIGHT};
	vkAllocateCommandBuffers(r->device, &cba, r->commandBuffers);
	r->imageAvailableSemaphores =
		malloc(sizeof(VkSemaphore) * MAX_FRAMES_IN_FLIGHT);
	r->renderFinishedSemaphores =
		malloc(sizeof(VkSemaphore) * MAX_FRAMES_IN_FLIGHT);
	r->inFlightFences = malloc(sizeof(VkFence) * MAX_FRAMES_IN_FLIGHT);
	VkSemaphoreCreateInfo si = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
	VkFenceCreateInfo fi = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, NULL,
							VK_FENCE_CREATE_SIGNALED_BIT};
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		vkCreateSemaphore(r->device, &si, NULL,
						  &r->imageAvailableSemaphores[i]);
		vkCreateSemaphore(r->device, &si, NULL,
						  &r->renderFinishedSemaphores[i]);
		vkCreateFence(r->device, &fi, NULL, &r->inFlightFences[i]);
	}
	glm_mat4_identity(r->ubo.model);
	glm_mat4_identity(r->ubo.view);
	glm_perspective(glm_rad(45.0f), 3440.0f / 1440.0f, 0.1f, 1000.0f,
					r->ubo.proj);
	r->ubo.proj[1][1] *= -1;
	return 0;
}

void renderer_update_view(Renderer *r, vec3 pos, vec3 front, vec3 up) {
	vec3 c;
	glm_vec3_add(pos, front, c);
	glm_lookat(pos, c, up, r->ubo.view);
}

void renderer_draw_frame(Renderer *r) {
	vkWaitForFences(r->device, 1, &r->inFlightFences[r->currentFrame], VK_TRUE,
					UINT64_MAX);
	vkResetFences(r->device, 1, &r->inFlightFences[r->currentFrame]);
	uint32_t ii;
	vkAcquireNextImageKHR(r->device, r->swapchain, UINT64_MAX,
						  r->imageAvailableSemaphores[r->currentFrame],
						  VK_NULL_HANDLE, &ii);
	void *d;
	vkMapMemory(r->device, r->uniformBuffersMemory[r->currentFrame], 0,
				sizeof(UniformBufferObject), 0, &d);
	memcpy(d, &r->ubo, sizeof(UniformBufferObject));
	vkUnmapMemory(r->device, r->uniformBuffersMemory[r->currentFrame]);
	vkResetCommandBuffer(r->commandBuffers[r->currentFrame], 0);
	VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
	vkBeginCommandBuffer(r->commandBuffers[r->currentFrame], &bi);
	VkClearValue cv = {{{0.01f, 0.01f, 0.02f, 1.0f}}};
	VkRenderPassBeginInfo rpi = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
								 NULL,
								 r->renderPass,
								 r->framebuffers[ii],
								 {{0, 0}, {3440, 1440}},
								 1,
								 &cv};
	vkCmdBeginRenderPass(r->commandBuffers[r->currentFrame], &rpi,
						 VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindDescriptorSets(r->commandBuffers[r->currentFrame],
							VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipelineLayout,
							0, 1, &r->descriptorSets[r->currentFrame], 0, NULL);
	if (r->showEdges && r->edgeCount > 0) {
		vkCmdBindPipeline(r->commandBuffers[r->currentFrame],
						  VK_PIPELINE_BIND_POINT_GRAPHICS, r->edgePipeline);
		VkDeviceSize off = 0;
		vkCmdBindVertexBuffers(r->commandBuffers[r->currentFrame], 0, 1,
							   &r->edgeVertexBuffer, &off);
		vkCmdDraw(r->commandBuffers[r->currentFrame], r->edgeVertexCount, 1, 0,
				  0);
	}
	if (r->showNodes && r->nodeCount > 0) {
		float alpha_face = 0.5f;
		vkCmdPushConstants(
			r->commandBuffers[r->currentFrame], r->pipelineLayout,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
			sizeof(float), &alpha_face);
		vkCmdBindPipeline(r->commandBuffers[r->currentFrame],
						  VK_PIPELINE_BIND_POINT_GRAPHICS, r->graphicsPipeline);
		for (int i = 0; i < PLATONIC_COUNT; i++) {
			if (r->platonicDrawCalls[i].count == 0)
				continue;
			VkBuffer vbs[] = {r->vertexBuffers[i], r->instanceBuffer};
			VkDeviceSize vos[] = {0, 0};
			vkCmdBindVertexBuffers(r->commandBuffers[r->currentFrame], 0, 2,
								   vbs, vos);
			vkCmdBindIndexBuffer(r->commandBuffers[r->currentFrame],
								 r->indexBuffers[i], 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(r->commandBuffers[r->currentFrame],
							 r->platonicIndexCounts[i],
							 r->platonicDrawCalls[i].count, 0, 0,
							 r->platonicDrawCalls[i].firstInstance);
		}

		float alpha_edge = 1.0f;
		vkCmdPushConstants(
			r->commandBuffers[r->currentFrame], r->pipelineLayout,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
			sizeof(float), &alpha_edge);
		vkCmdBindPipeline(r->commandBuffers[r->currentFrame],
						  VK_PIPELINE_BIND_POINT_GRAPHICS, r->nodeEdgePipeline);
		for (int i = 0; i < PLATONIC_COUNT; i++) {
			if (r->platonicDrawCalls[i].count == 0)
				continue;
			VkBuffer vbs[] = {r->vertexBuffers[i], r->instanceBuffer};
			VkDeviceSize vos[] = {0, 0};
			vkCmdBindVertexBuffers(r->commandBuffers[r->currentFrame], 0, 2,
								   vbs, vos);
			vkCmdBindIndexBuffer(r->commandBuffers[r->currentFrame],
								 r->indexBuffers[i], 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(r->commandBuffers[r->currentFrame],
							 r->platonicIndexCounts[i],
							 r->platonicDrawCalls[i].count, 0, 0,
							 r->platonicDrawCalls[i].firstInstance);
		}
	}
	if (r->showLabels && r->labelCharCount > 0 &&
		r->labelInstanceBuffer != VK_NULL_HANDLE) {
		vkCmdBindPipeline(r->commandBuffers[r->currentFrame],
						  VK_PIPELINE_BIND_POINT_GRAPHICS, r->labelPipeline);
		VkBuffer lbs[] = {r->labelVertexBuffer, r->labelInstanceBuffer};
		VkDeviceSize los[] = {0, 0};
		vkCmdBindVertexBuffers(r->commandBuffers[r->currentFrame], 0, 2, lbs,
							   los);
		vkCmdDraw(r->commandBuffers[r->currentFrame], 4, r->labelCharCount, 0,
				  0);
	}

	// Draw Transparent Spheres (Last for blending)
	if (r->showSpheres && r->numSpheres > 0 &&
		r->sphereVertexBuffer != VK_NULL_HANDLE) {
		float alpha_sphere = 0.2f / (float)r->numSpheres; // Scale transparency
		if (alpha_sphere < 0.02f)
			alpha_sphere = 0.02f;

		vkCmdPushConstants(
			r->commandBuffers[r->currentFrame], r->pipelineLayout,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
			sizeof(float), &alpha_sphere);
		vkCmdBindPipeline(r->commandBuffers[r->currentFrame],
						  VK_PIPELINE_BIND_POINT_GRAPHICS, r->spherePipeline);
		VkBuffer vbs[] = {r->sphereVertexBuffer};
		VkDeviceSize vos[] = {0};
		vkCmdBindVertexBuffers(r->commandBuffers[r->currentFrame], 0, 1, vbs,
							   vos);
		vkCmdBindIndexBuffer(r->commandBuffers[r->currentFrame],
							 r->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);

		for (int s = 0; s < r->numSpheres; s++) {
			vkCmdDrawIndexed(r->commandBuffers[r->currentFrame],
							 r->sphereIndexCounts[s], 1,
							 r->sphereIndexOffsets[s], 0, 0);
		}
	}

	if (r->showUI) {
		vkCmdBindPipeline(r->commandBuffers[r->currentFrame],
						  VK_PIPELINE_BIND_POINT_GRAPHICS, r->uiPipeline);
		VkDeviceSize zero = 0;
		vkCmdBindVertexBuffers(r->commandBuffers[r->currentFrame], 0, 1,
							   &r->uiBgVertexBuffer, &zero);
		UIInstance bgInst = {.color = {0, 0, 0, -1.0f}};
		updateBuffer(r->device, r->uiTextInstanceBufferMemory,
					 sizeof(UIInstance), &bgInst);
		vkCmdDraw(r->commandBuffers[r->currentFrame], 4, 1, 0, 0);
		if (r->uiTextCharCount > 0) {
			VkBuffer ubs[] = {r->labelVertexBuffer, r->uiTextInstanceBuffer};
			VkDeviceSize uos[] = {0, 0};
			vkCmdBindVertexBuffers(r->commandBuffers[r->currentFrame], 0, 2,
								   ubs, uos);
			vkCmdDraw(r->commandBuffers[r->currentFrame], 4, r->uiTextCharCount,
					  0, 0);
		}
	}
	vkCmdEndRenderPass(r->commandBuffers[r->currentFrame]);
	vkEndCommandBuffer(r->commandBuffers[r->currentFrame]);
	VkPipelineStageFlags ws = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO,
					   NULL,
					   1,
					   &r->imageAvailableSemaphores[r->currentFrame],
					   &ws,
					   1,
					   &r->commandBuffers[r->currentFrame],
					   1,
					   &r->renderFinishedSemaphores[r->currentFrame]};
	vkQueueSubmit(r->graphicsQueue, 1, &si, r->inFlightFences[r->currentFrame]);
	VkPresentInfoKHR pi = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
						   NULL,
						   1,
						   &r->renderFinishedSemaphores[r->currentFrame],
						   1,
						   &r->swapchain,
						   &ii,
						   NULL};
	vkQueuePresentKHR(r->presentQueue, &pi);
	r->currentFrame = (r->currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void renderer_cleanup(Renderer *r) {
	vkDeviceWaitIdle(r->device);
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		vkDestroySemaphore(r->device, r->renderFinishedSemaphores[i], NULL);
		vkDestroySemaphore(r->device, r->imageAvailableSemaphores[i], NULL);
		vkDestroyFence(r->device, r->inFlightFences[i], NULL);
		vkDestroyBuffer(r->device, r->uniformBuffers[i], NULL);
		vkFreeMemory(r->device, r->uniformBuffersMemory[i], NULL);
	}
	if (r->labelInstanceBuffer != VK_NULL_HANDLE) {
		vkDestroyBuffer(r->device, r->labelInstanceBuffer, NULL);
		vkFreeMemory(r->device, r->labelInstanceBufferMemory, NULL);
	}
	vkDestroyBuffer(r->device, r->labelVertexBuffer, NULL);
	vkFreeMemory(r->device, r->labelVertexBufferMemory, NULL);
	vkDestroyBuffer(r->device, r->edgeVertexBuffer, NULL);
	vkFreeMemory(r->device, r->edgeVertexBufferMemory, NULL);
	vkDestroyBuffer(r->device, r->instanceBuffer, NULL);
	vkFreeMemory(r->device, r->instanceBufferMemory, NULL);
	for (int i = 0; i < PLATONIC_COUNT; i++) {
		vkDestroyBuffer(r->device, r->vertexBuffers[i], NULL);
		vkFreeMemory(r->device, r->vertexBufferMemories[i], NULL);
		vkDestroyBuffer(r->device, r->indexBuffers[i], NULL);
		vkFreeMemory(r->device, r->indexBufferMemories[i], NULL);
	}
	vkDestroyBuffer(r->device, r->uiBgVertexBuffer, NULL);
	vkFreeMemory(r->device, r->uiBgVertexBufferMemory, NULL);
	vkDestroyBuffer(r->device, r->uiTextInstanceBuffer, NULL);
	vkFreeMemory(r->device, r->uiTextInstanceBufferMemory, NULL);

	vkDestroyCommandPool(r->device, r->commandPool, NULL);
	vkDestroyDescriptorPool(r->device, r->descriptorPool, NULL);
	vkDestroySampler(r->device, r->textureSampler, NULL);
	vkDestroyImageView(r->device, r->textureImageView, NULL);
	vkDestroyImage(r->device, r->textureImage, NULL);
	vkFreeMemory(r->device, r->textureImageMemory, NULL);
	for (uint32_t i = 0; i < r->swapchainImageCount; i++) {
		vkDestroyFramebuffer(r->device, r->framebuffers[i], NULL);
		vkDestroyImageView(r->device, r->swapchainImageViews[i], NULL);
	}
	vkDestroyPipeline(r->device, r->computeSphericalPipeline, NULL);
	vkDestroyPipeline(r->device, r->computeHubSpokePipeline, NULL);
	vkDestroyPipelineLayout(r->device, r->computePipelineLayout, NULL);
	vkDestroyDescriptorSetLayout(r->device, r->computeDescriptorSetLayout,
								 NULL);
	vkDestroyPipeline(r->device, r->uiPipeline, NULL);
	vkDestroyPipeline(r->device, r->labelPipeline, NULL);
	vkDestroyPipeline(r->device, r->edgePipeline, NULL);
	vkDestroyPipeline(r->device, r->nodeEdgePipeline, NULL);
	vkDestroyPipeline(r->device, r->graphicsPipeline, NULL);
	vkDestroyPipelineLayout(r->device, r->pipelineLayout, NULL);
	vkDestroyDescriptorSetLayout(r->device, r->descriptorSetLayout, NULL);
	vkDestroyRenderPass(r->device, r->renderPass, NULL);
	vkDestroySwapchainKHR(r->device, r->swapchain, NULL);
	vkDestroyDevice(r->device, NULL);
	vkDestroyInstance(r->instance, NULL);
}
