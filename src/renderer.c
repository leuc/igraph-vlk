#include "renderer.h"
#include "polyhedron.h"
#include "text.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FRAMES_IN_FLIGHT 2
#define FONT_PATH "/usr/share/fonts/truetype/inconsolata/Inconsolata.otf"

typedef struct { vec3 pos; vec2 tex; } LabelVertex;
typedef struct { vec3 nodePos; vec4 charRect; vec4 charUV; } LabelInstance;

static FontAtlas globalAtlas;
static bool atlasLoaded = false;

static uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties; vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) { if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) return i; }
    return 0;
}

static void createBuffer(VkDevice device, VkPhysicalDevice physicalDevice, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer* buffer, VkDeviceMemory* bufferMemory) {
    VkBufferCreateInfo bufferInfo = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size, .usage = usage, .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
    vkCreateBuffer(device, &bufferInfo, NULL, buffer);
    VkMemoryRequirements memReqs; vkGetBufferMemoryRequirements(device, *buffer, &memReqs);
    VkMemoryAllocateInfo allocInfo = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = memReqs.size, .memoryTypeIndex = findMemoryType(physicalDevice, memReqs.memoryTypeBits, properties) };
    vkAllocateMemory(device, &allocInfo, NULL, bufferMemory);
    vkBindBufferMemory(device, *buffer, *bufferMemory, 0);
}

static void updateBuffer(VkDevice device, VkDeviceMemory memory, VkDeviceSize size, const void* data) {
    void* mapped; vkMapMemory(device, memory, 0, size, 0, &mapped);
    memcpy(mapped, data, size);
    vkUnmapMemory(device, memory);
}

static void createImage(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage* image, VkDeviceMemory* imageMemory) {
    VkImageCreateInfo imageInfo = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D, .extent = {width, height, 1}, .mipLevels = 1, .arrayLayers = 1, .format = format, .tiling = tiling, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .usage = usage, .samples = VK_SAMPLE_COUNT_1_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
    vkCreateImage(device, &imageInfo, NULL, image);
    VkMemoryRequirements memReqs; vkGetImageMemoryRequirements(device, *image, &memReqs);
    VkMemoryAllocateInfo allocInfo = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = memReqs.size, .memoryTypeIndex = findMemoryType(physicalDevice, memReqs.memoryTypeBits, properties) };
    vkAllocateMemory(device, &allocInfo, NULL, imageMemory);
    vkBindImageMemory(device, *image, *imageMemory, 0);
}

static void transitionImageLayout(VkDevice device, VkCommandPool commandPool, VkQueue graphicsQueue, VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout) {
    VkCommandBufferAllocateInfo allocInfo = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandPool = commandPool, .commandBufferCount = 1 };
    VkCommandBuffer commandBuffer; vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);
    VkCommandBufferBeginInfo beginInfo = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    VkImageMemoryBarrier barrier = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, .oldLayout = oldLayout, .newLayout = newLayout, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = image, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1} };
    VkPipelineStageFlags srcStage, dstStage;
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) { barrier.srcAccessMask = 0; barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT; dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT; }
    else { barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT; srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT; dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; }
    vkCmdPipelineBarrier(commandBuffer, srcStage, dstStage, 0, 0, NULL, 0, NULL, 1, &barrier);
    vkEndCommandBuffer(commandBuffer);
    VkSubmitInfo submitInfo = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &commandBuffer };
    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE); vkQueueWaitIdle(graphicsQueue);
    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}

static VkResult create_shader_module(VkDevice device, const char* path, VkShaderModule* shaderModule) {
    FILE* file = fopen(path, "rb"); if (!file) return VK_ERROR_INITIALIZATION_FAILED;
    fseek(file, 0, SEEK_END); long size = ftell(file); fseek(file, 0, SEEK_SET);
    uint32_t* code = malloc(size); fread(code, 1, size, file); fclose(file);
    VkShaderModuleCreateInfo createInfo = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = size, .pCode = code };
    VkResult result = vkCreateShaderModule(device, &createInfo, NULL, shaderModule);
    free(code); return result;
}

