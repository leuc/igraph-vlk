#include "vulkan/pipeline_ui.h"

#include <stdio.h>
#include <stdlib.h>

#include "vulkan/renderer_geometry.h"
#include "vulkan/renderer_pipelines.h"
#include "vulkan/utils.h"

int pipeline_ui_create(Renderer *r) {
    VkDevice device = r->device;

    // =========================================================================
    // LABEL PIPELINE
    // =========================================================================
    VkShaderModule lVMod = VK_NULL_HANDLE, lfMod = VK_NULL_HANDLE;
    create_shader_module(device, LABEL_VERT_SHADER_PATH, &lVMod);
    create_shader_module(device, LABEL_FRAG_SHADER_PATH, &lfMod);

    VkPipelineShaderStageCreateInfo lstages[] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0,
         VK_SHADER_STAGE_VERTEX_BIT, lVMod, "main", NULL},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0,
         VK_SHADER_STAGE_FRAGMENT_BIT, lfMod, "main", NULL}};

    // Vertex input (LabelVertex + LabelInstance)
    VkVertexInputBindingDescription lb[] = {
        {0, sizeof(LabelVertex), VK_VERTEX_INPUT_RATE_VERTEX},
        {1, sizeof(LabelInstance), VK_VERTEX_INPUT_RATE_INSTANCE}};

    VkVertexInputAttributeDescription la[] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(LabelVertex, pos)},
        {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(LabelVertex, tex)},
        {2, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(LabelInstance, nodePos)},
        {3, 1, VK_FORMAT_R32G32B32A32_SFLOAT,
         offsetof(LabelInstance, charRect)},
        {4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(LabelInstance, charUV)}};
    VkPipelineVertexInputStateCreateInfo lvi = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 2,
        .pVertexBindingDescriptions = lb,
        .vertexAttributeDescriptionCount = 5,
        .pVertexAttributeDescriptions = la};

    VkPipelineColorBlendAttachmentState lcb = {
        .colorWriteMask = 0xF,
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD};
    VkPipelineColorBlendStateCreateInfo lcs = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &lcb};

    VkGraphicsPipelineCreateInfo lpInfo = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = lstages,
        .pVertexInputState = &lvi,
        .pInputAssemblyState = &lias,
        .pViewportState = &vpS,
        .pRasterizationState = &ras,
        .pMultisampleState = &mul,
        .pColorBlendState = &lcs,
        .layout = r->pipelineLayout,
        .renderPass = r->renderPass};

    VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                                                &lpInfo, NULL, &r->labelPipeline);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to create label pipeline\n");
        return -1;
    }

    // =========================================================================
    // UI PIPELINE
    // =========================================================================
    VkShaderModule uiVMod = VK_NULL_HANDLE, uiFMod = VK_NULL_HANDLE;
    create_shader_module(device, UI_VERT_SHADER_PATH, &uiVMod);
    create_shader_module(device, UI_FRAG_SHADER_PATH, &uiFMod);

    VkPipelineShaderStageCreateInfo uiStages[] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0,
         VK_SHADER_STAGE_VERTEX_BIT, uiVMod, "main", NULL},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0,
         VK_SHADER_STAGE_FRAGMENT_BIT, uiFMod, "main", NULL}};

    // Vertex input (UIVertex + UIInstance)
    VkVertexInputBindingDescription uib[] = {
        {0, sizeof(UIVertex), VK_VERTEX_INPUT_RATE_VERTEX},
        {1, sizeof(UIInstance), VK_VERTEX_INPUT_RATE_INSTANCE}};

    VkVertexInputAttributeDescription uia[] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(UIVertex, pos)},
        {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(UIVertex, tex)},
        {2, 1, VK_FORMAT_R32G32_SFLOAT, offsetof(UIInstance, screenPos)},
        {3, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(UIInstance, charRect)},
        {4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(UIInstance, charUV)},
        {5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(UIInstance, color)}};
    VkPipelineVertexInputStateCreateInfo uivi = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 2,
        .pVertexBindingDescriptions = uib,
        .vertexAttributeDescriptionCount = 6,
        .pVertexAttributeDescriptions = uia};

    VkGraphicsPipelineCreateInfo uiPInfo = pInfo; // Reuse basic config
    uiPInfo.stageCount = 2;
    uiPInfo.pStages = uiStages;
    uiPInfo.pVertexInputState = &uivi;

    result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &uiPInfo,
                                       NULL, &r->uiPipeline);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to create UI pipeline\n");
        return -1;
    }

    // Cleanup shader modules
    vkDestroyShaderModule(device, uiFMod, NULL);
    vkDestroyShaderModule(device, uiVMod, NULL);
    vkDestroyShaderModule(device, lfMod, NULL);
    vkDestroyShaderModule(device, lVMod, NULL);

    return 0;
}
