/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "vulkan/pipeline_graphics.h"
#include "vulkan/renderer.h"
#include "vulkan/renderer_geometry.h"
#include "vulkan/utils.h"
#include <stddef.h>

void renderer_create_graphics_pipelines(Renderer *r, Pipelines *pipelines, VkRenderPass render_pass, bool linear_output)
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
	uint32_t linear_output_value = linear_output ? 1u : 0u;
	VkSpecializationMapEntry linear_output_entry = {.constantID = 0, .offset = 0, .size = sizeof(linear_output_value)};
	VkSpecializationInfo linear_output_info = {.mapEntryCount = 1, .pMapEntries = &linear_output_entry, .dataSize = sizeof(linear_output_value), .pData = &linear_output_value};

	// Nodes
	VkPipelineShaderStageCreateInfo nodeShaderStages[] = {VK_SHADER_STAGE_VERT(nodeVertModule), VK_SHADER_STAGE_FRAG(nodeFragModule)};
	nodeShaderStages[1].pSpecializationInfo = &linear_output_info;
	VkVertexInputBindingDescription nodeBindings[] = {{0, 3 * sizeof(float), VK_VERTEX_INPUT_RATE_VERTEX}, {1, sizeof(NodePosition), VK_VERTEX_INPUT_RATE_INSTANCE}, {2, sizeof(NodeAttribute), VK_VERTEX_INPUT_RATE_INSTANCE}, {3, sizeof(NodePosition), VK_VERTEX_INPUT_RATE_INSTANCE}};
	VkVertexInputAttributeDescription nodeAttributes[] = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, {1, 1, VK_FORMAT_R32G32B32_SFLOAT, 0}, {2, 2, VK_FORMAT_R32G32B32_SFLOAT, offsetof(NodeAttribute, sdr_srgb)}, {3, 2, VK_FORMAT_R32G32B32_SFLOAT, offsetof(NodeAttribute, hdr_linear_bt2020)}, {4, 2, VK_FORMAT_R32_SFLOAT, offsetof(NodeAttribute, size)}, {5, 2, VK_FORMAT_R32_SINT, offsetof(NodeAttribute, degree)}, {6, 2, VK_FORMAT_R32_SFLOAT, offsetof(NodeAttribute, selected)}, {7, 2, VK_FORMAT_R32_SFLOAT, offsetof(NodeAttribute, visible)}, {8, 3, VK_FORMAT_R32G32B32_SFLOAT, 0}};
	VkPipelineVertexInputStateCreateInfo nodeVertexInput = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, .vertexBindingDescriptionCount = 4, .pVertexBindingDescriptions = nodeBindings, .vertexAttributeDescriptionCount = 9, .pVertexAttributeDescriptions = nodeAttributes};
	VkPipelineInputAssemblyStateCreateInfo nodeInputAssembly = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
	VkGraphicsPipelineCreateInfo nodePipelineInfo = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = 2, .pStages = nodeShaderStages, .pVertexInputState = &nodeVertexInput, .pInputAssemblyState = &nodeInputAssembly, .pViewportState = &viewportState, .pRasterizationState = &rasterizationState, .pMultisampleState = &multisampleState, .pColorBlendState = &colorBlendState, .pDepthStencilState = &nodeDepthStencilState, .pDynamicState = &dynamicState, .layout = r->pipelineLayout, .renderPass = render_pass};
	VK_CHECK(vkCreateGraphicsPipelines(r->core.device, VK_NULL_HANDLE, 1, &nodePipelineInfo, NULL, &pipelines->node), "Failed to create node graphics pipeline");

	// Edges
	VkPipelineShaderStageCreateInfo edgeShaderStages[] = {VK_SHADER_STAGE_VERT(edgeVertexShaderModule), VK_SHADER_STAGE_FRAG(edgeFragmentShaderModule)};
	edgeShaderStages[1].pSpecializationInfo = &linear_output_info;
	VkVertexInputBindingDescription edgeBindings[] = {{0, sizeof(EdgePosition), VK_VERTEX_INPUT_RATE_VERTEX}, {1, sizeof(EdgeAttribute), VK_VERTEX_INPUT_RATE_VERTEX}, {2, sizeof(EdgePosition), VK_VERTEX_INPUT_RATE_VERTEX}};
	VkVertexInputAttributeDescription edgeAttributes[] = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, {1, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(EdgeAttribute, sdr_srgb)}, {2, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(EdgeAttribute, hdr_linear_bt2020)}, {3, 1, VK_FORMAT_R32_SFLOAT, offsetof(EdgeAttribute, selected)}, {4, 1, VK_FORMAT_R32_SFLOAT, offsetof(EdgeAttribute, normalized_pos)}, {5, 1, VK_FORMAT_R32_SFLOAT, offsetof(EdgeAttribute, visible)}, {6, 2, VK_FORMAT_R32G32B32_SFLOAT, 0}, {7, 1, VK_FORMAT_R32_SFLOAT, offsetof(EdgeAttribute, alpha)}};
	VkPipelineVertexInputStateCreateInfo edgeVertexInput = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, .vertexBindingDescriptionCount = 3, .pVertexBindingDescriptions = edgeBindings, .vertexAttributeDescriptionCount = 8, .pVertexAttributeDescriptions = edgeAttributes};
	VkPipelineInputAssemblyStateCreateInfo edgeInputAssembly = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST};
	VkGraphicsPipelineCreateInfo edgePipelineInfo = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = 2, .pStages = edgeShaderStages, .pVertexInputState = &edgeVertexInput, .pInputAssemblyState = &edgeInputAssembly, .pViewportState = &viewportState, .pRasterizationState = &rasterizationState, .pMultisampleState = &multisampleState, .pColorBlendState = &colorBlendState, .pDepthStencilState = &nodeDepthStencilState, .pDynamicState = &dynamicState, .layout = r->pipelineLayout, .renderPass = render_pass};
	VK_CHECK(vkCreateGraphicsPipelines(r->core.device, VK_NULL_HANDLE, 1, &edgePipelineInfo, NULL, &pipelines->edge), "Failed to create edge graphics pipeline");

	// Ray
	VkPipelineShaderStageCreateInfo rayShaderStages[] = {VK_SHADER_STAGE_VERT(rayVertexShaderModule), VK_SHADER_STAGE_FRAG(rayFragmentShaderModule)};
	rayShaderStages[1].pSpecializationInfo = &linear_output_info;
	VkVertexInputBindingDescription rayBindings = {0, sizeof(float) * 7, VK_VERTEX_INPUT_RATE_VERTEX};
	VkVertexInputAttributeDescription rayAttributes[] = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 12}};
	VkPipelineVertexInputStateCreateInfo rayVertexInput = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &rayBindings, .vertexAttributeDescriptionCount = 2, .pVertexAttributeDescriptions = rayAttributes};
	VkGraphicsPipelineCreateInfo rayPipelineInfo = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = 2, .pStages = rayShaderStages, .pVertexInputState = &rayVertexInput, .pInputAssemblyState = &edgeInputAssembly, .pViewportState = &viewportState, .pRasterizationState = &rasterizationState, .pMultisampleState = &multisampleState, .pColorBlendState = &colorBlendState, .pDepthStencilState = &nodeDepthStencilState, .pDynamicState = &dynamicState, .layout = r->pipelineLayout, .renderPass = render_pass};
	VK_CHECK(vkCreateGraphicsPipelines(r->core.device, VK_NULL_HANDLE, 1, &rayPipelineInfo, NULL, &pipelines->ray), "Failed to create ray graphics pipeline");

	vkDestroyShaderModule(r->core.device, rayFragmentShaderModule, NULL);
	vkDestroyShaderModule(r->core.device, rayVertexShaderModule, NULL);
	vkDestroyShaderModule(r->core.device, edgeFragmentShaderModule, NULL);
	vkDestroyShaderModule(r->core.device, edgeVertexShaderModule, NULL);
	vkDestroyShaderModule(r->core.device, nodeFragModule, NULL);
	vkDestroyShaderModule(r->core.device, nodeVertModule, NULL);
}
