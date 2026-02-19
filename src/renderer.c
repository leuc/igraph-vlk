#include "renderer.h"
#include "sphere.h"
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

int renderer_init(Renderer* r, GLFWwindow* window, GraphData* graph) {
    r->window = window;
    r->currentFrame = 0;
    r->nodeCount = graph->node_count;
    r->edgeCount = graph->edge_count;

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

    r->swapchainExtent = (VkExtent2D){3440, 1440};
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

    VkDescriptorSetLayoutBinding uboBinding = { .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_VERTEX_BIT };
    VkDescriptorSetLayoutCreateInfo layoutInfo = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 1, .pBindings = &uboBinding };
    vkCreateDescriptorSetLayout(r->device, &layoutInfo, NULL, &r->descriptorSetLayout);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1, .pSetLayouts = &r->descriptorSetLayout };
    vkCreatePipelineLayout(r->device, &pipelineLayoutInfo, NULL, &r->pipelineLayout);

    VkAttachmentDescription colorAttachmentDesc = { .format = r->swapchainFormat, .samples = VK_SAMPLE_COUNT_1_BIT, .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR };
    VkAttachmentReference colorAttachmentRef = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription subpass = { .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, .colorAttachmentCount = 1, .pColorAttachments = &colorAttachmentRef };
    VkRenderPassCreateInfo renderPassInfo = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, .attachmentCount = 1, .pAttachments = &colorAttachmentDesc, .subpassCount = 1, .pSubpasses = &subpass };
    vkCreateRenderPass(r->device, &renderPassInfo, NULL, &r->renderPass);

    // Node Pipeline (Instanced Spheres)
    VkShaderModule vertShaderModule, fragShaderModule;
    create_shader_module(r->device, VERT_SHADER_PATH, &vertShaderModule);
    create_shader_module(r->device, FRAG_SHADER_PATH, &fragShaderModule);
    VkPipelineShaderStageCreateInfo shaderStages[] = { { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vertShaderModule, .pName = "main" }, { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fragShaderModule, .pName = "main" } };
    
    VkVertexInputBindingDescription bindings[] = {
        { .binding = 0, .stride = sizeof(Vertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX },
        { .binding = 1, .stride = sizeof(Node), .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE }
    };
    VkVertexInputAttributeDescription attributes[] = {
        { .binding = 0, .location = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, pos) },
        { .binding = 0, .location = 1, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, normal) },
        { .binding = 0, .location = 2, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(Vertex, texCoord) },
        { .binding = 1, .location = 3, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Node, position) },
        { .binding = 1, .location = 4, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Node, color) },
        { .binding = 1, .location = 5, .format = VK_FORMAT_R32_SFLOAT, .offset = offsetof(Node, size) }
    };
    VkPipelineVertexInputStateCreateInfo vertexInputInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, .vertexBindingDescriptionCount = 2, .pVertexBindingDescriptions = bindings, .vertexAttributeDescriptionCount = 6, .pVertexAttributeDescriptions = attributes };
    
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

    // Edge Pipeline (Lines)
    VkShaderModule edgeVertShader, edgeFragShader;
    create_shader_module(r->device, EDGE_VERT_SHADER_PATH, &edgeVertShader);
    create_shader_module(r->device, EDGE_FRAG_SHADER_PATH, &edgeFragShader);
    VkPipelineShaderStageCreateInfo edgeStages[] = { { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = edgeVertShader, .pName = "main" }, { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = edgeFragShader, .pName = "main" } };
    
    // For edges, we need a special vertex struct that includes node position AND edge size
    typedef struct {
        vec3 pos;
        vec3 color;
        float size;
    } EdgeVertex;

    VkVertexInputBindingDescription edgeBinding = { .binding = 0, .stride = sizeof(EdgeVertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX };
    VkVertexInputAttributeDescription edgeAttributes[] = {
        { .binding = 0, .location = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(EdgeVertex, pos) },
        { .binding = 0, .location = 1, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(EdgeVertex, color) },
        { .binding = 0, .location = 2, .format = VK_FORMAT_R32_SFLOAT, .offset = offsetof(EdgeVertex, size) }
    };
    VkPipelineVertexInputStateCreateInfo edgeVertexInputInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &edgeBinding, .vertexAttributeDescriptionCount = 3, .pVertexAttributeDescriptions = edgeAttributes };
    VkPipelineInputAssemblyStateCreateInfo edgeInputAssembly = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST };
    
    VkGraphicsPipelineCreateInfo edgePipelineInfo = { .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = 2, .pStages = edgeStages, .pVertexInputState = &edgeVertexInputInfo, .pInputAssemblyState = &edgeInputAssembly, .pViewportState = &viewportState, .pRasterizationState = &rasterizer, .pMultisampleState = &multisampling, .pColorBlendState = &colorBlending, .layout = r->pipelineLayout, .renderPass = r->renderPass, .subpass = 0 };
    vkCreateGraphicsPipelines(r->device, VK_NULL_HANDLE, 1, &edgePipelineInfo, NULL, &r->edgePipeline);
    vkDestroyShaderModule(r->device, edgeFragShader, NULL);
    vkDestroyShaderModule(r->device, edgeVertShader, NULL);

    r->framebuffers = malloc(sizeof(VkFramebuffer) * r->swapchainImageCount);
    for (uint32_t i = 0; i < r->swapchainImageCount; i++) {
        VkFramebufferCreateInfo framebufferInfo = { .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, .renderPass = r->renderPass, .attachmentCount = 1, .pAttachments = &r->swapchainImageViews[i], .width = r->swapchainExtent.width, .height = r->swapchainExtent.height, .layers = 1 };
        vkCreateFramebuffer(r->device, &framebufferInfo, NULL, &r->framebuffers[i]);
    }

    // Sphere Geometry
    Vertex* vertices; uint32_t vertexCount; uint32_t* indices;
    sphere_generate(1.0f, 16, 8, &vertices, &vertexCount, &indices, &r->sphereIndexCount);
    createBuffer(r->device, r->physicalDevice, sizeof(Vertex) * vertexCount, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->vertexBuffer, &r->vertexBufferMemory);
    void* data; vkMapMemory(r->device, r->vertexBufferMemory, 0, sizeof(Vertex) * vertexCount, 0, &data); memcpy(data, vertices, sizeof(Vertex) * vertexCount); vkUnmapMemory(r->device, r->vertexBufferMemory);
    createBuffer(r->device, r->physicalDevice, sizeof(uint32_t) * r->sphereIndexCount, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->indexBuffer, &r->indexBufferMemory);
    vkMapMemory(r->device, r->indexBufferMemory, 0, sizeof(uint32_t) * r->sphereIndexCount, 0, &data); memcpy(data, indices, sizeof(uint32_t) * r->sphereIndexCount); vkUnmapMemory(r->device, r->indexBufferMemory);
    free(vertices); free(indices);

    // Instance Data
    createBuffer(r->device, r->physicalDevice, sizeof(Node) * r->nodeCount, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->instanceBuffer, &r->instanceBufferMemory);
    vkMapMemory(r->device, r->instanceBufferMemory, 0, sizeof(Node) * r->nodeCount, 0, &data); memcpy(data, graph->nodes, sizeof(Node) * r->nodeCount); vkUnmapMemory(r->device, r->instanceBufferMemory);

    // Edge Vertex Data
    EdgeVertex* edgeVertices = malloc(sizeof(EdgeVertex) * r->edgeCount * 2);
    for (uint32_t i = 0; i < r->edgeCount; i++) {
        edgeVertices[i * 2].pos[0] = graph->nodes[graph->edges[i].from].position[0];
        edgeVertices[i * 2].pos[1] = graph->nodes[graph->edges[i].from].position[1];
        edgeVertices[i * 2].pos[2] = graph->nodes[graph->edges[i].from].position[2];
        edgeVertices[i * 2].color[0] = graph->nodes[graph->edges[i].from].color[0];
        edgeVertices[i * 2].color[1] = graph->nodes[graph->edges[i].from].color[1];
        edgeVertices[i * 2].color[2] = graph->nodes[graph->edges[i].from].color[2];
        edgeVertices[i * 2].size = graph->edges[i].size;

        edgeVertices[i * 2 + 1].pos[0] = graph->nodes[graph->edges[i].to].position[0];
        edgeVertices[i * 2 + 1].pos[1] = graph->nodes[graph->edges[i].to].position[1];
        edgeVertices[i * 2 + 1].pos[2] = graph->nodes[graph->edges[i].to].position[2];
        edgeVertices[i * 2 + 1].color[0] = graph->nodes[graph->edges[i].to].color[0];
        edgeVertices[i * 2 + 1].color[1] = graph->nodes[graph->edges[i].to].color[1];
        edgeVertices[i * 2 + 1].color[2] = graph->nodes[graph->edges[i].to].color[2];
        edgeVertices[i * 2 + 1].size = graph->edges[i].size;
    }
    createBuffer(r->device, r->physicalDevice, sizeof(EdgeVertex) * r->edgeCount * 2, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->edgeVertexBuffer, &r->edgeVertexBufferMemory);
    vkMapMemory(r->device, r->edgeVertexBufferMemory, 0, sizeof(EdgeVertex) * r->edgeCount * 2, 0, &data); memcpy(data, edgeVertices, sizeof(EdgeVertex) * r->edgeCount * 2); vkUnmapMemory(r->device, r->edgeVertexBufferMemory);
    free(edgeVertices);

    // Uniforms and Sync
    r->uniformBuffers = malloc(sizeof(VkBuffer) * MAX_FRAMES_IN_FLIGHT); r->uniformBuffersMemory = malloc(sizeof(VkDeviceMemory) * MAX_FRAMES_IN_FLIGHT);
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) createBuffer(r->device, r->physicalDevice, sizeof(UniformBufferObject), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->uniformBuffers[i], &r->uniformBuffersMemory[i]);
    VkDescriptorPoolSize poolSize = { .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = MAX_FRAMES_IN_FLIGHT };
    VkDescriptorPoolCreateInfo poolInfo = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .poolSizeCount = 1, .pPoolSizes = &poolSize, .maxSets = MAX_FRAMES_IN_FLIGHT };
    vkCreateDescriptorPool(r->device, &poolInfo, NULL, &r->descriptorPool);
    VkDescriptorSetLayout layouts[MAX_FRAMES_IN_FLIGHT]; for(int i=0; i<MAX_FRAMES_IN_FLIGHT; i++) layouts[i] = r->descriptorSetLayout;
    VkDescriptorSetAllocateInfo dsAllocInfo = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = r->descriptorPool, .descriptorSetCount = MAX_FRAMES_IN_FLIGHT, .pSetLayouts = layouts };
    r->descriptorSets = malloc(sizeof(VkDescriptorSet) * MAX_FRAMES_IN_FLIGHT); vkAllocateDescriptorSets(r->device, &dsAllocInfo, r->descriptorSets);
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorBufferInfo bInfo = { .buffer = r->uniformBuffers[i], .offset = 0, .range = sizeof(UniformBufferObject) };
        VkWriteDescriptorSet descriptorWrite = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = r->descriptorSets[i], .dstBinding = 0, .dstArrayElement = 0, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1, .pBufferInfo = &bInfo };
        vkUpdateDescriptorSets(r->device, 1, &descriptorWrite, 0, NULL);
    }
    VkCommandPoolCreateInfo commandPoolInfo = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = 0 };
    vkCreateCommandPool(r->device, &commandPoolInfo, NULL, &r->commandPool);
    r->commandBuffers = malloc(sizeof(VkCommandBuffer) * MAX_FRAMES_IN_FLIGHT);
    VkCommandBufferAllocateInfo cbAllocInfo = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = r->commandPool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = MAX_FRAMES_IN_FLIGHT };
    vkAllocateCommandBuffers(r->device, &cbAllocInfo, r->commandBuffers);
    r->imageAvailableSemaphores = malloc(sizeof(VkSemaphore) * MAX_FRAMES_IN_FLIGHT); r->renderFinishedSemaphores = malloc(sizeof(VkSemaphore) * MAX_FRAMES_IN_FLIGHT); r->inFlightFences = malloc(sizeof(VkFence) * MAX_FRAMES_IN_FLIGHT);
    VkSemaphoreCreateInfo sInfo = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO }; VkFenceCreateInfo fInfo = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT };
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkCreateSemaphore(r->device, &sInfo, NULL, &r->imageAvailableSemaphores[i]); vkCreateSemaphore(r->device, &sInfo, NULL, &r->renderFinishedSemaphores[i]); vkCreateFence(r->device, &fInfo, NULL, &r->inFlightFences[i]);
    }
        glm_mat4_identity(r->ubo.model);
        glm_mat4_identity(r->ubo.view);
        glm_perspective(glm_rad(45.0f), 3440.0f/1440.0f, 0.1f, 1000.0f, r->ubo.proj);
        r->ubo.proj[1][1] *= -1;
    
    return 0;
}

