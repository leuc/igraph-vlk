#include "vulkan/renderer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "interaction/state.h"
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
	r->xrFramebuffers = NULL;
	r->xrFramebufferImageCount = NULL;
	r->currentRoutingMode = ROUTING_MODE_SPHERICAL_PCB;
	r->sphereVertexBuffer = VK_NULL_HANDLE;
	r->sphereIndexBuffer = VK_NULL_HANDLE;

	// Get actual window size for swapchain
	int width, height;
	glfwGetWindowSize(window, &width, &height);
	r->swapchainExtent = (VkExtent2D){(uint32_t)width, (uint32_t)height};

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
	
	const char *instExts[64];
	uint32_t instExtCount = 0;
	for (uint32_t i = 0; i < glfwExtCount; i++) instExts[instExtCount++] = glfwExts[i];

	if (xr) {
		char xrInstExts[4096];
		uint32_t xrInstExtsSize = sizeof(xrInstExts);
		xr_context_get_vulkan_instance_extensions(xr, xrInstExts, &xrInstExtsSize);
		
		char *token = strtok(xrInstExts, " ");
		while (token) {
			instExts[instExtCount++] = strdup(token);
			token = strtok(NULL, " ");
		}
	}

	VkInstanceCreateInfo instInfo = {
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.ppEnabledExtensionNames = instExts,
		.enabledExtensionCount = instExtCount
	};
	vkCreateInstance(&instInfo, NULL, &r->instance);

	VkSurfaceKHR surface;
	glfwCreateWindowSurface(r->instance, window, NULL, &surface);

	if (xr) {
		r->physicalDevice = xr_context_get_vulkan_graphics_device(xr, r->instance);
	} else {
		uint32_t devCount = 0;
		vkEnumeratePhysicalDevices(r->instance, &devCount, NULL);
		VkPhysicalDevice *devs = malloc(sizeof(VkPhysicalDevice) * devCount);
		vkEnumeratePhysicalDevices(r->instance, &devCount, devs);
		r->physicalDevice = devs[0];
		free(devs);
	}

	float qPrio = 1.0f;
	VkDeviceQueueCreateInfo qInfo = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		.queueFamilyIndex = 0,
		.queueCount = 1,
		.pQueuePriorities = &qPrio
	};

	const char *devExts[64];
	uint32_t devExtCount = 0;
	devExts[devExtCount++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;

	if (xr) {
		char xrDevExts[4096];
		uint32_t xrDevExtsSize = sizeof(xrDevExts);
		xr_context_get_vulkan_device_extensions(xr, xrDevExts, &xrDevExtsSize);

		char *token = strtok(xrDevExts, " ");
		while (token) {
			devExts[devExtCount++] = strdup(token);
			token = strtok(NULL, " ");
		}
	}

	VkDeviceCreateInfo devInfo = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.queueCreateInfoCount = 1,
		.pQueueCreateInfos = &qInfo,
		.enabledExtensionCount = devExtCount,
		.ppEnabledExtensionNames = devExts
	};
	vkCreateDevice(r->physicalDevice, &devInfo, NULL, &r->device);
	vkGetDeviceQueue(r->device, 0, 0, &r->graphicsQueue);
	vkGetDeviceQueue(r->device, 0, 0, &r->presentQueue);
	r->swapchainExtent = (VkExtent2D){3440, 1440};
	r->swapchainFormat = VK_FORMAT_B8G8R8A8_UNORM;

	// Query supported present modes and prefer MAILBOX for higher throughput
	uint32_t presentModeCount;
	vkGetPhysicalDeviceSurfacePresentModesKHR(r->physicalDevice, surface, &presentModeCount, NULL);
	VkPresentModeKHR *presentModes = malloc(sizeof(VkPresentModeKHR) * presentModeCount);
	vkGetPhysicalDeviceSurfacePresentModesKHR(r->physicalDevice, surface, &presentModeCount, presentModes);
	VkPresentModeKHR chosenPresentMode = VK_PRESENT_MODE_FIFO_KHR;
	uint32_t minImageCount = 2;
	for (uint32_t i = 0; i < presentModeCount; i++) {
		if (presentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
			chosenPresentMode = VK_PRESENT_MODE_MAILBOX_KHR;
			minImageCount = 3;
			break;
		}
	}
	free(presentModes);

	VkSwapchainCreateInfoKHR swpInfo = {.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, .surface = surface, .minImageCount = minImageCount, .imageFormat = r->swapchainFormat, .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, .imageExtent = r->swapchainExtent, .imageArrayLayers = 1, .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE, .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR, .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, .presentMode = chosenPresentMode, .clipped = VK_TRUE};
	vkCreateSwapchainKHR(r->device, &swpInfo, NULL, &r->swapchain);
	vkGetSwapchainImagesKHR(r->device, r->swapchain, &r->swapchainImageCount, NULL);
	r->swapchainImages = malloc(sizeof(VkImage) * r->swapchainImageCount);
	vkGetSwapchainImagesKHR(r->device, r->swapchain, &r->swapchainImageCount, r->swapchainImages);
	r->swapchainImageViews = malloc(sizeof(VkImageView) * r->swapchainImageCount);
	for (uint32_t i = 0; i < r->swapchainImageCount; i++) {
		VkImageViewCreateInfo vInfo = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = r->swapchainImages[i], .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = r->swapchainFormat, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
		vkCreateImageView(r->device, &vInfo, NULL, &r->swapchainImageViews[i]);
	}
	VkDescriptorSetLayoutBinding dslb[] = {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, NULL}, {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL}};
	VkDescriptorSetLayoutCreateInfo layInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 2, .pBindings = dslb};
	vkCreateDescriptorSetLayout(r->device, &layInfo, NULL, &r->descriptorSetLayout);

	VkPushConstantRange pushConstantRange = {
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		.offset = 0,
		.size = sizeof(float) // For alpha value
	};

	VkPipelineLayoutCreateInfo plyLayInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1, .pSetLayouts = &r->descriptorSetLayout, .pushConstantRangeCount = 1, .pPushConstantRanges = &pushConstantRange};
	vkCreatePipelineLayout(r->device, &plyLayInfo, NULL, &r->pipelineLayout);
	VkAttachmentDescription cAtt = {.format = r->swapchainFormat, .samples = VK_SAMPLE_COUNT_1_BIT, .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE, .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};
	VkAttachmentDescription dAtt = {.format = VK_FORMAT_D32_SFLOAT, .samples = VK_SAMPLE_COUNT_1_BIT, .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
	VkAttachmentReference cAttRef = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
	VkAttachmentReference dAttRef = {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
	VkSubpassDescription sub = {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, .colorAttachmentCount = 1, .pColorAttachments = &cAttRef, .pDepthStencilAttachment = &dAttRef};
	VkAttachmentDescription atts[] = {cAtt, dAtt};
	VkRenderPassCreateInfo rpInfo = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, .attachmentCount = 2, .pAttachments = atts, .subpassCount = 1, .pSubpasses = &sub};
	vkCreateRenderPass(r->device, &rpInfo, NULL, &r->renderPass);
	r->depthFormat = VK_FORMAT_D32_SFLOAT;

	VkCommandPoolCreateInfo cpI = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = 0};
	vkCreateCommandPool(r->device, &cpI, NULL, &r->commandPool);

	if (!atlasLoaded) {
		text_generate_atlas(FONT_PATH, &globalAtlas);
		atlasLoaded = true;
	}
	createImage(r->device, r->physicalDevice, globalAtlas.width, globalAtlas.height, VK_FORMAT_R8_UNORM, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &r->textureImage, &r->textureImageMemory);
	VkDeviceSize imgSize = globalAtlas.width * globalAtlas.height;
	VkBuffer sBuf;
	VkDeviceMemory sBufM;
	createBuffer(r->device, r->physicalDevice, imgSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &sBuf, &sBufM);
	void *dPtr;
	vkMapMemory(r->device, sBufM, 0, imgSize, 0, &dPtr);
	memcpy(dPtr, globalAtlas.atlasData, imgSize);
	vkUnmapMemory(r->device, sBufM);
	transitionImageLayout(r->device, r->commandPool, r->graphicsQueue, r->textureImage, VK_FORMAT_R8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	VkCommandBufferAllocateInfo aI = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandPool = r->commandPool, .commandBufferCount = 1};
	VkCommandBuffer cB;
	vkAllocateCommandBuffers(r->device, &aI, &cB);
	VkCommandBufferBeginInfo bI = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
	vkBeginCommandBuffer(cB, &bI);
	VkBufferImageCopy bReg = {.bufferOffset = 0, .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageExtent = {(uint32_t)globalAtlas.width, (uint32_t)globalAtlas.height, 1}};
	vkCmdCopyBufferToImage(cB, sBuf, r->textureImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bReg);
	vkEndCommandBuffer(cB);
	VkSubmitInfo sI = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cB};
	vkQueueSubmit(r->graphicsQueue, 1, &sI, VK_NULL_HANDLE);
	vkQueueWaitIdle(r->graphicsQueue);
	vkFreeCommandBuffers(r->device, r->commandPool, 1, &cB);
	vkDestroyBuffer(r->device, sBuf, NULL);
	vkFreeMemory(r->device, sBufM, NULL);
	transitionImageLayout(r->device, r->commandPool, r->graphicsQueue, r->textureImage, VK_FORMAT_R8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	VkImageViewCreateInfo viewI = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = r->textureImage, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = VK_FORMAT_R8_UNORM, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
	vkCreateImageView(r->device, &viewI, NULL, &r->textureImageView);
	VkSamplerCreateInfo sampI = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, .magFilter = VK_FILTER_LINEAR, .minFilter = VK_FILTER_LINEAR, .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR, .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
	vkCreateSampler(r->device, &sampI, NULL, &r->textureSampler);

	// Create depth image for early-Z rejection
	r->depthFormat = VK_FORMAT_D32_SFLOAT;
    uint32_t depth_w = (xr && xr->swapchains) ? xr->swapchains[0].width : 3440;
    uint32_t depth_h = (xr && xr->swapchains) ? xr->swapchains[0].height : 1440;
	createImage(r->device, r->physicalDevice, depth_w, depth_h, r->depthFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &r->depthImage, &r->depthImageMemory);
	VkImageViewCreateInfo depthViewInfo = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = r->depthImage, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = r->depthFormat, .subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1}};
	vkCreateImageView(r->device, &depthViewInfo, NULL, &r->depthImageView);
	transitionImageLayout(r->device, r->commandPool, r->graphicsQueue, r->depthImage, r->depthFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

	// Call out to the newly split pipelines file
	// Create framebuffers for desktop swapchain
	r->framebuffers = malloc(sizeof(VkFramebuffer) * r->swapchainImageCount);
	for (uint32_t i = 0; i < r->swapchainImageCount; i++) {
		VkImageView attachments[] = {r->swapchainImageViews[i], r->depthImageView};
		VkFramebufferCreateInfo fbi = {.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, .renderPass = r->renderPass, .attachmentCount = 2, .pAttachments = attachments, .width = r->swapchainExtent.width, .height = r->swapchainExtent.height, .layers = 1};
		vkCreateFramebuffer(r->device, &fbi, NULL, &r->framebuffers[i]);
	}

	for (int i = 0; i < PLATONIC_COUNT; i++) {
		Vertex *v;
		uint32_t vc;
		uint32_t *idx;
		polyhedron_generate_platonic(i, &v, &vc, &idx, &r->platonicIndexCounts[i]);
		createBuffer(r->device, r->physicalDevice, sizeof(Vertex) * vc, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->vertexBuffers[i], &r->vertexBufferMemories[i]);
		updateBuffer(r->device, r->vertexBufferMemories[i], sizeof(Vertex) * vc, v);
		createBuffer(r->device, r->physicalDevice, sizeof(uint32_t) * r->platonicIndexCounts[i], VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->indexBuffers[i], &r->indexBufferMemories[i]);
		updateBuffer(r->device, r->indexBufferMemories[i], sizeof(uint32_t) * r->platonicIndexCounts[i], idx);
		free(v);
		free(idx);
	}

	LabelVertex lvs[] = {{{0, 0, 0}, {0, 0}}, {{1, 0, 0}, {1, 0}}, {{0, 1, 0}, {0, 1}}, {{1, 1, 0}, {1, 1}}};
	createBuffer(r->device, r->physicalDevice, sizeof(lvs), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->labelVertexBuffer, &r->labelVertexBufferMemory);
	updateBuffer(r->device, r->labelVertexBufferMemory, sizeof(lvs), lvs);

	UIVertex uiBg[] = {{{0, 0, 0}, {0, 0}}, {{1, 0, 0}, {1, 0}}, {{0, 1, 0}, {0, 1}}, {{1, 1, 0}, {1, 1}}};
	createBuffer(r->device, r->physicalDevice, sizeof(uiBg), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->uiBgVertexBuffer, &r->uiBgVertexBufferMemory);
	updateBuffer(r->device, r->uiBgVertexBufferMemory, sizeof(uiBg), uiBg);
	createBuffer(r->device, r->physicalDevice, sizeof(UIInstance) * 1024, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->uiTextInstanceBuffer, &r->uiTextInstanceBufferMemory);

	// Dedicated background instance buffer to avoid corrupting text data
	UIInstance bgInst = {.color = {0, 0, 0, -1.0f}};
	createBuffer(r->device, r->physicalDevice, sizeof(UIInstance), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->uiBgInstanceBuffer, &r->uiBgInstanceBufferMemory);
	updateBuffer(r->device, r->uiBgInstanceBufferMemory, sizeof(UIInstance), &bgInst);

	// Initialize menu buffers (will be filled when menu is generated)
	r->menuQuadVertexBuffer = VK_NULL_HANDLE;
	r->menuQuadVertexBufferMemory = VK_NULL_HANDLE;
	r->menuQuadIndexBuffer = VK_NULL_HANDLE;
	r->menuQuadIndexBufferMemory = VK_NULL_HANDLE;
	r->menuInstanceBuffer = VK_NULL_HANDLE;
	r->menuInstanceBufferMemory = VK_NULL_HANDLE;
	r->menuTextInstanceBuffer = VK_NULL_HANDLE;
	r->menuTextInstanceBufferMemory = VK_NULL_HANDLE;
	r->menuTextCharCount = 0;
	r->menuNodeCount = 0;
	r->menuQuadIndexCount = 0;

	r->crosshairVertexBuffer = VK_NULL_HANDLE;
	r->crosshairVertexBufferMemory = VK_NULL_HANDLE;
	r->crosshairVertexCount = 0;

	// Create numeric widget quad vertex buffer (static geometry for slider)
	QuadVertex numericQuadVertices[] = {// Track: rectangle from (-0.5, -0.02, 0) to (0.5, 0.02, 0)
										{{-0.5f, -0.02f, 0.0f}, {0.0f, 0.0f}},
										{{0.5f, -0.02f, 0.0f}, {1.0f, 0.0f}},
										{{-0.5f, 0.02f, 0.0f}, {0.0f, 1.0f}},
										{{0.5f, 0.02f, 0.0f}, {1.0f, 1.0f}},
										// Thumb: square from (-0.03, -0.05, 0) to (0.03, 0.05, 0)
										{{-0.03f, -0.05f, 0.0f}, {0.0f, 0.0f}},
										{{0.03f, -0.05f, 0.0f}, {1.0f, 0.0f}},
										{{-0.03f, 0.05f, 0.0f}, {0.0f, 1.0f}},
										{{0.03f, 0.05f, 0.0f}, {1.0f, 1.0f}}};
	uint32_t numericQuadIndices[] = {// Track triangles (0-3)
									 0, 1, 2, 2, 1, 3,
									 // Thumb triangles (4-7)
									 4, 5, 6, 6, 5, 7};
	createBuffer(r->device, r->physicalDevice, sizeof(numericQuadVertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->numericQuadVertexBuffer, &r->numericQuadVertexBufferMemory);
	updateBuffer(r->device, r->numericQuadVertexBufferMemory, sizeof(numericQuadVertices), numericQuadVertices);
	createBuffer(r->device, r->physicalDevice, sizeof(numericQuadIndices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->numericQuadIndexBuffer, &r->numericQuadIndexBufferMemory);
	updateBuffer(r->device, r->numericQuadIndexBufferMemory, sizeof(numericQuadIndices), numericQuadIndices);
	r->numericQuadIndexCount = sizeof(numericQuadIndices) / sizeof(uint32_t);
	// Numeric instance buffer will be dynamically allocated/updated when needed
	r->numericInstanceBuffer = VK_NULL_HANDLE;
	r->numericInstanceBufferMemory = VK_NULL_HANDLE;
	r->numericInstanceCount = 0;

	r->nodePositionBuffer = VK_NULL_HANDLE;
	r->nodePositionMemory = VK_NULL_HANDLE;
	r->nodeAttributeBuffer = VK_NULL_HANDLE;
	r->nodeAttributeMemory = VK_NULL_HANDLE;
	r->nodeAttributeStagingBuffer = VK_NULL_HANDLE;
	r->nodeAttributeStagingMemory = VK_NULL_HANDLE;
	r->nodeCapacity = 0;

	r->edgePositionBuffer = VK_NULL_HANDLE;
	r->edgePositionMemory = VK_NULL_HANDLE;
	r->edgeAttributeBuffer = VK_NULL_HANDLE;
	r->edgeAttributeMemory = VK_NULL_HANDLE;
	r->edgeAttributeStagingBuffer = VK_NULL_HANDLE;
	r->edgeAttributeStagingMemory = VK_NULL_HANDLE;
	r->edgeCapacity = 0;

	r->needsAttributeUpload = VK_TRUE;

	r->labelInstanceBuffer = VK_NULL_HANDLE;
	r->labelStagingBuffer = VK_NULL_HANDLE;
	renderer_update_graph(r, graph);

	r->uniformBuffers = malloc(sizeof(VkBuffer) * MAX_FRAMES_IN_FLIGHT * MAX_VIEWS);
	r->uniformBuffersMemory = malloc(sizeof(VkDeviceMemory) * MAX_FRAMES_IN_FLIGHT * MAX_VIEWS);
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT * MAX_VIEWS; i++)
		createBuffer(r->device, r->physicalDevice, sizeof(UniformBufferObject), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->uniformBuffers[i], &r->uniformBuffersMemory[i]);

	// Persistently map all UBOs
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT * MAX_VIEWS; i++)
		vkMapMemory(r->device, r->uniformBuffersMemory[i], 0, sizeof(UniformBufferObject), 0, &r->uboMapped[i]);

	VkDescriptorPoolSize dps[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MAX_FRAMES_IN_FLIGHT * MAX_VIEWS}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_FRAMES_IN_FLIGHT * MAX_VIEWS}};
	VkDescriptorPoolCreateInfo dpi = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .poolSizeCount = 2, .pPoolSizes = dps, .maxSets = MAX_FRAMES_IN_FLIGHT * MAX_VIEWS};
	vkCreateDescriptorPool(r->device, &dpi, NULL, &r->descriptorPool);

	VkDescriptorSetLayout dsls[MAX_FRAMES_IN_FLIGHT * MAX_VIEWS];
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT * MAX_VIEWS; i++)
		dsls[i] = r->descriptorSetLayout;

	VkDescriptorSetAllocateInfo dsa = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = r->descriptorPool, .descriptorSetCount = MAX_FRAMES_IN_FLIGHT * MAX_VIEWS, .pSetLayouts = dsls};
	r->descriptorSets = malloc(sizeof(VkDescriptorSet) * MAX_FRAMES_IN_FLIGHT * MAX_VIEWS);
	vkAllocateDescriptorSets(r->device, &dsa, r->descriptorSets);

	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT * MAX_VIEWS; i++) {
		VkDescriptorBufferInfo bi = {r->uniformBuffers[i], 0, sizeof(UniformBufferObject)};
		VkDescriptorImageInfo ii = {r->textureSampler, r->textureImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
		VkWriteDescriptorSet dw[] = {{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, r->descriptorSets[i], 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, NULL, &bi, NULL}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, r->descriptorSets[i], 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &ii, NULL, NULL}};
		vkUpdateDescriptorSets(r->device, 2, dw, 0, NULL);
	}
	r->commandBuffers = malloc(sizeof(VkCommandBuffer) * MAX_FRAMES_IN_FLIGHT);
	VkCommandBufferAllocateInfo cba = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = r->commandPool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = MAX_FRAMES_IN_FLIGHT};
	vkAllocateCommandBuffers(r->device, &cba, r->commandBuffers);
	r->imageAvailableSemaphores = malloc(sizeof(VkSemaphore) * MAX_FRAMES_IN_FLIGHT);
	r->renderFinishedSemaphores = malloc(sizeof(VkSemaphore) * MAX_FRAMES_IN_FLIGHT);
	r->inFlightFences = malloc(sizeof(VkFence) * MAX_FRAMES_IN_FLIGHT);
	VkSemaphoreCreateInfo si = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
	VkFenceCreateInfo fi = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, NULL, VK_FENCE_CREATE_SIGNALED_BIT};
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		vkCreateSemaphore(r->device, &si, NULL, &r->imageAvailableSemaphores[i]);
		vkCreateSemaphore(r->device, &si, NULL, &r->renderFinishedSemaphores[i]);
		vkCreateFence(r->device, &fi, NULL, &r->inFlightFences[i]);
	}

	// Initialize ring-buffered fences for graph updates
	r->graphUpdateRingIndex = 0;
	for (int i = 0; i < GRAPH_UPDATE_RING_SIZE; i++) {
		vkCreateFence(r->device, &fi, NULL, &r->graphUpdateFences[i]);
	}

	// Initialize persistent compute context
	r->computeCtx.initialized = VK_FALSE;
	r->computeCtx.cmdPool = VK_NULL_HANDLE;
	r->computeCtx.cmdBuf = VK_NULL_HANDLE;
	r->computeCtx.fence = VK_NULL_HANDLE;
	r->computeCtx.pool = VK_NULL_HANDLE;
	r->computeCtx.nodeBuf = VK_NULL_HANDLE;
	r->computeCtx.edgeBuf = VK_NULL_HANDLE;
	r->computeCtx.hubBuf = VK_NULL_HANDLE;

	// Initialize command buffer caching
	r->cmdBufferValid = VK_FALSE;
	r->lastSceneHash = 0;
	glm_mat4_identity(r->ubo.model);
	glm_mat4_identity(r->ubo.view);
	glm_perspective(glm_rad(45.0f), (float)width / (float)height, 0.1f, 1000.0f, r->ubo.proj);
	r->ubo.proj[1][1] *= -1;
	return 0;
}

