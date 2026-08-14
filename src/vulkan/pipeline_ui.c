/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "vulkan/pipeline_ui.h"
#include "vulkan/renderer.h"
#include "vulkan/renderer_ui.h"
#include "vulkan/utils.h"
#include <stddef.h>

void renderer_create_ui_pipelines(Renderer *r, Pipelines *pipelines, VkRenderPass render_pass, bool linear_output)
{
	VkShaderModule uiVertexShaderModule, uiFragmentShaderModule, labelVertexShaderModule, labelFragmentShaderModule, menuVertexShaderModule, menuFragmentShaderModule, textQuadVertexShaderModule, textQuadFragmentShaderModule;
	VK_CHECK(create_shader_module(r->core.device, UI_VERT_SHADER_PATH, &uiVertexShaderModule), "Failed to create UI vertex shader module");
	VK_CHECK(create_shader_module(r->core.device, UI_FRAG_SHADER_PATH, &uiFragmentShaderModule), "Failed to create UI fragment shader module");
	VK_CHECK(create_shader_module(r->core.device, LABEL_VERT_SHADER_PATH, &labelVertexShaderModule), "Failed to create label vertex shader module");
	VK_CHECK(create_shader_module(r->core.device, LABEL_FRAG_SHADER_PATH, &labelFragmentShaderModule), "Failed to create label fragment shader module");
	VK_CHECK(create_shader_module(r->core.device, MENU_VERT_SHADER_PATH, &menuVertexShaderModule), "Failed to create menu vertex shader module");
	VK_CHECK(create_shader_module(r->core.device, MENU_FRAG_SHADER_PATH, &menuFragmentShaderModule), "Failed to create menu fragment shader module");
	VK_CHECK(create_shader_module(r->core.device, TEXTQUAD_VERT_SHADER_PATH, &textQuadVertexShaderModule), "Failed to create text quad vertex shader module");
	VK_CHECK(create_shader_module(r->core.device, TEXTQUAD_FRAG_SHADER_PATH, &textQuadFragmentShaderModule), "Failed to create text quad fragment shader module");

	VK_PIPELINE_COMMON_STATE_DECLS();
	VkPipelineDepthStencilStateCreateInfo uiDepthStencilState = VK_DEPTH_STENCIL_STATE_DISABLED;
	VkPipelineDepthStencilStateCreateInfo menuDepthStencilState = VK_DEPTH_STENCIL_STATE_TEST_NO_WRITE_LESS_EQUAL;
	uint32_t linear_output_value = linear_output ? 1u : 0u;
	VkSpecializationMapEntry linear_output_entry = {.constantID = 0, .offset = 0, .size = sizeof(linear_output_value)};
	VkSpecializationInfo linear_output_info = {.mapEntryCount = 1, .pMapEntries = &linear_output_entry, .dataSize = sizeof(linear_output_value), .pData = &linear_output_value};

	// UI
	VkPipelineShaderStageCreateInfo uiShaderStages[] = {VK_SHADER_STAGE_VERT(uiVertexShaderModule), VK_SHADER_STAGE_FRAG(uiFragmentShaderModule)};
	uiShaderStages[1].pSpecializationInfo = &linear_output_info;
	VkVertexInputBindingDescription uiBindings[] = {{0, sizeof(UIVertex), VK_VERTEX_INPUT_RATE_VERTEX}, {1, sizeof(UIInstance), VK_VERTEX_INPUT_RATE_INSTANCE}};
	VkVertexInputAttributeDescription uiAttributes[] = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(UIVertex, pos)}, {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(UIVertex, tex)}, {2, 1, VK_FORMAT_R32G32_SFLOAT, offsetof(UIInstance, screenPos)}, {3, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(UIInstance, charRect)}, {4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(UIInstance, charUV)}, {5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(UIInstance, color)}};
	VkPipelineVertexInputStateCreateInfo uiVertexInput = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, .vertexBindingDescriptionCount = 2, .pVertexBindingDescriptions = uiBindings, .vertexAttributeDescriptionCount = 6, .pVertexAttributeDescriptions = uiAttributes};
	VkPipelineInputAssemblyStateCreateInfo labelInputAssembly = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP};
	VkGraphicsPipelineCreateInfo uiPipelineInfo = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = 2, .pStages = uiShaderStages, .pVertexInputState = &uiVertexInput, .pInputAssemblyState = &labelInputAssembly, .pViewportState = &viewportState, .pRasterizationState = &rasterizationState, .pMultisampleState = &multisampleState, .pColorBlendState = &colorBlendState, .pDepthStencilState = &uiDepthStencilState, .pDynamicState = &dynamicState, .layout = r->pipelineLayout, .renderPass = render_pass};
	VK_CHECK(vkCreateGraphicsPipelines(r->core.device, VK_NULL_HANDLE, 1, &uiPipelineInfo, NULL, &pipelines->ui), "Failed to create UI graphics pipeline");

	// Node Labels (single quad per label, surface-oriented, depth-writing opaque)
	VkPipelineShaderStageCreateInfo labelShaderStages[] = {VK_SHADER_STAGE_VERT(labelVertexShaderModule), VK_SHADER_STAGE_FRAG(labelFragmentShaderModule)};
	labelShaderStages[1].pSpecializationInfo = &linear_output_info;
	VkPipelineInputAssemblyStateCreateInfo labelInputAssemblyState = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
	VkVertexInputBindingDescription labelBindings[] = {{0, sizeof(QuadVertex), VK_VERTEX_INPUT_RATE_VERTEX}, {1, sizeof(NodeLabelInstance), VK_VERTEX_INPUT_RATE_INSTANCE}};
	VkVertexInputAttributeDescription labelAttributes[] = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(QuadVertex, pos)}, {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(QuadVertex, tex)}, {2, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(NodeLabelInstance, worldPos)}, {3, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(NodeLabelInstance, bgColor)}, {4, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(NodeLabelInstance, scale)}, {5, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(NodeLabelInstance, right)}, {6, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(NodeLabelInstance, up)}, {7, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(NodeLabelInstance, textUV)}, {8, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(NodeLabelInstance, textRegion)}};
	VkPipelineVertexInputStateCreateInfo labelVertexInput = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, .vertexBindingDescriptionCount = 2, .pVertexBindingDescriptions = labelBindings, .vertexAttributeDescriptionCount = 9, .pVertexAttributeDescriptions = labelAttributes};
	VkPipelineColorBlendAttachmentState labelBlendAttachment = {.colorWriteMask = 0xF, .blendEnable = VK_FALSE};
	VkPipelineColorBlendStateCreateInfo labelBlendState = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1, .pAttachments = &labelBlendAttachment};
	VkGraphicsPipelineCreateInfo labelPipelineInfo = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = 2, .pStages = labelShaderStages, .pVertexInputState = &labelVertexInput, .pInputAssemblyState = &labelInputAssemblyState, .pViewportState = &viewportState, .pRasterizationState = &rasterizationState, .pMultisampleState = &multisampleState, .pColorBlendState = &labelBlendState, .pDepthStencilState = &VK_DEPTH_STENCIL_STATE_TEST_WRITE_LESS, .pDynamicState = &dynamicState, .layout = r->pipelineLayout, .renderPass = render_pass};
	VK_CHECK(vkCreateGraphicsPipelines(r->core.device, VK_NULL_HANDLE, 1, &labelPipelineInfo, NULL, &pipelines->label), "Failed to create label graphics pipeline");

	// Menu
	VkPipelineShaderStageCreateInfo menuShaderStages[] = {VK_SHADER_STAGE_VERT(menuVertexShaderModule), VK_SHADER_STAGE_FRAG(menuFragmentShaderModule)};
	menuShaderStages[1].pSpecializationInfo = &linear_output_info;
	VkVertexInputBindingDescription menuBindings[] = {{0, sizeof(QuadVertex), VK_VERTEX_INPUT_RATE_VERTEX}, {1, sizeof(MenuInstance), VK_VERTEX_INPUT_RATE_INSTANCE}};
	VkVertexInputAttributeDescription menuAttributes[] = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(QuadVertex, pos)}, {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(QuadVertex, tex)}, {2, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MenuInstance, worldPos)}, {3, 1, VK_FORMAT_R32G32_SFLOAT, offsetof(MenuInstance, texCoord)}, {4, 1, VK_FORMAT_R32_SFLOAT, offsetof(MenuInstance, texId)}, {5, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MenuInstance, scale)}, {6, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(MenuInstance, rotation)}, {7, 1, VK_FORMAT_R32_SFLOAT, offsetof(MenuInstance, hovered)}};
	VkPipelineVertexInputStateCreateInfo menuVertexInput = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, .vertexBindingDescriptionCount = 2, .pVertexBindingDescriptions = menuBindings, .vertexAttributeDescriptionCount = 8, .pVertexAttributeDescriptions = menuAttributes};
	VkPipelineInputAssemblyStateCreateInfo menuInputAssembly = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
	VkGraphicsPipelineCreateInfo menuPipelineInfo = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = 2, .pStages = menuShaderStages, .pVertexInputState = &menuVertexInput, .pInputAssemblyState = &menuInputAssembly, .pViewportState = &viewportState, .pRasterizationState = &rasterizationState, .pMultisampleState = &multisampleState, .pColorBlendState = &colorBlendState, .pDepthStencilState = &menuDepthStencilState, .pDynamicState = &dynamicState, .layout = r->pipelineLayout, .renderPass = render_pass};
	VK_CHECK(vkCreateGraphicsPipelines(r->core.device, VK_NULL_HANDLE, 1, &menuPipelineInfo, NULL, &pipelines->menu), "Failed to create menu graphics pipeline");

	// Text Quad (generic: background color + text atlas compositing)
	VkPipelineShaderStageCreateInfo textQuadShaderStages[] = {VK_SHADER_STAGE_VERT(textQuadVertexShaderModule), VK_SHADER_STAGE_FRAG(textQuadFragmentShaderModule)};
	textQuadShaderStages[1].pSpecializationInfo = &linear_output_info;
	VkVertexInputBindingDescription textQuadBindings[] = {{0, sizeof(QuadVertex), VK_VERTEX_INPUT_RATE_VERTEX}, {1, sizeof(TextQuadInstance), VK_VERTEX_INPUT_RATE_INSTANCE}};
	VkVertexInputAttributeDescription textQuadAttributes[] = {
		{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(QuadVertex, pos)}, {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(QuadVertex, tex)}, {2, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(TextQuadInstance, worldPos)}, {3, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(TextQuadInstance, bgColor)}, {4, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(TextQuadInstance, scale)}, {5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(TextQuadInstance, rotation)}, {6, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(TextQuadInstance, textUV)}, {7, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(TextQuadInstance, textRegion)},
	};
	VkPipelineVertexInputStateCreateInfo textQuadVertexInput = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, .vertexBindingDescriptionCount = 2, .pVertexBindingDescriptions = textQuadBindings, .vertexAttributeDescriptionCount = 8, .pVertexAttributeDescriptions = textQuadAttributes};
	VkGraphicsPipelineCreateInfo textQuadPipelineInfo = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = 2, .pStages = textQuadShaderStages, .pVertexInputState = &textQuadVertexInput, .pInputAssemblyState = &menuInputAssembly, .pViewportState = &viewportState, .pRasterizationState = &rasterizationState, .pMultisampleState = &multisampleState, .pColorBlendState = &colorBlendState, .pDepthStencilState = &menuDepthStencilState, .pDynamicState = &dynamicState, .layout = r->pipelineLayout, .renderPass = render_pass};
	VK_CHECK(vkCreateGraphicsPipelines(r->core.device, VK_NULL_HANDLE, 1, &textQuadPipelineInfo, NULL, &pipelines->textQuad), "Failed to create text quad graphics pipeline");

	vkDestroyShaderModule(r->core.device, textQuadFragmentShaderModule, NULL);
	vkDestroyShaderModule(r->core.device, textQuadVertexShaderModule, NULL);
	vkDestroyShaderModule(r->core.device, menuFragmentShaderModule, NULL);
	vkDestroyShaderModule(r->core.device, menuVertexShaderModule, NULL);
	vkDestroyShaderModule(r->core.device, labelFragmentShaderModule, NULL);
	vkDestroyShaderModule(r->core.device, labelVertexShaderModule, NULL);
	vkDestroyShaderModule(r->core.device, uiFragmentShaderModule, NULL);
	vkDestroyShaderModule(r->core.device, uiVertexShaderModule, NULL);
}
