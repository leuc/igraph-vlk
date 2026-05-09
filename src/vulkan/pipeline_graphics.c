#include "vulkan/pipeline_graphics.h"
#include "vulkan/renderer.h"
#include "vulkan/renderer_geometry.h"
#include "vulkan/utils.h"
#include <stddef.h>

void renderer_create_graphics_pipelines(Renderer *r)
{
	VkShaderModule vertexShaderModule, fragmentShaderModule, edgeVertexShaderModule, edgeFragmentShaderModule, sphereVertexShaderModule, sphereFragmentShaderModule, rayVertexShaderModule, rayFragmentShaderModule;
	create_shader_module(r->core.device, VERT_SHADER_PATH, &vertexShaderModule);
	create_shader_module(r->core.device, FRAG_SHADER_PATH, &fragmentShaderModule);
	create_shader_module(r->core.device, EDGE_VERT_SHADER_PATH, &edgeVertexShaderModule);
	create_shader_module(r->core.device, EDGE_FRAG_SHADER_PATH, &edgeFragmentShaderModule);
	create_shader_module(r->core.device, SPHERE_VERT_SHADER_PATH, &sphereVertexShaderModule);
	create_shader_module(r->core.device, SPHERE_FRAG_SHADER_PATH, &sphereFragmentShaderModule);
	create_shader_module(r->core.device, RAY_VERT_SHADER_PATH, &rayVertexShaderModule);
	create_shader_module(r->core.device, RAY_FRAG_SHADER_PATH, &rayFragmentShaderModule);

	VkPipelineViewportStateCreateInfo viewportState = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1};
	VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
	VkPipelineDynamicStateCreateInfo dynamicState = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, .dynamicStateCount = 2, .pDynamicStates = dynamicStates};
	VkPipelineRasterizationStateCreateInfo rasterizationState = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, .polygonMode = VK_POLYGON_MODE_FILL, .lineWidth = 1.0f, .cullMode = VK_CULL_MODE_NONE, .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE};
	VkPipelineMultisampleStateCreateInfo multisampleState = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
	VkPipelineColorBlendAttachmentState colorBlendAttachment = {.colorWriteMask = 0xF, .blendEnable = VK_TRUE, .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA, .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, .colorBlendOp = VK_BLEND_OP_ADD, .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE, .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO, .alphaBlendOp = VK_BLEND_OP_ADD};
	VkPipelineColorBlendStateCreateInfo colorBlendState = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1, .pAttachments = &colorBlendAttachment};
	VkPipelineDepthStencilStateCreateInfo nodeDepthStencilState = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, .depthTestEnable = VK_TRUE, .depthWriteEnable = VK_TRUE, .depthCompareOp = VK_COMPARE_OP_LESS};

	// Nodes
	VkPipelineShaderStageCreateInfo nodeShaderStages[] = {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, vertexShaderModule, "main", NULL}, {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragmentShaderModule, "main", NULL}};
	VkVertexInputBindingDescription nodeBindings[] = {{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX}, {1, sizeof(NodePosition), VK_VERTEX_INPUT_RATE_INSTANCE}, {2, sizeof(NodeAttribute), VK_VERTEX_INPUT_RATE_INSTANCE}};
	VkVertexInputAttributeDescription nodeAttributes[] = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12}, {2, 0, VK_FORMAT_R32_SFLOAT, 24}, {3, 1, VK_FORMAT_R32G32B32_SFLOAT, 0}, {4, 2, VK_FORMAT_R32G32B32_SFLOAT, 0}, {5, 2, VK_FORMAT_R32_SFLOAT, 12}, {6, 2, VK_FORMAT_R32_SINT, 16}, {7, 2, VK_FORMAT_R32_SFLOAT, 20}, {8, 2, VK_FORMAT_R32_SFLOAT, 24}};
	VkPipelineVertexInputStateCreateInfo nodeVertexInput = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, .vertexBindingDescriptionCount = 3, .pVertexBindingDescriptions = nodeBindings, .vertexAttributeDescriptionCount = 9, .pVertexAttributeDescriptions = nodeAttributes};
	VkPipelineInputAssemblyStateCreateInfo nodeInputAssembly = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
	VkGraphicsPipelineCreateInfo nodePipelineInfo = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = 2, .pStages = nodeShaderStages, .pVertexInputState = &nodeVertexInput, .pInputAssemblyState = &nodeInputAssembly, .pViewportState = &viewportState, .pRasterizationState = &rasterizationState, .pMultisampleState = &multisampleState, .pColorBlendState = &colorBlendState, .pDepthStencilState = &nodeDepthStencilState, .pDynamicState = &dynamicState, .layout = r->pipelineLayout, .renderPass = r->renderPass.renderPass};
	VK_CHECK(vkCreateGraphicsPipelines(r->core.device, VK_NULL_HANDLE, 1, &nodePipelineInfo, NULL, &r->graphicsPipeline), "Failed to create node graphics pipeline");

	// Spheres
	VkPipelineShaderStageCreateInfo sphereShaderStages[] = {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, sphereVertexShaderModule, "main", NULL}, {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, sphereFragmentShaderModule, "main", NULL}};
	VkVertexInputBindingDescription sphereBindings[] = {{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX}};
	VkVertexInputAttributeDescription sphereAttributes[] = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12}, {2, 0, VK_FORMAT_R32G32_SFLOAT, 24}};
	VkPipelineVertexInputStateCreateInfo sphereVertexInput = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = sphereBindings, .vertexAttributeDescriptionCount = 3, .pVertexAttributeDescriptions = sphereAttributes};
	VkPipelineDepthStencilStateCreateInfo sphereDepthStencilState = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, .depthTestEnable = VK_TRUE, .depthWriteEnable = VK_FALSE, .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL};
	VkPipelineRasterizationStateCreateInfo sphereRasterizationState = rasterizationState;
	sphereRasterizationState.cullMode = VK_CULL_MODE_BACK_BIT;
	VkGraphicsPipelineCreateInfo spherePipelineInfo = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = 2, .pStages = sphereShaderStages, .pVertexInputState = &sphereVertexInput, .pInputAssemblyState = &nodeInputAssembly, .pViewportState = &viewportState, .pRasterizationState = &sphereRasterizationState, .pMultisampleState = &multisampleState, .pColorBlendState = &colorBlendState, .pDepthStencilState = &sphereDepthStencilState, .pDynamicState = &dynamicState, .layout = r->pipelineLayout, .renderPass = r->renderPass.renderPass};
	VK_CHECK(vkCreateGraphicsPipelines(r->core.device, VK_NULL_HANDLE, 1, &spherePipelineInfo, NULL, &r->spherePipeline), "Failed to create sphere graphics pipeline");

	// Edges
	VkPipelineShaderStageCreateInfo edgeShaderStages[] = {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, edgeVertexShaderModule, "main", NULL}, {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, edgeFragmentShaderModule, "main", NULL}};
	VkVertexInputBindingDescription edgeBindings[] = {{0, sizeof(EdgePosition), VK_VERTEX_INPUT_RATE_VERTEX}, {1, sizeof(EdgeAttribute), VK_VERTEX_INPUT_RATE_VERTEX}};
	VkVertexInputAttributeDescription edgeAttributes[] = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, {1, 1, VK_FORMAT_R32G32B32_SFLOAT, 0}, {2, 1, VK_FORMAT_R32_SFLOAT, 12}, {3, 1, VK_FORMAT_R32_SFLOAT, 16}, {4, 1, VK_FORMAT_R32_SFLOAT, 20}, {5, 1, VK_FORMAT_R32_SINT, 24}, {6, 1, VK_FORMAT_R32_SINT, 28}, {7, 1, VK_FORMAT_R32_SFLOAT, 32}};
	VkPipelineVertexInputStateCreateInfo edgeVertexInput = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, .vertexBindingDescriptionCount = 2, .pVertexBindingDescriptions = edgeBindings, .vertexAttributeDescriptionCount = 8, .pVertexAttributeDescriptions = edgeAttributes};
	VkPipelineInputAssemblyStateCreateInfo edgeInputAssembly = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST};
	VkGraphicsPipelineCreateInfo edgePipelineInfo = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = 2, .pStages = edgeShaderStages, .pVertexInputState = &edgeVertexInput, .pInputAssemblyState = &edgeInputAssembly, .pViewportState = &viewportState, .pRasterizationState = &rasterizationState, .pMultisampleState = &multisampleState, .pColorBlendState = &colorBlendState, .pDepthStencilState = &nodeDepthStencilState, .pDynamicState = &dynamicState, .layout = r->pipelineLayout, .renderPass = r->renderPass.renderPass};
	VK_CHECK(vkCreateGraphicsPipelines(r->core.device, VK_NULL_HANDLE, 1, &edgePipelineInfo, NULL, &r->edgePipeline), "Failed to create edge graphics pipeline");

	// Ray
	VkPipelineShaderStageCreateInfo rayShaderStages[] = {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, rayVertexShaderModule, "main", NULL}, {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, rayFragmentShaderModule, "main", NULL}};
	VkVertexInputBindingDescription rayBindings = {0, sizeof(float) * 7, VK_VERTEX_INPUT_RATE_VERTEX};
	VkVertexInputAttributeDescription rayAttributes[] = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 12}};
	VkPipelineVertexInputStateCreateInfo rayVertexInput = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &rayBindings, .vertexAttributeDescriptionCount = 2, .pVertexAttributeDescriptions = rayAttributes};
	VkGraphicsPipelineCreateInfo rayPipelineInfo = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = 2, .pStages = rayShaderStages, .pVertexInputState = &rayVertexInput, .pInputAssemblyState = &edgeInputAssembly, .pViewportState = &viewportState, .pRasterizationState = &rasterizationState, .pMultisampleState = &multisampleState, .pColorBlendState = &colorBlendState, .pDepthStencilState = &nodeDepthStencilState, .pDynamicState = &dynamicState, .layout = r->pipelineLayout, .renderPass = r->renderPass.renderPass};
	VK_CHECK(vkCreateGraphicsPipelines(r->core.device, VK_NULL_HANDLE, 1, &rayPipelineInfo, NULL, &r->rayPipeline), "Failed to create ray graphics pipeline");

	vkDestroyShaderModule(r->core.device, rayFragmentShaderModule, NULL);
	vkDestroyShaderModule(r->core.device, rayVertexShaderModule, NULL);
	vkDestroyShaderModule(r->core.device, sphereFragmentShaderModule, NULL);
	vkDestroyShaderModule(r->core.device, sphereVertexShaderModule, NULL);
	vkDestroyShaderModule(r->core.device, edgeFragmentShaderModule, NULL);
	vkDestroyShaderModule(r->core.device, edgeVertexShaderModule, NULL);
	vkDestroyShaderModule(r->core.device, fragmentShaderModule, NULL);
	vkDestroyShaderModule(r->core.device, vertexShaderModule, NULL);
}
