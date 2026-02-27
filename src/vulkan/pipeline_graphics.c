#include "vulkan/pipeline_graphics.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vulkan/renderer_geometry.h"
#include "vulkan/renderer_pipelines.h"
#include "vulkan/utils.h"

int pipeline_graphics_create(Renderer *r) {
    VkDevice device = r->device;

    // =========================================================================
    // MAIN GRAPHICS PIPELINE (Nodes)
    // =========================================================================

    // Load shaders
    VkShaderModule vMod = VK_NULL_HANDLE, fMod = VK_NULL_HANDLE;
    create_shader_module(device, VERT_SHADER_PATH, &vMod);
    create_shader_module(device, FRAG_SHADER_PATH, &fMod);

    VkPipelineShaderStageCreateInfo sstages[] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0,
         VK_SHADER_STAGE_VERTEX_BIT, vMod, "main", NULL},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0,
         VK_SHADER_STAGE_FRAGMENT_BIT, fMod, "main", NULL}};

    // Dynamic state
    uint32_t dynamicViewports = 1;
    uint32_t dynamicScissors = 1;
    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                      VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates = dynamicStates};

    // Viewport
    VkPipelineViewportStateCreateInfo vpS = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports = NULL, // Dynamic
        .scissorCount = 1,
        .pScissors = NULL   // Dynamic
    };

    // Rasterization
    VkPipelineRasterizationStateCreateInfo ras = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f};

    // Multisample
    VkPipelineMultisampleStateCreateInfo mul = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .minSampleShading = 1.0f};

    // Color blend (opaque)
    VkPipelineColorBlendAttachmentState cb = {
        .colorWriteMask = 0xF,
        .blendEnable = VK_FALSE};
    VkPipelineColorBlendStateCreateInfo colS = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &cb};

    // Vertex input (NodeInstance)
    VkVertexInputBindingDescription nb[] = {
        {0, sizeof(NodeInstance), VK_VERTEX_INPUT_RATE_INSTANCE}};
    VkVertexInputAttributeDescription na[] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(NodeInstance, pos)},
        {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT,
         offsetof(NodeInstance, color)},
        {2, 0, VK_FORMAT_R32_SFLOAT, offsetof(NodeInstance, size)},
        {3, 0, VK_FORMAT_R32_SINT, offsetof(NodeInstance, degree)}};
    VkPipelineVertexInputStateCreateInfo nvi = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = nb,
        .vertexAttributeDescriptionCount = 4,
        .pVertexAttributeDescriptions = na};

    VkPipelineInputAssemblyStateCreateInfo niAs = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};

    VkGraphicsPipelineCreateInfo pInfo = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = sstages,
        .pVertexInputState = &nvi,
        .pInputAssemblyState = &niAs,
        .pViewportState = &vpS,
        .pRasterizationState = &ras,
        .pMultisampleState = &mul,
        .pColorBlendState = &colS,
        .layout = r->pipelineLayout,
        .renderPass = r->renderPass,
        .pDynamicState = &dyn};

    VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                                                 &pInfo, NULL, &r->pipeline);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to create main graphics pipeline\n");
        return -1;
    }

    // =========================================================================
    // WIREFRAME PIPELINE (Node Edges)
    // =========================================================================
    VkPipelineRasterizationStateCreateInfo rasWire = ras;
    rasWire.polygonMode = VK_POLYGON_MODE_LINE;

    VkGraphicsPipelineCreateInfo wpInfo = pInfo;
    wpInfo.pRasterizationState = &rasWire;

    result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &wpInfo, NULL,
                                       &r->nodeEdgePipeline);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to create wireframe pipeline\n");
        return -1;
    }

    // =========================================================================
    // SPHERE PIPELINE (Transparent)
    // =========================================================================
    // test
    VkPipelineDepthStencilStateCreateInfo ds = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_FALSE,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL};

    VkPipelineRasterizationStateCreateInfo rasSphere = ras;
    rasSphere.cullMode =
        VK_CULL_MODE_BACK_BIT; // FIX: Prevent drawing the inside of spheres
    VkGraphicsPipelineCreateInfo spInfo = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = sstages,
        .pVertexInputState = &svi,
        .pInputAssemblyState = &niAs,
        .pViewportState = &vpS,
        .pRasterizationState = &rasSphere,
        .pMultisampleState = &mul,
        .pColorBlendState = &colS_trans,
        .pDepthStencilState = &ds,
        .layout = r->pipelineLayout,
        .renderPass = r->renderPass};
    result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &spInfo,
                                       NULL, &r->spherePipeline);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to create sphere pipeline\n");
        return -1;
    }

    vkDestroyShaderModule(device, fMod, NULL);
    vkDestroyShaderModule(device, vMod, NULL);

    return 0;
}