void renderer_create_depth_resources(Renderer *r, uint32_t width, uint32_t height) {
    r->depthFormat = VK_FORMAT_D32_SFLOAT;
    createImage(r->device, r->physicalDevice, width, height, r->depthFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &r->depthImage, &r->depthImageMemory);
    VkImageViewCreateInfo depthViewInfo = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = r->depthImage, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = r->depthFormat, .subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1}};
    vkCreateImageView(r->device, &depthViewInfo, NULL, &r->depthImageView);
    transitionImageLayout(r->device, r->commandPool, r->graphicsQueue, r->depthImage, r->depthFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
}

void renderer_update_view(Renderer *r, vec3 pos, vec3 front, vec3 up)
{
	vec3 c;
	glm_vec3_add(pos, front, c);
	glm_lookat(pos, c, up, r->ubo.view);
}

void renderer_setup_xr(Renderer *r, XrContext *xr) {
    r->xrFramebuffers = malloc(sizeof(VkFramebuffer*) * xr->view_count);
    r->xrFramebufferImageCount = malloc(sizeof(uint32_t) * xr->view_count);

    for (uint32_t i = 0; i < xr->view_count; i++) {
        r->xrFramebufferImageCount[i] = xr->swapchains[i].image_count;
        r->xrFramebuffers[i] = malloc(sizeof(VkFramebuffer) * xr->swapchains[i].image_count);
        xr->swapchains[i].image_views = malloc(sizeof(VkImageView) * xr->swapchains[i].image_count);

        for (uint32_t j = 0; j < xr->swapchains[i].image_count; j++) {
            VkImageViewCreateInfo ivInfo = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = xr->swapchains[i].images[j],
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = VK_FORMAT_B8G8R8A8_SRGB, 
                .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
            };
            vkCreateImageView(r->device, &ivInfo, NULL, &xr->swapchains[i].image_views[j]);

            VkImageView attachments[] = {xr->swapchains[i].image_views[j], r->depthImageView};
            VkFramebufferCreateInfo fbInfo = {
                .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                .renderPass = r->renderPass,
                .attachmentCount = 2,
                .pAttachments = attachments,
                .width = xr->swapchains[i].width,
                .height = xr->swapchains[i].height,
                .layers = 1
            };
            vkCreateFramebuffer(r->device, &fbInfo, NULL, &r->xrFramebuffers[i][j]);
        }
    }
}

