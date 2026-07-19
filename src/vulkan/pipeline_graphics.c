/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "vulkan/pipeline_graphics.h"
#include "vulkan/renderer.h"
#include "vulkan/renderer_geometry.h"
#include "vulkan/utils.h"
#include <stddef.h>

void renderer_create_graphics_pipelines(Renderer *r)
{
	VkShaderModule nodeVertModule, nodeFragModule, edgeVertexShaderModule, edgeFragmentShaderModule, rayVertexShaderModule, rayFragmentShaderModule;
	VK_CHECK(create_shader_module(r->core.device, NODE_VERT_SHADER_PATH, &nodeVertModule), "Failed to create node vertex shader module");
	VK_CHECK(create_shader_module(r->core.device, NODE_FRAG_SHADER_PATH, &nodeFragModule), "Failed to create node fragment shader module");
	VK_CHECK(create_shader_module(r->core.device, EDGE_VERT_SHADER_PATH, &edgeVertexShaderModule), "Failed to create edge vertex shader module");
	VK_CHECK(create_shader_module(r->core.device, EDGE_FRAG_SHADER_PATH, &edgeFragmentShaderModule), "Failed to create edge fragment shader module");
	VK_CHECK(create_shader_module(r->core.device, RAY_VERT_SHADER_PATH, &rayVertexShaderModule), "Failed to create ray vertex shader module");
	VK_CHECK(create_shader_module(r->core.device, RAY_FRAG_SHADER_PATH, &rayFragmentShaderModule), "Failed to create ray fragment shader module");

	VK_PIPELINE_COMMON_STATE_DECLS();
	VkPipelineDepthStencilStateCreateInfo nodeDepthStencilState = VK_DEPTH_STENCIL_STATE_TEST_WRITE_LESS;

	// Nodes
	VkPipelineShaderStageCreateInfo nodeShaderStages[] = {VK_SHADER_STAGE_VERT(nodeVertModule), VK_SHADER_STAGE_FRAG(nodeFragModule)};
	VkVertexInputBindingDescription nodeBindings[] = {{0, 3 * sizeof(float), VK_VERTEX_INPUT_RATE_VERTEX}, {1, sizeof(NodePosition), VK_VERTEX_INPUT_RATE_INSTANCE}, {2, sizeof(NodeAttribute), VK_VERTEX_INPUT_RATE_INSTANCE}, {3, sizeof(NodePosition), VK_VERTEX_INPUT_RATE_INSTANCE}};
	VkVertexInputAttributeDescription nodeAttributes[] = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, {1, 1, VK_FORMAT_R32G32B32_SFLOAT, 0}, {2, 2, VK_FORMAT_R32G32B32_SFLOAT, 0}, {3, 2, VK_FORMAT_R32_SFLOAT, 12}, {4, 2, VK_FORMAT_R32_SINT, 16}, {5, 2, VK_FORMAT_R32_SFLOAT, 20}, {6, 2, VK_FORMAT_R32_SFLOAT, 24}, {7, 3, VK_FORMAT_R32G32B32_SFLOAT, 0}};
	VkPipelineVertexInputStateCreateInfo nodeVertexInput = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, .vertexBindingDescriptionCount = 4, .pVertexBindingDescriptions = nodeBindings, .vertexAttributeDescriptionCount = 8, .pVertexAttributeDescriptions = nodeAttributes};
	VkPipelineInputAssemblyStateCreateInfo nodeInputAssembly = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
	VkGraphicsPipelineCreateInfo nodePipelineInfo = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = 2, .pStages = nodeShaderStages, .pVertexInputState = &nodeVertexInput, .pInputAssemblyState = &nodeInputAssembly, .pViewportState = &viewportState, .pRasterizationState = &rasterizationState, .pMultisampleState = &multisampleState, .pColorBlendState = &colorBlendState, .pDepthStencilState = &nodeDepthStencilState, .pDynamicState = &dynamicState, .layout = r->pipelineLayout, .renderPass = r->renderPass.renderPass};
	VK_CHECK(vkCreateGraphicsPipelines(r->core.device, VK_NULL_HANDLE, 1, &nodePipelineInfo, NULL, &r->pipelines.node), "Failed to create node graphics pipeline");

	// Edges
	VkPipelineShaderStageCreateInfo edgeShaderStages[] = {VK_SHADER_STAGE_VERT(edgeVertexShaderModule), VK_SHADER_STAGE_FRAG(edgeFragmentShaderModule)};
	VkVertexInputBindingDescription edgeBindings[] = {{0, sizeof(EdgePosition), VK_VERTEX_INPUT_RATE_VERTEX}, {1, sizeof(EdgeAttribute), VK_VERTEX_INPUT_RATE_VERTEX}, {2, sizeof(EdgePosition), VK_VERTEX_INPUT_RATE_VERTEX}};
	VkVertexInputAttributeDescription edgeAttributes[] = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, {1, 1, VK_FORMAT_R32G32B32_SFLOAT, 0}, {2, 1, VK_FORMAT_R32_SFLOAT, 12}, {3, 1, VK_FORMAT_R32_SFLOAT, 16}, {4, 1, VK_FORMAT_R32_SFLOAT, 20}, {5, 2, VK_FORMAT_R32G32B32_SFLOAT, 0}};
	VkPipelineVertexInputStateCreateInfo edgeVertexInput = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, .vertexBindingDescriptionCount = 3, .pVertexBindingDescriptions = edgeBindings, .vertexAttributeDescriptionCount = 6, .pVertexAttributeDescriptions = edgeAttributes};
	VkPipelineInputAssemblyStateCreateInfo edgeInputAssembly = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST};
	VkGraphicsPipelineCreateInfo edgePipelineInfo = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = 2, .pStages = edgeShaderStages, .pVertexInputState = &edgeVertexInput, .pInputAssemblyState = &edgeInputAssembly, .pViewportState = &viewportState, .pRasterizationState = &rasterizationState, .pMultisampleState = &multisampleState, .pColorBlendState = &colorBlendState, .pDepthStencilState = &nodeDepthStencilState, .pDynamicState = &dynamicState, .layout = r->pipelineLayout, .renderPass = r->renderPass.renderPass};
	VK_CHECK(vkCreateGraphicsPipelines(r->core.device, VK_NULL_HANDLE, 1, &edgePipelineInfo, NULL, &r->pipelines.edge), "Failed to create edge graphics pipeline");

	// Ray
	VkPipelineShaderStageCreateInfo rayShaderStages[] = {VK_SHADER_STAGE_VERT(rayVertexShaderModule), VK_SHADER_STAGE_FRAG(rayFragmentShaderModule)};
	VkVertexInputBindingDescription rayBindings = {0, sizeof(float) * 7, VK_VERTEX_INPUT_RATE_VERTEX};
	VkVertexInputAttributeDescription rayAttributes[] = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 12}};
	VkPipelineVertexInputStateCreateInfo rayVertexInput = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &rayBindings, .vertexAttributeDescriptionCount = 2, .pVertexAttributeDescriptions = rayAttributes};
	VkGraphicsPipelineCreateInfo rayPipelineInfo = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = 2, .pStages = rayShaderStages, .pVertexInputState = &rayVertexInput, .pInputAssemblyState = &edgeInputAssembly, .pViewportState = &viewportState, .pRasterizationState = &rasterizationState, .pMultisampleState = &multisampleState, .pColorBlendState = &colorBlendState, .pDepthStencilState = &nodeDepthStencilState, .pDynamicState = &dynamicState, .layout = r->pipelineLayout, .renderPass = r->renderPass.renderPass};
	VK_CHECK(vkCreateGraphicsPipelines(r->core.device, VK_NULL_HANDLE, 1, &rayPipelineInfo, NULL, &r->pipelines.ray), "Failed to create ray graphics pipeline");

	vkDestroyShaderModule(r->core.device, rayFragmentShaderModule, NULL);
	vkDestroyShaderModule(r->core.device, rayVertexShaderModule, NULL);
	vkDestroyShaderModule(r->core.device, edgeFragmentShaderModule, NULL);
	vkDestroyShaderModule(r->core.device, edgeVertexShaderModule, NULL);
	vkDestroyShaderModule(r->core.device, nodeFragModule, NULL);
	vkDestroyShaderModule(r->core.device, nodeVertModule, NULL);
}
