#include "vulkan/pipeline_ui.h"

#include <stdio.h>
#include <stdlib.h>

#include "vulkan/renderer_geometry.h"
#include "vulkan/renderer_pipelines.h"
#include "vulkan/utils.h"
#include "vulkan/renderer_ui.h"

int pipeline_ui_create(Renderer *r) {
    VkDevice device = r->device;

    // UI Pipeline for Labels (legacy)
    VkShaderModule lVMod = VK_NULL_HANDLE, lfMod = VK_NULL_HANDLE;
    create_shader_module(device, LABEL_VERT_SHADER_PATH, &lVMod);
    create_shader_module(device, LABEL_FRAG_SHADER_PATH, &lfMod);

    VkPipelineShaderStageCreateInfo lstages[] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0,
         VK_SHADER_STAGE_VERTEX_BIT, lVMod, "main", NULL},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0,
         VK_SHADER_STAGE_FRAGMENT_BIT, lfMod, "main", NULL}};

    // Vertex input for labels (keep legacy)
    VkVertexInputBindingDescription lb[] = {
        {0, sizeof(LabelVertex), VK_VERTEX_INPUT_RATE_VERTEX},
        {1, sizeof(LabelInstance), VK_VERTEX_INPUT_RATE_INSTANCE}};

    VkVertexInputAttributeDescription la[] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(LabelVertex, pos)},
        {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(LabelVertex, tex)},
        {2, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(LabelInstance, nodePos)},
        {3, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(LabelInstance, charRect)},
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

    vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &lpInfo, NULL, &r->labelPipeline);

    // Menu Instanced Pipeline (new)
    VkShaderModule mVMod = VK_NULL_HANDLE, mFMod = VK_NULL_HANDLE;
    create_shader_module(device, "shaders/ui.vert", &mVMod);
    create_shader_module(device, UI_FRAG_SHADER_PATH, &mFMod); // Reuse UI frag or new menu.frag

    VkPipelineShaderStageCreateInfo mstages[] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0,
         VK_SHADER_STAGE_VERTEX_BIT, mVMod, "main", NULL},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0,
         VK_SHADER_STAGE_FRAGMENT_BIT, mFMod, "main", NULL}};

    // Vertex input for menu: QuadVertex (binding 0, vertex rate) + MenuInstanceData (binding 1, instance rate)
    VkVertexInputBindingDescription mb[] = {
        {0, sizeof(QuadVertex), VK_VERTEX_INPUT_RATE_VERTEX},
        {1, sizeof(MenuInstanceData), VK_VERTEX_INPUT_RATE_INSTANCE}};

    VkVertexInputAttributeDescription ma[] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(QuadVertex, pos)},
        {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(QuadVertex, uv)},
        {2, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MenuInstanceData, position)},
        {3, 1, VK_FORMAT_R32_SFLOAT, offsetof(MenuInstanceData, scale)},
        {4, 1, VK_FORMAT_R32_SINT, offsetof(MenuInstanceData, icon_index)}};

    VkPipelineVertexInputStateCreateInfo mvi = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 2,
        .pVertexBindingDescriptions = mb,
        .vertexAttributeDescriptionCount = 5,
        .pVertexAttributeDescriptions = ma};

    VkPipelineColorBlendAttachmentState mcb = lcb; // Same blending
    VkPipelineColorBlendStateCreateInfo mcs = lcs;

    VkGraphicsPipelineCreateInfo mpInfo = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = mstages,
        .pVertexInputState = &mvi,
        .pInputAssemblyState = &lias,
        .pViewportState = &vpS,
        .pRasterizationState = &ras,
        .pMultisampleState = &mul,
        .pColorBlendState = &mcs,
        .layout = r->pipelineLayout,
        .renderPass = r->renderPass};

    vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &mpInfo, NULL, &r->menuPipeline); // Assume r->menuPipeline added to Renderer

    // Cleanup
    vkDestroyShaderModule(device, mFMod, NULL);
    vkDestroyShaderModule(device, mVMod, NULL);
    vkDestroyShaderModule(device, lfMod, NULL);
    vkDestroyShaderModule(device, lVMod, NULL);

    return 0;
}
