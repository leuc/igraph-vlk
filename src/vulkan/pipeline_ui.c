#include "vulkan/pipeline_ui.h"
#include "vulkan/renderer.h"
#include "vulkan/renderer_ui.h"
#include "vulkan/utils.h"
#include <stddef.h>

void renderer_create_ui_pipelines(Renderer *r)
{
	VkShaderModule uiVMod, uiFMod, lVMod, lfMod, menuVMod, menuFMod;
	create_shader_module(r->core.device, UI_VERT_SHADER_PATH, &uiVMod);
	create_shader_module(r->core.device, UI_FRAG_SHADER_PATH, &uiFMod);
	create_shader_module(r->core.device, LABEL_VERT_SHADER_PATH, &lVMod);
	create_shader_module(r->core.device, LABEL_FRAG_SHADER_PATH, &lfMod);
	create_shader_module(r->core.device, MENU_VERT_SHADER_PATH, &menuVMod);
	create_shader_module(r->core.device, MENU_FRAG_SHADER_PATH, &menuFMod);

	VkPipelineViewportStateCreateInfo vpS = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1};
	VkDynamicState dStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
	VkPipelineDynamicStateCreateInfo dynS = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, .dynamicStateCount = 2, .pDynamicStates = dStates};
	VkPipelineRasterizationStateCreateInfo ras = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, .polygonMode = VK_POLYGON_MODE_FILL, .lineWidth = 1.0f, .cullMode = VK_CULL_MODE_NONE, .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE};
	VkPipelineMultisampleStateCreateInfo mul = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
	VkPipelineColorBlendAttachmentState colB = {.colorWriteMask = 0xF, .blendEnable = VK_TRUE, .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA, .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, .colorBlendOp = VK_BLEND_OP_ADD, .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE, .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO, .alphaBlendOp = VK_BLEND_OP_ADD};
	VkPipelineColorBlendStateCreateInfo colS = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1, .pAttachments = &colB};
	VkPipelineDepthStencilStateCreateInfo uiDS = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, .depthTestEnable = VK_FALSE, .depthWriteEnable = VK_FALSE, .depthCompareOp = VK_COMPARE_OP_ALWAYS};
	VkPipelineDepthStencilStateCreateInfo menuDS = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, .depthTestEnable = VK_TRUE, .depthWriteEnable = VK_FALSE, .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL};

	// UI
	VkPipelineShaderStageCreateInfo uiStages[] = {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, uiVMod, "main", NULL}, {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, uiFMod, "main", NULL}};
	VkVertexInputBindingDescription uib[] = {{0, sizeof(UIVertex), VK_VERTEX_INPUT_RATE_VERTEX}, {1, sizeof(UIInstance), VK_VERTEX_INPUT_RATE_INSTANCE}};
	VkVertexInputAttributeDescription uia[] = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(UIVertex, pos)}, {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(UIVertex, tex)}, {2, 1, VK_FORMAT_R32G32_SFLOAT, offsetof(UIInstance, screenPos)}, {3, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(UIInstance, charRect)}, {4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(UIInstance, charUV)}, {5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(UIInstance, color)}};
	VkPipelineVertexInputStateCreateInfo uivi = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, .vertexBindingDescriptionCount = 2, .pVertexBindingDescriptions = uib, .vertexAttributeDescriptionCount = 6, .pVertexAttributeDescriptions = uia};
	VkPipelineInputAssemblyStateCreateInfo lias = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP};
	VkGraphicsPipelineCreateInfo uiPInfo = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = 2, .pStages = uiStages, .pVertexInputState = &uivi, .pInputAssemblyState = &lias, .pViewportState = &vpS, .pRasterizationState = &ras, .pMultisampleState = &mul, .pColorBlendState = &colS, .pDepthStencilState = &uiDS, .pDynamicState = &dynS, .layout = r->pipelineLayout, .renderPass = r->renderPass.renderPass};
	vkCreateGraphicsPipelines(r->core.device, VK_NULL_HANDLE, 1, &uiPInfo, NULL, &r->uiPipeline);

	// Labels
	VkPipelineShaderStageCreateInfo lStages[] = {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, lVMod, "main", NULL}, {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, lfMod, "main", NULL}};
	VkVertexInputBindingDescription lb[] = {{0, sizeof(LabelVertex), VK_VERTEX_INPUT_RATE_VERTEX}, {1, sizeof(LabelInstance), VK_VERTEX_INPUT_RATE_INSTANCE}};
	VkVertexInputAttributeDescription la[] = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(LabelVertex, pos)}, {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(LabelVertex, tex)}, {2, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(LabelInstance, nodePos)}, {3, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(LabelInstance, charRect)}, {4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(LabelInstance, charUV)}, {5, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(LabelInstance, right)}, {6, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(LabelInstance, up)}};
	VkPipelineVertexInputStateCreateInfo lvi = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, .vertexBindingDescriptionCount = 2, .pVertexBindingDescriptions = lb, .vertexAttributeDescriptionCount = 7, .pVertexAttributeDescriptions = la};
	VkGraphicsPipelineCreateInfo lpInfo = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = 2, .pStages = lStages, .pVertexInputState = &lvi, .pInputAssemblyState = &lias, .pViewportState = &vpS, .pRasterizationState = &ras, .pMultisampleState = &mul, .pColorBlendState = &colS, .pDepthStencilState = &menuDS, .pDynamicState = &dynS, .layout = r->pipelineLayout, .renderPass = r->renderPass.renderPass};
	vkCreateGraphicsPipelines(r->core.device, VK_NULL_HANDLE, 1, &lpInfo, NULL, &r->labelPipeline);

	// Menu
	VkPipelineShaderStageCreateInfo menuStages[] = {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, menuVMod, "main", NULL}, {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, menuFMod, "main", NULL}};
	VkVertexInputBindingDescription menuB[] = {{0, sizeof(QuadVertex), VK_VERTEX_INPUT_RATE_VERTEX}, {1, sizeof(MenuInstance), VK_VERTEX_INPUT_RATE_INSTANCE}};
	VkVertexInputAttributeDescription menuA[] = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(QuadVertex, pos)}, {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(QuadVertex, tex)}, {2, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MenuInstance, worldPos)}, {3, 1, VK_FORMAT_R32G32_SFLOAT, offsetof(MenuInstance, texCoord)}, {4, 1, VK_FORMAT_R32_SFLOAT, offsetof(MenuInstance, texId)}, {5, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MenuInstance, scale)}, {6, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(MenuInstance, rotation)}, {7, 1, VK_FORMAT_R32_SFLOAT, offsetof(MenuInstance, hovered)}};
	VkPipelineVertexInputStateCreateInfo menuVI = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, .vertexBindingDescriptionCount = 2, .pVertexBindingDescriptions = menuB, .vertexAttributeDescriptionCount = 8, .pVertexAttributeDescriptions = menuA};
	VkPipelineInputAssemblyStateCreateInfo menuIA = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
	VkGraphicsPipelineCreateInfo menuPInfo = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = 2, .pStages = menuStages, .pVertexInputState = &menuVI, .pInputAssemblyState = &menuIA, .pViewportState = &vpS, .pRasterizationState = &ras, .pMultisampleState = &mul, .pColorBlendState = &colS, .pDepthStencilState = &menuDS, .pDynamicState = &dynS, .layout = r->pipelineLayout, .renderPass = r->renderPass.renderPass};
	vkCreateGraphicsPipelines(r->core.device, VK_NULL_HANDLE, 1, &menuPInfo, NULL, &r->menuPipeline);

	vkDestroyShaderModule(r->core.device, menuFMod, NULL);
	vkDestroyShaderModule(r->core.device, menuVMod, NULL);
	vkDestroyShaderModule(r->core.device, lfMod, NULL);
	vkDestroyShaderModule(r->core.device, lVMod, NULL);
	vkDestroyShaderModule(r->core.device, uiFMod, NULL);
	vkDestroyShaderModule(r->core.device, uiVMod, NULL);
}
