#include "renderer.h"
#include "sphere.h"
#include "text.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FRAMES_IN_FLIGHT 2

static uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return 0;
}

static void createBuffer(VkDevice device, VkPhysicalDevice physicalDevice, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer* buffer, VkDeviceMemory* bufferMemory) {
    VkBufferCreateInfo bufferInfo = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size, .usage = usage, .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
    vkCreateBuffer(device, &bufferInfo, NULL, buffer);
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, *buffer, &memRequirements);
    VkMemoryAllocateInfo allocInfo = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = memRequirements.size, .memoryTypeIndex = findMemoryType(physicalDevice, memRequirements.memoryTypeBits, properties) };
    vkAllocateMemory(device, &allocInfo, NULL, bufferMemory);
    vkBindBufferMemory(device, *buffer, *bufferMemory, 0);
}

static void createImage(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage* image, VkDeviceMemory* imageMemory) {
    VkImageCreateInfo imageInfo = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D, .extent = {width, height, 1}, .mipLevels = 1, .arrayLayers = 1, .format = format, .tiling = tiling, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .usage = usage, .samples = VK_SAMPLE_COUNT_1_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
    vkCreateImage(device, &imageInfo, NULL, image);
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, *image, &memRequirements);
    VkMemoryAllocateInfo allocInfo = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = memRequirements.size, .memoryTypeIndex = findMemoryType(physicalDevice, memRequirements.memoryTypeBits, properties) };
    vkAllocateMemory(device, &allocInfo, NULL, imageMemory);
    vkBindImageMemory(device, *image, *imageMemory, 0);
}

static void transitionImageLayout(VkDevice device, VkCommandPool commandPool, VkQueue graphicsQueue, VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout) {
    VkCommandBufferAllocateInfo allocInfo = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandPool = commandPool, .commandBufferCount = 1 };
    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkImageMemoryBarrier barrier = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, .oldLayout = oldLayout, .newLayout = newLayout, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = image, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1} };
    
    VkPipelineStageFlags sourceStage, destinationStage;
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }

    vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, NULL, 0, NULL, 1, &barrier);
    vkEndCommandBuffer(commandBuffer);
    VkSubmitInfo submitInfo = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &commandBuffer };
    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);
    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}

static VkResult create_shader_module(VkDevice device, const char* path, VkShaderModule* shaderModule) {
    FILE* file = fopen(path, "rb");
    if (!file) return VK_ERROR_INITIALIZATION_FAILED;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    uint32_t* code = malloc(size);
    fread(code, 1, size, file);
    fclose(file);
    VkShaderModuleCreateInfo createInfo = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = size, .pCode = code };
    VkResult result = vkCreateShaderModule(device, &createInfo, NULL, shaderModule);
    free(code);
    return result;
}