void renderer_update_view(Renderer* r, vec3 pos, vec3 front, vec3 up) {
    vec3 center; glm_vec3_add(pos, front, center); glm_lookat(pos, center, up, r->ubo.view);
}

void renderer_draw_frame(Renderer* r) {
    vkWaitForFences(r->device, 1, &r->inFlightFences[r->currentFrame], VK_TRUE, UINT64_MAX);
    vkResetFences(r->device, 1, &r->inFlightFences[r->currentFrame]);
    uint32_t imageIndex; vkAcquireNextImageKHR(r->device, r->swapchain, UINT64_MAX, r->imageAvailableSemaphores[r->currentFrame], VK_NULL_HANDLE, &imageIndex);
    void* data; vkMapMemory(r->device, r->uniformBuffersMemory[r->currentFrame], 0, sizeof(UniformBufferObject), 0, &data); memcpy(data, &r->ubo, sizeof(UniformBufferObject)); vkUnmapMemory(r->device, r->uniformBuffersMemory[r->currentFrame]);
    vkResetCommandBuffer(r->commandBuffers[r->currentFrame], 0);
    VkCommandBufferBeginInfo bInfo = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO }; vkBeginCommandBuffer(r->commandBuffers[r->currentFrame], &bInfo);
    VkClearValue clearColor = {{{0.01f, 0.01f, 0.02f, 1.0f}}};
    VkRenderPassBeginInfo rpInfo = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, .renderPass = r->renderPass, .framebuffer = r->framebuffers[imageIndex], .renderArea = {{0, 0}, r->swapchainExtent}, .clearValueCount = 1, .pClearValues = &clearColor };
    vkCmdBeginRenderPass(r->commandBuffers[r->currentFrame], &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(r->commandBuffers[r->currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, r->edgePipeline);
    vkCmdBindDescriptorSets(r->commandBuffers[r->currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipelineLayout, 0, 1, &r->descriptorSets[r->currentFrame], 0, NULL);
    VkDeviceSize offsets[] = {0}; vkCmdBindVertexBuffers(r->commandBuffers[r->currentFrame], 0, 1, &r->edgeVertexBuffer, offsets);
    vkCmdDraw(r->commandBuffers[r->currentFrame], r->edgeCount * 2, 1, 0, 0);
    vkCmdBindPipeline(r->commandBuffers[r->currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, r->graphicsPipeline);
    VkBuffer vBuffers[] = {r->vertexBuffer, r->instanceBuffer}; VkDeviceSize vOffsets[] = {0, 0};
    vkCmdBindVertexBuffers(r->commandBuffers[r->currentFrame], 0, 2, vBuffers, vOffsets);
    vkCmdBindIndexBuffer(r->commandBuffers[r->currentFrame], r->indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(r->commandBuffers[r->currentFrame], r->sphereIndexCount, r->nodeCount, 0, 0, 0);
    vkCmdEndRenderPass(r->commandBuffers[r->currentFrame]); vkEndCommandBuffer(r->commandBuffers[r->currentFrame]);
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
    vkDestroyBuffer(r->device, r->edgeVertexBuffer, NULL); vkFreeMemory(r->device, r->edgeVertexBufferMemory, NULL);
    vkDestroyBuffer(r->device, r->instanceBuffer, NULL); vkFreeMemory(r->device, r->instanceBufferMemory, NULL);
    vkDestroyCommandPool(r->device, r->commandPool, NULL); vkDestroyDescriptorPool(r->device, r->descriptorPool, NULL);
    vkDestroyBuffer(r->device, r->indexBuffer, NULL); vkFreeMemory(r->device, r->indexBufferMemory, NULL);
    vkDestroyBuffer(r->device, r->vertexBuffer, NULL); vkFreeMemory(r->device, r->vertexBufferMemory, NULL);
    for (uint32_t i = 0; i < r->swapchainImageCount; i++) {
        vkDestroyFramebuffer(r->device, r->framebuffers[i], NULL); vkDestroyImageView(r->device, r->swapchainImageViews[i], NULL);
    }
    vkDestroyPipeline(r->device, r->edgePipeline, NULL); vkDestroyPipeline(r->device, r->graphicsPipeline, NULL);
    vkDestroyPipelineLayout(r->device, r->pipelineLayout, NULL); vkDestroyDescriptorSetLayout(r->device, r->descriptorSetLayout, NULL);
    vkDestroyRenderPass(r->device, r->renderPass, NULL); vkDestroySwapchainKHR(r->device, r->swapchain, NULL);
    vkDestroyDevice(r->device, NULL); vkDestroyInstance(r->instance, NULL);
}