int renderer_init(Renderer* r, GLFWwindow* window, GraphData* graph) {
    r->window = window; r->currentFrame = 0; r->nodeCount = graph->node_count; r->edgeCount = graph->edge_count;
    r->showLabels = true; r->layoutScale = 1.0f;
    uint32_t glfwExtCount = 0; const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);
    VkInstanceCreateInfo instInfo = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .ppEnabledExtensionNames = glfwExts, .enabledExtensionCount = glfwExtCount };
    vkCreateInstance(&instInfo, NULL, &r->instance);
    VkSurfaceKHR surface; glfwCreateWindowSurface(r->instance, window, NULL, &surface);
    uint32_t devCount = 0; vkEnumeratePhysicalDevices(r->instance, &devCount, NULL);
    VkPhysicalDevice* devs = malloc(sizeof(VkPhysicalDevice) * devCount); vkEnumeratePhysicalDevices(r->instance, &devCount, devs);
    r->physicalDevice = devs[0]; free(devs);
    float qPrio = 1.0f; VkDeviceQueueCreateInfo qInfo = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = 0, .queueCount = 1, .pQueuePriorities = &qPrio };
    const char* devExts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo devInfo = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1, .pQueueCreateInfos = &qInfo, .enabledExtensionCount = 1, .ppEnabledExtensionNames = devExts };
    vkCreateDevice(r->physicalDevice, &devInfo, NULL, &r->device);
    vkGetDeviceQueue(r->device, 0, 0, &r->graphicsQueue); vkGetDeviceQueue(r->device, 0, 0, &r->presentQueue);
    r->swapchainExtent = (VkExtent2D){3440, 1440}; r->swapchainFormat = VK_FORMAT_B8G8R8A8_UNORM;
    VkSwapchainCreateInfoKHR swpInfo = { .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, .surface = surface, .minImageCount = 2, .imageFormat = r->swapchainFormat, .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, .imageExtent = r->swapchainExtent, .imageArrayLayers = 1, .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE, .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR, .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, .presentMode = VK_PRESENT_MODE_FIFO_KHR, .clipped = VK_TRUE };
    vkCreateSwapchainKHR(r->device, &swpInfo, NULL, &r->swapchain);
    vkGetSwapchainImagesKHR(r->device, r->swapchain, &r->swapchainImageCount, NULL);
    r->swapchainImages = malloc(sizeof(VkImage) * r->swapchainImageCount); vkGetSwapchainImagesKHR(r->device, r->swapchain, &r->swapchainImageCount, r->swapchainImages);
    r->swapchainImageViews = malloc(sizeof(VkImageView) * r->swapchainImageCount);
    for (uint32_t i = 0; i < r->swapchainImageCount; i++) {
        VkImageViewCreateInfo vInfo = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = r->swapchainImages[i], .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = r->swapchainFormat, .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
        vkCreateImageView(r->device, &vInfo, NULL, &r->swapchainImageViews[i]);
    }
    VkDescriptorSetLayoutBinding bindings[] = { { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, NULL }, { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL } };
    VkDescriptorSetLayoutCreateInfo layInfo = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 2, .pBindings = bindings };
    vkCreateDescriptorSetLayout(r->device, &layInfo, NULL, &r->descriptorSetLayout);
    VkPipelineLayoutCreateInfo plyLayInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1, .pSetLayouts = &r->descriptorSetLayout };
    vkCreatePipelineLayout(r->device, &plyLayInfo, NULL, &r->pipelineLayout);
    VkAttachmentDescription cAtt = { .format = r->swapchainFormat, .samples = VK_SAMPLE_COUNT_1_BIT, .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE, .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR };
    VkAttachmentReference cAttRef = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription sub = { .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, .colorAttachmentCount = 1, .pColorAttachments = &cAttRef };
    VkRenderPassCreateInfo rpInfo = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, .attachmentCount = 1, .pAttachments = &cAtt, .subpassCount = 1, .pSubpasses = &sub };
    vkCreateRenderPass(r->device, &rpInfo, NULL, &r->renderPass);

    VkCommandPoolCreateInfo cpI = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = 0 }; vkCreateCommandPool(r->device, &cpI, NULL, &r->commandPool);

    if(!atlasLoaded) { text_generate_atlas(FONT_PATH, &globalAtlas); atlasLoaded = true; }
    createImage(r->device, r->physicalDevice, globalAtlas.width, globalAtlas.height, VK_FORMAT_R8_UNORM, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &r->textureImage, &r->textureImageMemory);
    VkDeviceSize imgSize = globalAtlas.width * globalAtlas.height; VkBuffer sBuf; VkDeviceMemory sBufM;
    createBuffer(r->device, r->physicalDevice, imgSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &sBuf, &sBufM);
    void* dPtr; vkMapMemory(r->device, sBufM, 0, imgSize, 0, &dPtr); memcpy(dPtr, globalAtlas.atlasData, imgSize); vkUnmapMemory(r->device, sBufM);
    transitionImageLayout(r->device, r->commandPool, r->graphicsQueue, r->textureImage, VK_FORMAT_R8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkCommandBufferAllocateInfo aI = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandPool = r->commandPool, .commandBufferCount = 1 };
    VkCommandBuffer cB; vkAllocateCommandBuffers(r->device, &aI, &cB);
    VkCommandBufferBeginInfo bI = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
    vkBeginCommandBuffer(cB, &bI);
    VkBufferImageCopy reg = { .bufferOffset = 0, .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageExtent = {(uint32_t)globalAtlas.width, (uint32_t)globalAtlas.height, 1} };
    vkCmdCopyBufferToImage(cB, sBuf, r->textureImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &reg);
    vkEndCommandBuffer(cB);
    VkSubmitInfo sI = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cB };
    vkQueueSubmit(r->graphicsQueue, 1, &sI, VK_NULL_HANDLE); vkQueueWaitIdle(r->graphicsQueue);
    vkFreeCommandBuffers(r->device, r->commandPool, 1, &cB); vkDestroyBuffer(r->device, sBuf, NULL); vkFreeMemory(r->device, sBufM, NULL);
    transitionImageLayout(r->device, r->commandPool, r->graphicsQueue, r->textureImage, VK_FORMAT_R8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    VkImageViewCreateInfo viewI = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = r->textureImage, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = VK_FORMAT_R8_UNORM, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1} };
    vkCreateImageView(r->device, &viewI, NULL, &r->textureImageView);
    VkSamplerCreateInfo sampI = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, .magFilter = VK_FILTER_LINEAR, .minFilter = VK_FILTER_LINEAR, .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR, .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE };
    vkCreateSampler(r->device, &sampI, NULL, &r->textureSampler);

    VkShaderModule vMod, fMod, eVMod, efMod, lVMod, lfMod;
    create_shader_module(r->device, VERT_SHADER_PATH, &vMod); create_shader_module(r->device, FRAG_SHADER_PATH, &fMod);
    create_shader_module(r->device, EDGE_VERT_SHADER_PATH, &eVMod); create_shader_module(r->device, EDGE_FRAG_SHADER_PATH, &efMod);
    create_shader_module(r->device, LABEL_VERT_SHADER_PATH, &lVMod); create_shader_module(r->device, LABEL_FRAG_SHADER_PATH, &lfMod);

    VkViewport vp = { 0, 0, 3440, 1440, 0, 1 }; VkRect2D sc = { {0,0}, {3440, 1440} };
    VkPipelineViewportStateCreateInfo vpS = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .pViewports = &vp, .scissorCount = 1, .pScissors = &sc };
    VkPipelineRasterizationStateCreateInfo ras = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, .polygonMode = VK_POLYGON_MODE_FILL, .lineWidth = 1.0f, .cullMode = VK_CULL_MODE_NONE, .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE };
    VkPipelineMultisampleStateCreateInfo mul = { .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT };
    VkPipelineColorBlendAttachmentState colB = { .colorWriteMask = 0xF, .blendEnable = VK_FALSE };
    VkPipelineColorBlendStateCreateInfo colS = { .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1, .pAttachments = &colB };

    VkPipelineShaderStageCreateInfo nstages[] = { {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,NULL,0,VK_SHADER_STAGE_VERTEX_BIT,vMod,"main",NULL}, {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,NULL,0,VK_SHADER_STAGE_FRAGMENT_BIT,fMod,"main",NULL} };
    VkVertexInputBindingDescription nb[] = { {0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX}, {1, sizeof(Node), VK_VERTEX_INPUT_RATE_INSTANCE} };
    VkVertexInputAttributeDescription na[] = { {0,0,VK_FORMAT_R32G32B32_SFLOAT,0}, {1,0,VK_FORMAT_R32G32B32_SFLOAT,12}, {2,0,VK_FORMAT_R32_SFLOAT,24}, {3,1,VK_FORMAT_R32G32B32_SFLOAT,offsetof(Node, position)}, {4,1,VK_FORMAT_R32G32B32_SFLOAT,offsetof(Node, color)}, {5,1,VK_FORMAT_R32_SFLOAT,offsetof(Node, size)} };
    VkPipelineVertexInputStateCreateInfo nvi = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, .vertexBindingDescriptionCount = 2, .pVertexBindingDescriptions = nb, .vertexAttributeDescriptionCount = 6, .pVertexAttributeDescriptions = na };
    VkPipelineInputAssemblyStateCreateInfo niAs = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST };
    VkGraphicsPipelineCreateInfo pInfo = { .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = 2, .pStages = nstages, .pVertexInputState = &nvi, .pInputAssemblyState = &niAs, .pViewportState = &vpS, .pRasterizationState = &ras, .pMultisampleState = &mul, .pColorBlendState = &colS, .layout = r->pipelineLayout, .renderPass = r->renderPass };
    vkCreateGraphicsPipelines(r->device, VK_NULL_HANDLE, 1, &pInfo, NULL, &r->graphicsPipeline);

    VkPipelineShaderStageCreateInfo estages[] = { {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,NULL,0,VK_SHADER_STAGE_VERTEX_BIT,eVMod,"main",NULL}, {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,NULL,0,VK_SHADER_STAGE_FRAGMENT_BIT,efMod,"main",NULL} };
    typedef struct { vec3 pos; vec3 color; float size; } EdgeVertex;
    VkVertexInputBindingDescription eb[] = { {0, sizeof(EdgeVertex), VK_VERTEX_INPUT_RATE_VERTEX} };
    VkVertexInputAttributeDescription ea[] = { {0,0,VK_FORMAT_R32G32B32_SFLOAT,0}, {1,0,VK_FORMAT_R32G32B32_SFLOAT,12}, {2,0,VK_FORMAT_R32_SFLOAT,24} };
    VkPipelineVertexInputStateCreateInfo evi = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = eb, .vertexAttributeDescriptionCount = 3, .pVertexAttributeDescriptions = ea };
    VkPipelineInputAssemblyStateCreateInfo eia = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST };
    VkGraphicsPipelineCreateInfo epInfo = { .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = 2, .pStages = estages, .pVertexInputState = &evi, .pInputAssemblyState = &eia, .pViewportState = &vpS, .pRasterizationState = &ras, .pMultisampleState = &mul, .pColorBlendState = &colS, .layout = r->pipelineLayout, .renderPass = r->renderPass };
    vkCreateGraphicsPipelines(r->device, VK_NULL_HANDLE, 1, &epInfo, NULL, &r->edgePipeline);

    VkPipelineShaderStageCreateInfo lstages[] = { {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,NULL,0,VK_SHADER_STAGE_VERTEX_BIT,lVMod,"main",NULL}, {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,NULL,0,VK_SHADER_STAGE_FRAGMENT_BIT,lfMod,"main",NULL} };
    VkVertexInputBindingDescription lb[] = { {0, sizeof(LabelVertex), VK_VERTEX_INPUT_RATE_VERTEX}, {1, sizeof(LabelInstance), VK_VERTEX_INPUT_RATE_INSTANCE} };
    VkVertexInputAttributeDescription la[] = { {0,0,VK_FORMAT_R32G32B32_SFLOAT,offsetof(LabelVertex, pos)}, {1,0,VK_FORMAT_R32G32_SFLOAT,offsetof(LabelVertex, tex)}, {2,1,VK_FORMAT_R32G32B32_SFLOAT,offsetof(LabelInstance, nodePos)}, {3,1,VK_FORMAT_R32G32B32A32_SFLOAT,offsetof(LabelInstance, charRect)}, {4,1,VK_FORMAT_R32G32B32A32_SFLOAT,offsetof(LabelInstance, charUV)} };
    VkPipelineVertexInputStateCreateInfo lvi = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, .vertexBindingDescriptionCount = 2, .pVertexBindingDescriptions = lb, .vertexAttributeDescriptionCount = 5, .pVertexAttributeDescriptions = la };
    VkPipelineColorBlendAttachmentState lcb = { .colorWriteMask = 0xF, .blendEnable = VK_TRUE, .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA, .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, .colorBlendOp = VK_BLEND_OP_ADD, .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE, .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO, .alphaBlendOp = VK_BLEND_OP_ADD };
    VkPipelineColorBlendStateCreateInfo lcs = { .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1, .pAttachments = &lcb };
    VkPipelineInputAssemblyStateCreateInfo lias = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP };
    VkGraphicsPipelineCreateInfo lpInfo = { .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = 2, .pStages = lstages, .pVertexInputState = &lvi, .pInputAssemblyState = &lias, .pViewportState = &vpS, .pRasterizationState = &ras, .pMultisampleState = &mul, .pColorBlendState = &lcs, .layout = r->pipelineLayout, .renderPass = r->renderPass };
    vkCreateGraphicsPipelines(r->device, VK_NULL_HANDLE, 1, &lpInfo, NULL, &r->labelPipeline);

    vkDestroyShaderModule(r->device, lfMod, NULL); vkDestroyShaderModule(r->device, lVMod, NULL); vkDestroyShaderModule(r->device, efMod, NULL); vkDestroyShaderModule(r->device, eVMod, NULL); vkDestroyShaderModule(r->device, fMod, NULL); vkDestroyShaderModule(r->device, vMod, NULL);

    r->framebuffers = malloc(sizeof(VkFramebuffer) * r->swapchainImageCount);
    for (uint32_t i = 0; i < r->swapchainImageCount; i++) { VkFramebufferCreateInfo fbi = { .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, .renderPass = r->renderPass, .attachmentCount = 1, .pAttachments = &r->swapchainImageViews[i], .width = 3440, .height = 1440, .layers = 1 }; vkCreateFramebuffer(r->device, &fbi, NULL, &r->framebuffers[i]); }

    // Platonic Geometries
    for (int i = 0; i < PLATONIC_COUNT; i++) {
        Vertex* v; uint32_t vc; uint32_t* is; polyhedron_generate_platonic(i, &v, &vc, &is, &r->platonicIndexCounts[i]);
        createBuffer(r->device, r->physicalDevice, sizeof(Vertex)*vc, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->vertexBuffers[i], &r->vertexBufferMemories[i]);
        updateBuffer(r->device, r->vertexBufferMemories[i], sizeof(Vertex)*vc, v);
        createBuffer(r->device, r->physicalDevice, sizeof(uint32_t)*r->platonicIndexCounts[i], VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->indexBuffers[i], &r->indexBufferMemories[i]);
        updateBuffer(r->device, r->indexBufferMemories[i], sizeof(uint32_t)*r->platonicIndexCounts[i], is);
        free(v); free(is);
    }

    createBuffer(r->device, r->physicalDevice, sizeof(Node)*r->nodeCount, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->instanceBuffer, &r->instanceBufferMemory);
    createBuffer(r->device, r->physicalDevice, sizeof(EdgeVertex)*r->edgeCount*2, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->edgeVertexBuffer, &r->edgeVertexBufferMemory);
    LabelVertex lverts[] = { {{0,0,0},{0,0}}, {{1,0,0},{1,0}}, {{0,1,0},{0,1}}, {{1,1,0},{1,1}} };
    createBuffer(r->device, r->physicalDevice, sizeof(lverts), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->labelVertexBuffer, &r->labelVertexBufferMemory);
    updateBuffer(r->device, r->labelVertexBufferMemory, sizeof(lverts), lverts);

    uint32_t tc = 0; for(uint32_t i=0; i<r->nodeCount; i++) if(graph->nodes[i].label) tc += strlen(graph->nodes[i].label);
    r->labelCharCount = tc;
    if(tc > 0) createBuffer(r->device, r->physicalDevice, sizeof(LabelInstance)*tc, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->labelInstanceBuffer, &r->labelInstanceBufferMemory);

    renderer_update_graph(r, graph);

    r->uniformBuffers = malloc(sizeof(VkBuffer)*MAX_FRAMES_IN_FLIGHT); r->uniformBuffersMemory = malloc(sizeof(VkDeviceMemory)*MAX_FRAMES_IN_FLIGHT);
    for(int i=0; i<MAX_FRAMES_IN_FLIGHT; i++) createBuffer(r->device, r->physicalDevice, sizeof(UniformBufferObject), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->uniformBuffers[i], &r->uniformBuffersMemory[i]);
    VkDescriptorPoolSize dps[] = { {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MAX_FRAMES_IN_FLIGHT}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_FRAMES_IN_FLIGHT} };
    VkDescriptorPoolCreateInfo dpi = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .poolSizeCount = 2, .pPoolSizes = dps, .maxSets = MAX_FRAMES_IN_FLIGHT };
    vkCreateDescriptorPool(r->device, &dpi, NULL, &r->descriptorPool);
    VkDescriptorSetLayout dsls[MAX_FRAMES_IN_FLIGHT]; for(int i=0; i<MAX_FRAMES_IN_FLIGHT; i++) dsls[i] = r->descriptorSetLayout;
    VkDescriptorSetAllocateInfo dsa = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = r->descriptorPool, .descriptorSetCount = MAX_FRAMES_IN_FLIGHT, .pSetLayouts = dsls };
    r->descriptorSets = malloc(sizeof(VkDescriptorSet)*MAX_FRAMES_IN_FLIGHT); vkAllocateDescriptorSets(r->device, &dsa, r->descriptorSets);
    for(int i=0; i<MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorBufferInfo bi = { r->uniformBuffers[i], 0, sizeof(UniformBufferObject) };
        VkDescriptorImageInfo ii = { r->textureSampler, r->textureImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkWriteDescriptorSet dw[] = { {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,NULL,r->descriptorSets[i],0,0,1,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,NULL,&bi,NULL}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,NULL,r->descriptorSets[i],1,0,1,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,&ii,NULL,NULL} };
        vkUpdateDescriptorSets(r->device, 2, dw, 0, NULL);
    }
    r->commandBuffers = malloc(sizeof(VkCommandBuffer)*MAX_FRAMES_IN_FLIGHT);
    VkCommandBufferAllocateInfo cba = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = r->commandPool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = MAX_FRAMES_IN_FLIGHT };
    vkAllocateCommandBuffers(r->device, &cba, r->commandBuffers);
    r->imageAvailableSemaphores = malloc(sizeof(VkSemaphore)*MAX_FRAMES_IN_FLIGHT); r->renderFinishedSemaphores = malloc(sizeof(VkSemaphore)*MAX_FRAMES_IN_FLIGHT); r->inFlightFences = malloc(sizeof(VkFence)*MAX_FRAMES_IN_FLIGHT);
    VkSemaphoreCreateInfo si = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO}; VkFenceCreateInfo fi = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,NULL,VK_FENCE_CREATE_SIGNALED_BIT};
    for(int i=0; i<MAX_FRAMES_IN_FLIGHT; i++) { vkCreateSemaphore(r->device, &si, NULL, &r->imageAvailableSemaphores[i]); vkCreateSemaphore(r->device, &si, NULL, &r->renderFinishedSemaphores[i]); vkCreateFence(r->device, &fi, NULL, &r->inFlightFences[i]); }
    glm_mat4_identity(r->ubo.model); glm_mat4_identity(r->ubo.view); glm_perspective(glm_rad(45.0f), 3440.0f/1440.0f, 0.1f, 1000.0f, r->ubo.proj); r->ubo.proj[1][1] *= -1;
    return 0;
}