int renderer_init(Renderer* r, GLFWwindow* window) {
    r->window = window;
    r->currentFrame = 0;

    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    VkInstanceCreateInfo instanceInfo = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .ppEnabledExtensionNames = glfwExtensions, .enabledExtensionCount = glfwExtensionCount };
    vkCreateInstance(&instanceInfo, NULL, &r->instance);

    VkSurfaceKHR surface;
    glfwCreateWindowSurface(r->instance, window, NULL, &surface);

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(r->instance, &deviceCount, NULL);
    VkPhysicalDevice* devices = malloc(sizeof(VkPhysicalDevice) * deviceCount);
    vkEnumeratePhysicalDevices(r->instance, &deviceCount, devices);
    r->physicalDevice = devices[0];
    free(devices);

    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = 0, .queueCount = 1, .pQueuePriorities = &queuePriority };
    const char* deviceExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo deviceCreateInfo = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1, .pQueueCreateInfos = &queueCreateInfo, .enabledExtensionCount = 1, .ppEnabledExtensionNames = deviceExtensions };
    vkCreateDevice(r->physicalDevice, &deviceCreateInfo, NULL, &r->device);
    vkGetDeviceQueue(r->device, 0, 0, &r->graphicsQueue);
    vkGetDeviceQueue(r->device, 0, 0, &r->presentQueue);

    r->swapchainExtent = (VkExtent2D){800, 600};
    r->swapchainFormat = VK_FORMAT_B8G8R8A8_UNORM;
    VkSwapchainCreateInfoKHR swapchainCreateInfo = { .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, .surface = surface, .minImageCount = 2, .imageFormat = r->swapchainFormat, .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, .imageExtent = r->swapchainExtent, .imageArrayLayers = 1, .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE, .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR, .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, .presentMode = VK_PRESENT_MODE_FIFO_KHR, .clipped = VK_TRUE };
    vkCreateSwapchainKHR(r->device, &swapchainCreateInfo, NULL, &r->swapchain);

    vkGetSwapchainImagesKHR(r->device, r->swapchain, &r->swapchainImageCount, NULL);
    r->swapchainImages = malloc(sizeof(VkImage) * r->swapchainImageCount);
    vkGetSwapchainImagesKHR(r->device, r->swapchain, &r->swapchainImageCount, r->swapchainImages);
    r->swapchainImageViews = malloc(sizeof(VkImageView) * r->swapchainImageCount);
    for (uint32_t i = 0; i < r->swapchainImageCount; i++) {
        VkImageViewCreateInfo viewInfo = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = r->swapchainImages[i], .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = r->swapchainFormat, .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
        vkCreateImageView(r->device, &viewInfo, NULL, &r->swapchainImageViews[i]);
    }

    VkDescriptorSetLayoutBinding bindings[] = {
        { .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_VERTEX_BIT },
        { .binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT }
    };
    VkDescriptorSetLayoutCreateInfo layoutInfo = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 2, .pBindings = bindings };
    vkCreateDescriptorSetLayout(r->device, &layoutInfo, NULL, &r->descriptorSetLayout);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1, .pSetLayouts = &r->descriptorSetLayout };
    vkCreatePipelineLayout(r->device, &pipelineLayoutInfo, NULL, &r->pipelineLayout);

    VkRenderPass colorAttachment = {0}; // Shadowed name, using info directly
    VkAttachmentDescription colorAttachmentDesc = { .format = r->swapchainFormat, .samples = VK_SAMPLE_COUNT_1_BIT, .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR };
    VkAttachmentReference colorAttachmentRef = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription subpass = { .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, .colorAttachmentCount = 1, .pColorAttachments = &colorAttachmentRef };
    VkRenderPassCreateInfo renderPassInfo = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, .attachmentCount = 1, .pAttachments = &colorAttachmentDesc, .subpassCount = 1, .pSubpasses = &subpass };
    vkCreateRenderPass(r->device, &renderPassInfo, NULL, &r->renderPass);

    VkShaderModule vertShaderModule, fragShaderModule;
    create_shader_module(r->device, VERT_SHADER_PATH, &vertShaderModule);
    create_shader_module(r->device, FRAG_SHADER_PATH, &fragShaderModule);
    VkPipelineShaderStageCreateInfo shaderStages[] = { { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vertShaderModule, .pName = "main" }, { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fragShaderModule, .pName = "main" } };
    VkVertexInputBindingDescription bindingDescription = { .binding = 0, .stride = sizeof(Vertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX };
    VkVertexInputAttributeDescription attributeDescriptions[] = { { .binding = 0, .location = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, pos) }, { .binding = 0, .location = 1, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, normal) }, { .binding = 0, .location = 2, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(Vertex, texCoord) } };
    VkPipelineVertexInputStateCreateInfo vertexInputInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &bindingDescription, .vertexAttributeDescriptionCount = 3, .pVertexAttributeDescriptions = attributeDescriptions };
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST };
    VkViewport viewport = { 0.0f, 0.0f, (float)r->swapchainExtent.width, (float)r->swapchainExtent.height, 0.0f, 1.0f };
    VkRect2D scissor = { {0, 0}, r->swapchainExtent };
    VkPipelineViewportStateCreateInfo viewportState = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .pViewports = &viewport, .scissorCount = 1, .pScissors = &scissor };
    VkPipelineRasterizationStateCreateInfo rasterizer = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, .polygonMode = VK_POLYGON_MODE_FILL, .lineWidth = 1.0f, .cullMode = VK_CULL_MODE_BACK_BIT, .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE };
    VkPipelineMultisampleStateCreateInfo multisampling = { .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT };
    VkPipelineColorBlendAttachmentState colorBlendAttachment = { .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT, .blendEnable = VK_FALSE };
    VkPipelineColorBlendStateCreateInfo colorBlending = { .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1, .pAttachments = &colorBlendAttachment };
    VkGraphicsPipelineCreateInfo pipelineInfo = { .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = 2, .pStages = shaderStages, .pVertexInputState = &vertexInputInfo, .pInputAssemblyState = &inputAssembly, .pViewportState = &viewportState, .pRasterizationState = &rasterizer, .pMultisampleState = &multisampling, .pColorBlendState = &colorBlending, .layout = r->pipelineLayout, .renderPass = r->renderPass, .subpass = 0 };
    vkCreateGraphicsPipelines(r->device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &r->graphicsPipeline);
    vkDestroyShaderModule(r->device, fragShaderModule, NULL);
    vkDestroyShaderModule(r->device, vertShaderModule, NULL);

    r->framebuffers = malloc(sizeof(VkFramebuffer) * r->swapchainImageCount);
    for (uint32_t i = 0; i < r->swapchainImageCount; i++) {
        VkFramebufferCreateInfo framebufferInfo = { .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, .renderPass = r->renderPass, .attachmentCount = 1, .pAttachments = &r->swapchainImageViews[i], .width = r->swapchainExtent.width, .height = r->swapchainExtent.height, .layers = 1 };
        vkCreateFramebuffer(r->device, &framebufferInfo, NULL, &r->framebuffers[i]);
    }

    VkCommandPoolCreateInfo commandPoolInfo = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = 0 };
    vkCreateCommandPool(r->device, &commandPoolInfo, NULL, &r->commandPool);

    // Text Texture
    uint8_t* textData;
    int tw, th;
    text_generate_texture("Hello World", &textData, &tw, &th);
    VkDeviceSize imageSize = tw * th;
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    createBuffer(r->device, r->physicalDevice, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &stagingBuffer, &stagingBufferMemory);
    void* data;
    vkMapMemory(r->device, stagingBufferMemory, 0, imageSize, 0, &data);
    memcpy(data, textData, imageSize);
    vkUnmapMemory(r->device, stagingBufferMemory);
    free(textData);

    createImage(r->device, r->physicalDevice, tw, th, VK_FORMAT_R8_UNORM, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &r->textureImage, &r->textureImageMemory);
    transitionImageLayout(r->device, r->commandPool, r->graphicsQueue, r->textureImage, VK_FORMAT_R8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    
    VkCommandBufferAllocateInfo allocInfo = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandPool = r->commandPool, .commandBufferCount = 1 };
    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(r->device, &allocInfo, &commandBuffer);
    VkCommandBufferBeginInfo beginInfo = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    VkBufferImageCopy region = { .bufferOffset = 0, .bufferRowLength = 0, .bufferImageHeight = 0, .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageOffset = {0, 0, 0}, .imageExtent = {(uint32_t)tw, (uint32_t)th, 1} };
    vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, r->textureImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    vkEndCommandBuffer(commandBuffer);
    VkSubmitInfo submitInfo = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &commandBuffer };
    vkQueueSubmit(r->graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(r->graphicsQueue);
    vkFreeCommandBuffers(r->device, r->commandPool, 1, &commandBuffer);
    vkDestroyBuffer(r->device, stagingBuffer, NULL);
    vkFreeMemory(r->device, stagingBufferMemory, NULL);

    transitionImageLayout(r->device, r->commandPool, r->graphicsQueue, r->textureImage, VK_FORMAT_R8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    VkImageViewCreateInfo viewInfo = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = r->textureImage, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = VK_FORMAT_R8_UNORM, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1} };
    vkCreateImageView(r->device, &viewInfo, NULL, &r->textureImageView);
    VkSamplerCreateInfo samplerInfo = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, .magFilter = VK_FILTER_NEAREST, .minFilter = VK_FILTER_NEAREST, .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT, .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT, .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT, .anisotropyEnable = VK_FALSE, .maxAnisotropy = 1.0f, .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK, .unnormalizedCoordinates = VK_FALSE, .compareEnable = VK_FALSE, .compareOp = VK_COMPARE_OP_ALWAYS, .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR };
    vkCreateSampler(r->device, &samplerInfo, NULL, &r->textureSampler);

    // Geometry
    Vertex* vertices;
    uint32_t vertexCount;
    uint32_t* indices;
    sphere_generate(1.0f, 64, 32, &vertices, &vertexCount, &indices, &r->indexCount);
    createBuffer(r->device, r->physicalDevice, sizeof(Vertex) * vertexCount, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->vertexBuffer, &r->vertexBufferMemory);
    vkMapMemory(r->device, r->vertexBufferMemory, 0, sizeof(Vertex) * vertexCount, 0, &data);
    memcpy(data, vertices, sizeof(Vertex) * vertexCount);
    vkUnmapMemory(r->device, r->vertexBufferMemory);
    createBuffer(r->device, r->physicalDevice, sizeof(uint32_t) * r->indexCount, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->indexBuffer, &r->indexBufferMemory);
    vkMapMemory(r->device, r->indexBufferMemory, 0, sizeof(uint32_t) * r->indexCount, 0, &data);
    memcpy(data, indices, sizeof(uint32_t) * r->indexCount);
    vkUnmapMemory(r->device, r->indexBufferMemory);
    free(vertices);
    free(indices);

    r->uniformBuffers = malloc(sizeof(VkBuffer) * MAX_FRAMES_IN_FLIGHT);
    r->uniformBuffersMemory = malloc(sizeof(VkDeviceMemory) * MAX_FRAMES_IN_FLIGHT);
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) createBuffer(r->device, r->physicalDevice, sizeof(UniformBufferObject), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->uniformBuffers[i], &r->uniformBuffersMemory[i]);

    VkDescriptorPoolSize poolSizes[] = { { .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = MAX_FRAMES_IN_FLIGHT }, { .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = MAX_FRAMES_IN_FLIGHT } };
    VkDescriptorPoolCreateInfo poolInfo = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .poolSizeCount = 2, .pPoolSizes = poolSizes, .maxSets = MAX_FRAMES_IN_FLIGHT };
    vkCreateDescriptorPool(r->device, &poolInfo, NULL, &r->descriptorPool);

    VkDescriptorSetLayout layouts[MAX_FRAMES_IN_FLIGHT];
    for(int i=0; i<MAX_FRAMES_IN_FLIGHT; i++) layouts[i] = r->descriptorSetLayout;
    VkDescriptorSetAllocateInfo dsAllocInfo = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = r->descriptorPool, .descriptorSetCount = MAX_FRAMES_IN_FLIGHT, .pSetLayouts = layouts };
    r->descriptorSets = malloc(sizeof(VkDescriptorSet) * MAX_FRAMES_IN_FLIGHT);
    vkAllocateDescriptorSets(r->device, &dsAllocInfo, r->descriptorSets);

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorBufferInfo bInfo = { .buffer = r->uniformBuffers[i], .offset = 0, .range = sizeof(UniformBufferObject) };
        VkDescriptorImageInfo iInfo = { .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, .imageView = r->textureImageView, .sampler = r->textureSampler };
        VkWriteDescriptorSet descriptorWrites[] = { { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = r->descriptorSets[i], .dstBinding = 0, .dstArrayElement = 0, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1, .pBufferInfo = &bInfo }, { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = r->descriptorSets[i], .dstBinding = 1, .dstArrayElement = 0, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1, .pImageInfo = &iInfo } };
        vkUpdateDescriptorSets(r->device, 2, descriptorWrites, 0, NULL);
    }

    r->commandBuffers = malloc(sizeof(VkCommandBuffer) * MAX_FRAMES_IN_FLIGHT);
    VkCommandBufferAllocateInfo cbAllocInfo = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = r->commandPool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = MAX_FRAMES_IN_FLIGHT };
    vkAllocateCommandBuffers(r->device, &cbAllocInfo, r->commandBuffers);

    r->imageAvailableSemaphores = malloc(sizeof(VkSemaphore) * MAX_FRAMES_IN_FLIGHT);
    r->renderFinishedSemaphores = malloc(sizeof(VkSemaphore) * MAX_FRAMES_IN_FLIGHT);
    r->inFlightFences = malloc(sizeof(VkFence) * MAX_FRAMES_IN_FLIGHT);
    VkSemaphoreCreateInfo sInfo = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo fInfo = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT };
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkCreateSemaphore(r->device, &sInfo, NULL, &r->imageAvailableSemaphores[i]);
        vkCreateSemaphore(r->device, &sInfo, NULL, &r->renderFinishedSemaphores[i]);
        vkCreateFence(r->device, &fInfo, NULL, &r->inFlightFences[i]);
    }

    glm_mat4_identity(r->ubo.model);
    glm_mat4_identity(r->ubo.view);
    glm_perspective(glm_rad(45.0f), 800.0f/600.0f, 0.1f, 100.0f, r->ubo.proj);
    r->ubo.proj[1][1] *= -1;
    return 0;
}