void renderer_render_scene(Renderer *r, VkCommandBuffer cmd, VkFramebuffer fb, VkExtent2D extent, mat4 view, mat4 proj, uint32_t view_index)
{
    printf("[Renderer] RenderScene(View: %u, FB: %p, Extent: %ux%u)\n", view_index, (void*)fb, extent.width, extent.height);
	uint32_t ubo_idx = r->currentFrame * MAX_VIEWS + view_index;
	UniformBufferObject eye_ubo = r->ubo;
	glm_mat4_copy(view, eye_ubo.view);
	glm_mat4_copy(proj, eye_ubo.proj);
	memcpy(r->uboMapped[ubo_idx], &eye_ubo, sizeof(UniformBufferObject));

	VkClearValue clearValues[2];
	clearValues[0].color = (VkClearColorValue){0.01f, 0.01f, 0.02f, 1.0f};
	clearValues[1].depthStencil = (VkClearDepthStencilValue){1.0f, 0};

	VkRenderPassBeginInfo rpi = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = r->renderPass,
		.framebuffer = fb,
		.renderArea = {{0, 0}, {extent.width, extent.height}},
		.clearValueCount = 2,
		.pClearValues = clearValues};

	vkCmdBeginRenderPass(cmd, &rpi, VK_SUBPASS_CONTENTS_INLINE);

	// Set dynamic viewport and scissor
	VkViewport viewport = {0.0f, 0.0f, (float)extent.width, (float)extent.height, 0.0f, 1.0f};
    printf("[Renderer] Viewport: 0,0, %fx%f\n", viewport.width, viewport.height);
	vkCmdSetViewport(cmd, 0, 1, &viewport);
	VkRect2D scissor = {{0, 0}, {extent.width, extent.height}};
    printf("[Renderer] Scissor: 0,0, %ux%u\n", scissor.extent.width, scissor.extent.height);
	vkCmdSetScissor(cmd, 0, 1, &scissor);

	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipelineLayout, 0, 1, &r->descriptorSets[ubo_idx], 0, NULL);

	if (r->showEdges && r->edgeCount > 0) {
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->edgePipeline);
		VkBuffer edgeBuffers[] = {r->edgePositionBuffer, r->edgeAttributeBuffer};
		VkDeviceSize offsets[] = {0, 0};
		vkCmdBindVertexBuffers(cmd, 0, 2, edgeBuffers, offsets);
		vkCmdDraw(cmd, r->edgeVertexCount, 1, 0, 0);
	}

	if (r->showNodes && r->nodeCount > 0) {
		float alpha_node = 1.0f;
		vkCmdPushConstants(cmd, r->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float), &alpha_node);
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
		VkBuffer menuVbs[] = {r->menuQuadVertexBuffer, r->menuInstanceBuffer};
		VkDeviceSize menuVos[] = {0, 0};
		vkCmdBindVertexBuffers(cmd, 0, 2, menuVbs, menuVos);
		vkCmdBindIndexBuffer(cmd, r->menuQuadIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
		vkCmdDrawIndexed(cmd, r->menuQuadIndexCount, r->menuNodeCount, 0, 0, 0);

		if (r->menuTextCharCount > 0 && r->menuTextInstanceBuffer != VK_NULL_HANDLE) {
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->labelPipeline);
			VkBuffer mTextVbs[] = {r->labelVertexBuffer, r->menuTextInstanceBuffer};
			VkDeviceSize mTextVos[] = {0, 0};
			vkCmdBindVertexBuffers(cmd, 0, 2, mTextVbs, mTextVos);
			vkCmdDraw(cmd, 4, r->menuTextCharCount, 0, 0);
		}
	}

	if (r->numericInstanceCount > 0 && r->numericInstanceBuffer != VK_NULL_HANDLE) {
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->menuPipeline);
		VkBuffer numericVbs[] = {r->numericQuadVertexBuffer, r->numericInstanceBuffer};
		VkDeviceSize numericVos[] = {0, 0};
		vkCmdBindVertexBuffers(cmd, 0, 2, numericVbs, numericVos);
		vkCmdBindIndexBuffer(cmd, r->numericQuadIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
		vkCmdDrawIndexed(cmd, r->numericQuadIndexCount, r->numericInstanceCount, 0, 0, 0);
	}

	if (r->showSpheres && r->numSpheres > 0 && r->sphereVertexBuffer != VK_NULL_HANDLE) {
		float alpha_sphere = 0.2f / (float)r->numSpheres;
		if (alpha_sphere < 0.02f)
			alpha_sphere = 0.02f;
		vkCmdPushConstants(cmd, r->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float), &alpha_sphere);
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->spherePipeline);
		VkBuffer vbs[] = {r->sphereVertexBuffer};
		VkDeviceSize vos[] = {0};
		vkCmdBindVertexBuffers(cmd, 0, 1, vbs, vos);
		vkCmdBindIndexBuffer(cmd, r->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
		for (int s = 0; s < r->numSpheres; s++) {
			vkCmdDrawIndexed(cmd, r->sphereIndexCounts[s], 1, r->sphereIndexOffsets[s], 0, 0);
		}
	}

	if (r->showUI) {
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->uiPipeline);
		VkBuffer bgUbs[] = {r->uiBgVertexBuffer, r->uiBgInstanceBuffer};
		VkDeviceSize bgUos[] = {0, 0};
		vkCmdBindVertexBuffers(cmd, 0, 2, bgUbs, bgUos);
		vkCmdDraw(cmd, 4, 1, 0, 0);
		if (r->uiTextCharCount > 0) {
			VkBuffer ubs[] = {r->labelVertexBuffer, r->uiTextInstanceBuffer};
			VkDeviceSize uos[] = {0, 0};
			vkCmdBindVertexBuffers(cmd, 0, 2, ubs, uos);
			vkCmdDraw(cmd, 4, r->uiTextCharCount, 0, 0);
		}
	}

	vkCmdEndRenderPass(cmd);
}