void renderer_update_graph(Renderer* r, GraphData* graph) {
    vkDeviceWaitIdle(r->device);
    
    // Group nodes by shape (Platonic Type)
    Node* sortedNodes = malloc(sizeof(Node) * graph->node_count);
    uint32_t currentOffset = 0;
    for (int t = 0; t < PLATONIC_COUNT; t++) {
        r->platonicDrawCalls[t].firstInstance = currentOffset;
        uint32_t count = 0;
        for (uint32_t i = 0; i < graph->node_count; i++) {
            int deg = graph->nodes[i].degree;
            PlatonicType pt;
            if (deg < 4) pt = PLATONIC_TETRAHEDRON;
            else if (deg < 6) pt = PLATONIC_CUBE;
            else if (deg < 8) pt = PLATONIC_OCTAHEDRON;
            else if (deg < 12) pt = PLATONIC_DODECAHEDRON;
            else pt = PLATONIC_ICOSAHEDRON;

            if (pt == (PlatonicType)t) {
                sortedNodes[currentOffset + count] = graph->nodes[i];
                glm_vec3_scale(sortedNodes[currentOffset + count].position, r->layoutScale, sortedNodes[currentOffset + count].position);
                count++;
            }
        }
        r->platonicDrawCalls[t].count = count;
        currentOffset += count;
    }
    updateBuffer(r->device, r->instanceBufferMemory, sizeof(Node)*graph->node_count, sortedNodes);

    typedef struct { vec3 pos; vec3 color; float size; } EdgeVertex;
    EdgeVertex* evs = malloc(sizeof(EdgeVertex)*graph->edge_count*2);
    // Use the positions from the updated layout
    for(uint32_t i=0; i<graph->edge_count; i++) {
        vec3 p1, p2; 
        glm_vec3_scale(graph->nodes[graph->edges[i].from].position, r->layoutScale, p1);
        glm_vec3_scale(graph->nodes[graph->edges[i].to].position, r->layoutScale, p2);
        memcpy(evs[i*2].pos, p1, 12); memcpy(evs[i*2].color, graph->nodes[graph->edges[i].from].color, 12); evs[i*2].size = graph->edges[i].size;
        memcpy(evs[i*2+1].pos, p2, 12); memcpy(evs[i*2+1].color, graph->nodes[graph->edges[i].to].color, 12); evs[i*2+1].size = graph->edges[i].size;
    }
    updateBuffer(r->device, r->edgeVertexBufferMemory, sizeof(EdgeVertex)*graph->edge_count*2, evs);
    free(evs);

    if(r->labelCharCount > 0) {
        LabelInstance* li = malloc(sizeof(LabelInstance)*r->labelCharCount); uint32_t k = 0;
        for(uint32_t i=0; i<graph->node_count; i++) {
            if(!graph->nodes[i].label) continue;
            int len = strlen(graph->nodes[i].label); float xoff = 0;
            vec3 pos; glm_vec3_scale(graph->nodes[i].position, r->layoutScale, pos);
            for(int j=0; j<len; j++) {
                unsigned char c = graph->nodes[i].label[j]; CharInfo* ci = (c < 128) ? &globalAtlas.chars[c] : &globalAtlas.chars[32];
                memcpy(li[k].nodePos, pos, 12);
                li[k].nodePos[1] += (0.5f * graph->nodes[i].size) + 0.3f;
                li[k].charRect[0] = xoff + ci->x0; li[k].charRect[1] = ci->y0; li[k].charRect[2] = xoff + ci->x1; li[k].charRect[3] = ci->y1;
                li[k].charUV[0] = ci->u0; li[k].charUV[1] = ci->v0; li[k].charUV[2] = ci->u1; li[k].charUV[3] = ci->v1;
                xoff += ci->xadvance; k++;
            }
        }
        updateBuffer(r->device, r->labelInstanceBufferMemory, sizeof(LabelInstance)*r->labelCharCount, li);
        free(li);
    }
    free(sortedNodes);
}