void renderer_update_view(Renderer* r, vec3 pos, vec3 front, vec3 up) {
    vec3 center;
    glm_vec3_add(pos, front, center);
    glm_lookat(pos, center, up, r->ubo.view);
}

void renderer_draw_frame(Renderer* r) {
    vkWaitForFences(r->device, 1, &r->inFlightFences[r->currentFrame], VK_TRUE, UINT64_MAX);
    vkResetFences(r->device, 1, &r->inFlightFences[r->currentFrame]);
    uint32_t imageIndex;
    vkAcquireNextImageKHR(r->device, r->swapchain, UINT64_MAX, r->imageAvailableSemaphores[r->currentFrame], VK_NULL_HANDLE, &imageIndex);
    void* data;
    vkMapMemory(r->device, r->uniformBuffersMemory[r->currentFrame], 0, sizeof(UniformBufferObject), 0, &data);
    memcpy(data, &r->ubo, sizeof(UniformBufferObject));
    vkUnmapMemory(r->device, r->uniformBuffersMemory[r->currentFrame]);
    vkResetCommandBuffer(r->commandBuffers[r->currentFrame], 0);
    VkCommandBufferBeginInfo bInfo = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    vkBeginCommandBuffer(r->commandBuffers[r->currentFrame], &bInfo);
    VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
    VkRenderPassBeginInfo rpInfo = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, .renderPass = r->renderPass, .framebuffer = r->framebuffers[imageIndex], .renderArea = {{0, 0}, r->swapchainExtent}, .clearValueCount = 1, .pClearValues = &clearColor };
    vkCmdBeginRenderPass(r->commandBuffers[r->currentFrame], &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(r->commandBuffers[r->currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, r->graphicsPipeline);
    VkBuffer vertexBuffers[] = {r->vertexBuffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(r->commandBuffers[r->currentFrame], 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(r->commandBuffers[r->currentFrame], r->indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdBindDescriptorSets(r->commandBuffers[r->currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipelineLayout, 0, 1, &r->descriptorSets[r->currentFrame], 0, NULL);
    vkCmdDrawIndexed(r->commandBuffers[r->currentFrame], r->indexCount, 1, 0, 0, 0);
    vkCmdEndRenderPass(r->commandBuffers[r->currentFrame]);
    vkEndCommandBuffer(r->commandBuffers[r->currentFrame]);
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSubmitInfo submitInfo = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .waitSemaphoreCount = 1, .pWaitSemaphores = &r->imageAvailableSemaphores[r->currentFrame], .pWaitDstStageMask = waitStages, .commandBufferCount = 1, .pCommandBuffers = &r->commandBuffers[r->currentFrame], .signalSemaphoreCount = 1, .pSignalSemaphores = &r->renderFinishedSemaphores[r->currentFrame] };
    vkQueueSubmit(r->graphicsQueue, 1, &submitInfo, r->inFlightFences[r->currentFrame]);
    VkPresentInfoKHR presentInfo = { .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, .waitSemaphoreCount = 1, .pWaitSemaphores = &r->renderFinishedSemaphores[r->currentFrame], .swapchainCount = 1, .pSwapchains = &r->swapchain, .pImageIndices = &imageIndex };
    vkQueuePresentKHR(r->presentQueue, &presentInfo);
    r->currentFrame = (r->currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void renderer_cleanup(Renderer* r) {
    vkDeviceWaitIdle(r->device);
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroySemaphore(r->device, r->renderFinishedSemaphores[i], NULL);
        vkDestroySemaphore(r->device, r->imageAvailableSemaphores[i], NULL);
        vkDestroyFence(r->device, r->inFlightFences[i], NULL);
        vkDestroyBuffer(r->device, r->uniformBuffers[i], NULL);
        vkFreeMemory(r->device, r->uniformBuffersMemory[i], NULL);
    }
    vkDestroySampler(r->device, r->textureSampler, NULL);
    vkDestroyImageView(r->device, r->textureImageView, NULL);
    vkDestroyImage(r->device, r->textureImage, NULL);
    vkFreeMemory(r->device, r->textureImageMemory, NULL);
    vkDestroyCommandPool(r->device, r->commandPool, NULL);
    vkDestroyDescriptorPool(r->device, r->descriptorPool, NULL);
    vkDestroyBuffer(r->device, r->indexBuffer, NULL);
    vkFreeMemory(r->device, r->indexBufferMemory, NULL);
    vkDestroyBuffer(r->device, r->vertexBuffer, NULL);
    vkFreeMemory(r->device, r->vertexBufferMemory, NULL);
    for (uint32_t i = 0; i < r->swapchainImageCount; i++) {
        vkDestroyFramebuffer(r->device, r->framebuffers[i], NULL);
        vkDestroyImageView(r->device, r->swapchainImageViews[i], NULL);
    }
    vkDestroyPipeline(r->device, r->graphicsPipeline, NULL);
    vkDestroyPipelineLayout(r->device, r->pipelineLayout, NULL);
    vkDestroyDescriptorSetLayout(r->device, r->descriptorSetLayout, NULL);
    vkDestroyRenderPass(r->device, r->renderPass, NULL);
    vkDestroySwapchainKHR(r->device, r->swapchain, NULL);
    vkDestroyDevice(r->device, NULL);
    vkDestroyInstance(r->instance, NULL);
}