void renderer_draw_frame(Renderer *r)
{
	vkWaitForFences(r->device, 1, &r->inFlightFences[r->currentFrame], VK_TRUE, UINT64_MAX);
	vkResetFences(r->device, 1, &r->inFlightFences[r->currentFrame]);
	uint32_t ii;
	vkAcquireNextImageKHR(r->device, r->swapchain, UINT64_MAX, r->imageAvailableSemaphores[r->currentFrame], VK_NULL_HANDLE, &ii);

	vkResetCommandBuffer(r->commandBuffers[r->currentFrame], 0);
	VkCommandBufferBeginInfo bi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
	vkBeginCommandBuffer(r->commandBuffers[r->currentFrame], &bi);

	renderer_render_scene(r, r->commandBuffers[r->currentFrame], r->framebuffers[ii], r->swapchainExtent, r->ubo.view, r->ubo.proj, MAX_VIEWS - 1);


	vkEndCommandBuffer(r->commandBuffers[r->currentFrame]);

	VkPipelineStageFlags ws = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkSubmitInfo si = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &r->imageAvailableSemaphores[r->currentFrame],
		.pWaitDstStageMask = &ws,
		.commandBufferCount = 1,
		.pCommandBuffers = &r->commandBuffers[r->currentFrame],
		.signalSemaphoreCount = 1,
		.pSignalSemaphores = &r->renderFinishedSemaphores[r->currentFrame]};

	vkQueueSubmit(r->graphicsQueue, 1, &si, r->inFlightFences[r->currentFrame]);

	VkPresentInfoKHR pi = {
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &r->renderFinishedSemaphores[r->currentFrame],
		.swapchainCount = 1,
		.pSwapchains = &r->swapchain,
		.pImageIndices = &ii};

	vkQueuePresentKHR(r->presentQueue, &pi);
	r->currentFrame = (r->currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void renderer_cleanup(Renderer *r)
{
	vkDeviceWaitIdle(r->device);
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT * MAX_VIEWS; i++) {
		vkDestroyBuffer(r->device, r->uniformBuffers[i], NULL);
		vkFreeMemory(r->device, r->uniformBuffersMemory[i], NULL);
	}
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		vkDestroySemaphore(r->device, r->renderFinishedSemaphores[i], NULL);
		vkDestroySemaphore(r->device, r->imageAvailableSemaphores[i], NULL);
		vkDestroyFence(r->device, r->inFlightFences[i], NULL);
	}

	// Cleanup ring fences
	for (int i = 0; i < GRAPH_UPDATE_RING_SIZE; i++) {
		vkDestroyFence(r->device, r->graphUpdateFences[i], NULL);
	}

	// Cleanup persistent compute resources
	if (r->computeCtx.initialized) {
		if (r->computeCtx.fence != VK_NULL_HANDLE)
			vkDestroyFence(r->device, r->computeCtx.fence, NULL);
		if (r->computeCtx.cmdBuf != VK_NULL_HANDLE)
			vkFreeCommandBuffers(r->device, r->computeCtx.cmdPool, 1, &r->computeCtx.cmdBuf);
		if (r->computeCtx.cmdPool != VK_NULL_HANDLE)
			vkDestroyCommandPool(r->device, r->computeCtx.cmdPool, NULL);
		if (r->computeCtx.pool != VK_NULL_HANDLE)
			vkDestroyDescriptorPool(r->device, r->computeCtx.pool, NULL);
		if (r->computeCtx.nodeBuf != VK_NULL_HANDLE) {
			vkDestroyBuffer(r->device, r->computeCtx.nodeBuf, NULL);
			vkFreeMemory(r->device, r->computeCtx.nodeMem, NULL);
		}
		if (r->computeCtx.edgeBuf != VK_NULL_HANDLE) {
			vkDestroyBuffer(r->device, r->computeCtx.edgeBuf, NULL);
			vkFreeMemory(r->device, r->computeCtx.edgeMem, NULL);
		}
		if (r->computeCtx.hubBuf != VK_NULL_HANDLE) {
			vkDestroyBuffer(r->device, r->computeCtx.hubBuf, NULL);
			vkFreeMemory(r->device, r->computeCtx.hubMem, NULL);
		}
	}

	if (r->labelInstanceBuffer != VK_NULL_HANDLE) {
		vkDestroyBuffer(r->device, r->labelInstanceBuffer, NULL);
		vkFreeMemory(r->device, r->labelInstanceBufferMemory, NULL);
	}
	if (r->labelStagingBuffer != VK_NULL_HANDLE) {
		vkDestroyBuffer(r->device, r->labelStagingBuffer, NULL);
		vkFreeMemory(r->device, r->labelStagingBufferMemory, NULL);
	}
	vkDestroyBuffer(r->device, r->labelVertexBuffer, NULL);
	vkFreeMemory(r->device, r->labelVertexBufferMemory, NULL);

	vkDestroyBuffer(r->device, r->edgePositionBuffer, NULL);
	vkFreeMemory(r->device, r->edgePositionMemory, NULL);
	vkDestroyBuffer(r->device, r->edgeAttributeBuffer, NULL);
	vkFreeMemory(r->device, r->edgeAttributeMemory, NULL);
	if (r->edgeAttributeStagingBuffer != VK_NULL_HANDLE) {
		vkDestroyBuffer(r->device, r->edgeAttributeStagingBuffer, NULL);
		vkFreeMemory(r->device, r->edgeAttributeStagingMemory, NULL);
	}

	vkDestroyBuffer(r->device, r->nodePositionBuffer, NULL);
	vkFreeMemory(r->device, r->nodePositionMemory, NULL);
	vkDestroyBuffer(r->device, r->nodeAttributeBuffer, NULL);
	vkFreeMemory(r->device, r->nodeAttributeMemory, NULL);
	if (r->nodeAttributeStagingBuffer != VK_NULL_HANDLE) {
		vkDestroyBuffer(r->device, r->nodeAttributeStagingBuffer, NULL);
		vkFreeMemory(r->device, r->nodeAttributeStagingMemory, NULL);
	}
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

	if (r->uiBgInstanceBuffer != VK_NULL_HANDLE) {
		vkDestroyBuffer(r->device, r->uiBgInstanceBuffer, NULL);
		vkFreeMemory(r->device, r->uiBgInstanceBufferMemory, NULL);
	}

	// Cleanup crosshair buffer
	if (r->crosshairVertexBuffer != VK_NULL_HANDLE) {
		vkDestroyBuffer(r->device, r->crosshairVertexBuffer, NULL);
		vkFreeMemory(r->device, r->crosshairVertexBufferMemory, NULL);
	}

	// Cleanup menu buffers
	if (r->menuQuadVertexBuffer != VK_NULL_HANDLE) {
		vkDestroyBuffer(r->device, r->menuQuadVertexBuffer, NULL);
		vkFreeMemory(r->device, r->menuQuadVertexBufferMemory, NULL);
	}
	if (r->menuQuadIndexBuffer != VK_NULL_HANDLE) {
		vkDestroyBuffer(r->device, r->menuQuadIndexBuffer, NULL);
		vkFreeMemory(r->device, r->menuQuadIndexBufferMemory, NULL);
	}
	if (r->menuInstanceBuffer != VK_NULL_HANDLE) {
		vkDestroyBuffer(r->device, r->menuInstanceBuffer, NULL);
		vkFreeMemory(r->device, r->menuInstanceBufferMemory, NULL);
	}
	if (r->menuTextInstanceBuffer != VK_NULL_HANDLE) {
		vkDestroyBuffer(r->device, r->menuTextInstanceBuffer, NULL);
		vkFreeMemory(r->device, r->menuTextInstanceBufferMemory, NULL);
	}

	// Cleanup numeric widget buffers
	if (r->numericQuadVertexBuffer != VK_NULL_HANDLE) {
		vkDestroyBuffer(r->device, r->numericQuadVertexBuffer, NULL);
		vkFreeMemory(r->device, r->numericQuadVertexBufferMemory, NULL);
	}
	if (r->numericQuadIndexBuffer != VK_NULL_HANDLE) {
		vkDestroyBuffer(r->device, r->numericQuadIndexBuffer, NULL);
		vkFreeMemory(r->device, r->numericQuadIndexBufferMemory, NULL);
	}
	if (r->numericInstanceBuffer != VK_NULL_HANDLE) {
		vkDestroyBuffer(r->device, r->numericInstanceBuffer, NULL);
		vkFreeMemory(r->device, r->numericInstanceBufferMemory, NULL);
	}

	if (r->xrFramebuffers) {
		// Assuming we know xr->view_count or stored it. 
		// Actually, let's just use MAX_VIEWS - 1 as a limit if we don't have xr context here.
		// Better: add xr_enabled or similar to Renderer?
		// For now, let's assume view_count is 2 for VR.
		for (int i = 0; i < 2; i++) {
			for (uint32_t j = 0; j < r->xrFramebufferImageCount[i]; j++) {
				vkDestroyFramebuffer(r->device, r->xrFramebuffers[i][j], NULL);
			}
			free(r->xrFramebuffers[i]);
		}
		free(r->xrFramebuffers);
		free(r->xrFramebufferImageCount);
	}

	vkDestroyCommandPool(r->device, r->commandPool, NULL);
	vkDestroyDescriptorPool(r->device, r->descriptorPool, NULL);
	vkDestroyImageView(r->device, r->depthImageView, NULL);
	vkDestroyImage(r->device, r->depthImage, NULL);
	vkFreeMemory(r->device, r->depthImageMemory, NULL);
	vkDestroySampler(r->device, r->textureSampler, NULL);
	vkDestroyImageView(r->device, r->textureImageView, NULL);
	vkDestroyImage(r->device, r->textureImage, NULL);
	vkFreeMemory(r->device, r->textureImageMemory, NULL);
	for (uint32_t i = 0; i < r->swapchainImageCount; i++) {
		vkDestroyFramebuffer(r->device, r->framebuffers[i], NULL);
		vkDestroyImageView(r->device, r->swapchainImageViews[i], NULL);
	}
	vkDestroyPipeline(r->device, r->computeSphericalPipeline, NULL);
	vkDestroyPipelineLayout(r->device, r->computePipelineLayout, NULL);
	vkDestroyDescriptorSetLayout(r->device, r->computeDescriptorSetLayout, NULL);
	vkDestroyPipeline(r->device, r->uiPipeline, NULL);
	vkDestroyPipeline(r->device, r->labelPipeline, NULL);
	vkDestroyPipeline(r->device, r->edgePipeline, NULL);
	vkDestroyPipeline(r->device, r->graphicsPipeline, NULL);
	vkDestroyPipelineLayout(r->device, r->pipelineLayout, NULL);
	vkDestroyDescriptorSetLayout(r->device, r->descriptorSetLayout, NULL);
	vkDestroyRenderPass(r->device, r->renderPass, NULL);
	vkDestroySwapchainKHR(r->device, r->swapchain, NULL);
	vkDestroyDevice(r->device, NULL);
	vkDestroyInstance(r->instance, NULL);
}