void renderer_update_view(Renderer* r, vec3 pos, vec3 front, vec3 up) { vec3 c; glm_vec3_add(pos, front, c); glm_lookat(pos, c, up, r->ubo.view); }

void renderer_draw_frame(Renderer* r) {
    vkWaitForFences(r->device, 1, &r->inFlightFences[r->currentFrame], VK_TRUE, UINT64_MAX);
    vkResetFences(r->device, 1, &r->inFlightFences[r->currentFrame]);
    uint32_t ii; vkAcquireNextImageKHR(r->device, r->swapchain, UINT64_MAX, r->imageAvailableSemaphores[r->currentFrame], VK_NULL_HANDLE, &ii);
    void* d; vkMapMemory(r->device, r->uniformBuffersMemory[r->currentFrame], 0, sizeof(UniformBufferObject), 0, &d); memcpy(d, &r->ubo, sizeof(UniformBufferObject)); vkUnmapMemory(r->device, r->uniformBuffersMemory[r->currentFrame]);
    vkResetCommandBuffer(r->commandBuffers[r->currentFrame], 0);
    VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; vkBeginCommandBuffer(r->commandBuffers[r->currentFrame], &bi);
    VkClearValue cv = {{{0.01f, 0.01f, 0.02f, 1.0f}}};
    VkRenderPassBeginInfo rpi = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,NULL,r->renderPass,r->framebuffers[ii],{{0,0},{3440,1440}},1,&cv};
    vkCmdBeginRenderPass(r->commandBuffers[r->currentFrame], &rpi, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindDescriptorSets(r->commandBuffers[r->currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipelineLayout, 0, 1, &r->descriptorSets[r->currentFrame], 0, NULL);
    
    vkCmdBindPipeline(r->commandBuffers[r->currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, r->edgePipeline);
    VkDeviceSize off = 0; vkCmdBindVertexBuffers(r->commandBuffers[r->currentFrame], 0, 1, &r->edgeVertexBuffer, &off); vkCmdDraw(r->commandBuffers[r->currentFrame], r->edgeCount*2, 1, 0, 0);
    
    vkCmdBindPipeline(r->commandBuffers[r->currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, r->graphicsPipeline);
    for (int i = 0; i < PLATONIC_COUNT; i++) {
        if (r->platonicDrawCalls[i].count == 0) continue;
        VkBuffer vbs[] = {r->vertexBuffers[i], r->instanceBuffer}; VkDeviceSize vos[] = {0, 0};
        vkCmdBindVertexBuffers(r->commandBuffers[r->currentFrame], 0, 2, vbs, vos);
        vkCmdBindIndexBuffer(r->commandBuffers[r->currentFrame], r->indexBuffers[i], 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(r->commandBuffers[r->currentFrame], r->platonicIndexCounts[i], r->platonicDrawCalls[i].count, 0, 0, r->platonicDrawCalls[i].firstInstance);
    }
    
    if(r->showLabels && r->labelCharCount > 0) {
        vkCmdBindPipeline(r->commandBuffers[r->currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, r->labelPipeline);
        VkBuffer lbs[] = {r->labelVertexBuffer, r->labelInstanceBuffer}; VkDeviceSize los[] = {0, 0};
        vkCmdBindVertexBuffers(r->commandBuffers[r->currentFrame], 0, 2, lbs, los);
        vkCmdDraw(r->commandBuffers[r->currentFrame], 4, r->labelCharCount, 0, 0);
    }
    vkCmdEndRenderPass(r->commandBuffers[r->currentFrame]); vkEndCommandBuffer(r->commandBuffers[r->currentFrame]);
    VkPipelineStageFlags ws = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO,NULL,1,&r->imageAvailableSemaphores[r->currentFrame],&ws,1,&r->commandBuffers[r->currentFrame],1,&r->renderFinishedSemaphores[r->currentFrame]};
    vkQueueSubmit(r->graphicsQueue, 1, &si, r->inFlightFences[r->currentFrame]);
    VkPresentInfoKHR pi = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,NULL,1,&r->renderFinishedSemaphores[r->currentFrame],1,&r->swapchain,&ii,NULL};
    vkQueuePresentKHR(r->presentQueue, &pi);
    r->currentFrame = (r->currentFrame+1)%MAX_FRAMES_IN_FLIGHT;
}

void renderer_cleanup(Renderer* r) {
    vkDeviceWaitIdle(r->device);
    for(int i=0; i<MAX_FRAMES_IN_FLIGHT; i++) { vkDestroySemaphore(r->device, r->renderFinishedSemaphores[i], NULL); vkDestroySemaphore(r->device, r->imageAvailableSemaphores[i], NULL); vkDestroyFence(r->device, r->inFlightFences[i], NULL); vkDestroyBuffer(r->device, r->uniformBuffers[i], NULL); vkFreeMemory(r->device, r->uniformBuffersMemory[i], NULL); }
    if(r->labelCharCount > 0) { vkDestroyBuffer(r->device, r->labelInstanceBuffer, NULL); vkFreeMemory(r->device, r->labelInstanceBufferMemory, NULL); }
    vkDestroyBuffer(r->device, r->labelVertexBuffer, NULL); vkFreeMemory(r->device, r->labelVertexBufferMemory, NULL);
    vkDestroyBuffer(r->device, r->edgeVertexBuffer, NULL); vkFreeMemory(r->device, r->edgeVertexBufferMemory, NULL);
    vkDestroyBuffer(r->device, r->instanceBuffer, NULL); vkFreeMemory(r->device, r->instanceBufferMemory, NULL);
    for (int i = 0; i < PLATONIC_COUNT; i++) { vkDestroyBuffer(r->device, r->vertexBuffers[i], NULL); vkFreeMemory(r->device, r->vertexBufferMemories[i], NULL); vkDestroyBuffer(r->device, r->indexBuffers[i], NULL); vkFreeMemory(r->device, r->indexBufferMemories[i], NULL); }
    vkDestroyCommandPool(r->device, r->commandPool, NULL); vkDestroyDescriptorPool(r->device, r->descriptorPool, NULL);
    vkDestroySampler(r->device, r->textureSampler, NULL); vkDestroyImageView(r->device, r->textureImageView, NULL); vkDestroyImage(r->device, r->textureImage, NULL); vkFreeMemory(r->device, r->textureImageMemory, NULL);
    for(uint32_t i=0; i<r->swapchainImageCount; i++) { vkDestroyFramebuffer(r->device, r->framebuffers[i], NULL); vkDestroyImageView(r->device, r->swapchainImageViews[i], NULL); }
    vkDestroyPipeline(r->device, r->labelPipeline, NULL); vkDestroyPipeline(r->device, r->edgePipeline, NULL); vkDestroyPipeline(r->device, r->graphicsPipeline, NULL);
    vkDestroyPipelineLayout(r->device, r->pipelineLayout, NULL); vkDestroyDescriptorSetLayout(r->device, r->descriptorSetLayout, NULL);
    vkDestroyRenderPass(r->device, r->renderPass, NULL); vkDestroySwapchainKHR(r->device, r->swapchain, NULL);
    vkDestroyDevice(r->device, NULL); vkDestroyInstance(r->instance, NULL);
}
