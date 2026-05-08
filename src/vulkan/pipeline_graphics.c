#include "vulkan/pipeline_graphics.h"
#include "vulkan/renderer.h"
#include "vulkan/renderer_geometry.h"
#include "vulkan/utils.h"
#include <stddef.h>

void renderer_create_graphics_pipelines(Renderer *r)
{
	VkShaderModule vMod, fMod, eVMod, efMod, svMod, sfMod, rayVMod, rayFMod;
	create_shader_module(r->core.device, VERT_SHADER_PATH, &vMod);
	create_shader_module(r->core.device, FRAG_SHADER_PATH, &fMod);
	create_shader_module(r->core.device, EDGE_VERT_SHADER_PATH, &eVMod);
	create_shader_module(r->core.device, EDGE_FRAG_SHADER_PATH, &efMod);
	create_shader_module(r->core.device, SPHERE_VERT_SHADER_PATH, &svMod);
	create_shader_module(r->core.device, SPHERE_FRAG_SHADER_PATH, &sfMod);
	create_shader_module(r->core.device, RAY_VERT_SHADER_PATH, &rayVMod);
	create_shader_module(r->core.device, RAY_FRAG_SHADER_PATH, &rayFMod);

	VkPipelineViewportStateCreateInfo vpS = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1};
	VkDynamicState dStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
	VkPipelineDynamicStateCreateInfo dynS = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, .dynamicStateCount = 2, .pDynamicStates = dStates};
	VkPipelineRasterizationStateCreateInfo ras = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, .polygonMode = VK_POLYGON_MODE_FILL, .lineWidth = 1.0f, .cullMode = VK_CULL_MODE_NONE, .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE};
	VkPipelineMultisampleStateCreateInfo mul = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
	VkPipelineColorBlendAttachmentState colB = {.colorWriteMask = 0xF, .blendEnable = VK_TRUE, .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA, .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, .colorBlendOp = VK_BLEND_OP_ADD, .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE, .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO, .alphaBlendOp = VK_BLEND_OP_ADD};
	VkPipelineColorBlendStateCreateInfo colS = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1, .pAttachments = &colB};
	VkPipelineDepthStencilStateCreateInfo nodeDS = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, .depthTestEnable = VK_TRUE, .depthWriteEnable = VK_TRUE, .depthCompareOp = VK_COMPARE_OP_LESS};

	// Nodes
	VkPipelineShaderStageCreateInfo nStages[] = {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, vMod, "main", NULL}, {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fMod, "main", NULL}};
	VkVertexInputBindingDescription nb[] = {{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX}, {1, sizeof(NodePosition), VK_VERTEX_INPUT_RATE_INSTANCE}, {2, sizeof(NodeAttribute), VK_VERTEX_INPUT_RATE_INSTANCE}};
	VkVertexInputAttributeDescription na[] = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12}, {2, 0, VK_FORMAT_R32_SFLOAT, 24}, {3, 1, VK_FORMAT_R32G32B32_SFLOAT, 0}, {4, 2, VK_FORMAT_R32G32B32_SFLOAT, 0}, {5, 2, VK_FORMAT_R32_SFLOAT, 12}, {6, 2, VK_FORMAT_R32_SINT, 16}, {7, 2, VK_FORMAT_R32_SFLOAT, 20}, {8, 2, VK_FORMAT_R32_SFLOAT, 24}};
	VkPipelineVertexInputStateCreateInfo nvi = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, .vertexBindingDescriptionCount = 3, .pVertexBindingDescriptions = nb, .vertexAttributeDescriptionCount = 9, .pVertexAttributeDescriptions = na};
	VkPipelineInputAssemblyStateCreateInfo niAs = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
	VkGraphicsPipelineCreateInfo pInfo = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = 2, .pStages = nStages, .pVertexInputState = &nvi, .pInputAssemblyState = &niAs, .pViewportState = &vpS, .pRasterizationState = &ras, .pMultisampleState = &mul, .pColorBlendState = &colS, .pDepthStencilState = &nodeDS, .pDynamicState = &dynS, .layout = r->pipelineLayout, .renderPass = r->renderPass.renderPass};
	vkCreateGraphicsPipelines(r->core.device, VK_NULL_HANDLE, 1, &pInfo, NULL, &r->graphicsPipeline);

	// Spheres
	VkPipelineShaderStageCreateInfo sStages[] = {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, svMod, "main", NULL}, {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, sfMod, "main", NULL}};
	VkVertexInputBindingDescription sb[] = {{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX}};
	VkVertexInputAttributeDescription sa[] = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12}, {2, 0, VK_FORMAT_R32G32_SFLOAT, 24}};
	VkPipelineVertexInputStateCreateInfo svi = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = sb, .vertexAttributeDescriptionCount = 3, .pVertexAttributeDescriptions = sa};
	VkPipelineDepthStencilStateCreateInfo ds = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, .depthTestEnable = VK_TRUE, .depthWriteEnable = VK_FALSE, .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL};
	VkPipelineRasterizationStateCreateInfo rasS = ras;
	rasS.cullMode = VK_CULL_MODE_BACK_BIT;
	VkGraphicsPipelineCreateInfo spInfo = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = 2, .pStages = sStages, .pVertexInputState = &svi, .pInputAssemblyState = &niAs, .pViewportState = &vpS, .pRasterizationState = &rasS, .pMultisampleState = &mul, .pColorBlendState = &colS, .pDepthStencilState = &ds, .pDynamicState = &dynS, .layout = r->pipelineLayout, .renderPass = r->renderPass.renderPass};
	vkCreateGraphicsPipelines(r->core.device, VK_NULL_HANDLE, 1, &spInfo, NULL, &r->spherePipeline);

	// Edges
	VkPipelineShaderStageCreateInfo eStages[] = {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, eVMod, "main", NULL}, {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, efMod, "main", NULL}};
	VkVertexInputBindingDescription eb[] = {{0, sizeof(EdgePosition), VK_VERTEX_INPUT_RATE_VERTEX}, {1, sizeof(EdgeAttribute), VK_VERTEX_INPUT_RATE_VERTEX}};
	VkVertexInputAttributeDescription ea[] = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, {1, 1, VK_FORMAT_R32G32B32_SFLOAT, 0}, {2, 1, VK_FORMAT_R32_SFLOAT, 12}, {3, 1, VK_FORMAT_R32_SFLOAT, 16}, {4, 1, VK_FORMAT_R32_SFLOAT, 20}, {5, 1, VK_FORMAT_R32_SINT, 24}, {6, 1, VK_FORMAT_R32_SINT, 28}, {7, 1, VK_FORMAT_R32_SFLOAT, 32}};
	VkPipelineVertexInputStateCreateInfo evi = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, .vertexBindingDescriptionCount = 2, .pVertexBindingDescriptions = eb, .vertexAttributeDescriptionCount = 8, .pVertexAttributeDescriptions = ea};
	VkPipelineInputAssemblyStateCreateInfo eia = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST};
	VkGraphicsPipelineCreateInfo epInfo = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = 2, .pStages = eStages, .pVertexInputState = &evi, .pInputAssemblyState = &eia, .pViewportState = &vpS, .pRasterizationState = &ras, .pMultisampleState = &mul, .pColorBlendState = &colS, .pDepthStencilState = &nodeDS, .pDynamicState = &dynS, .layout = r->pipelineLayout, .renderPass = r->renderPass.renderPass};
	vkCreateGraphicsPipelines(r->core.device, VK_NULL_HANDLE, 1, &epInfo, NULL, &r->edgePipeline);

	// Ray
	VkPipelineShaderStageCreateInfo rStages[] = {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, rayVMod, "main", NULL}, {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, rayFMod, "main", NULL}};
	VkVertexInputBindingDescription rb = {0, sizeof(float) * 7, VK_VERTEX_INPUT_RATE_VERTEX};
	VkVertexInputAttributeDescription ra[] = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 12}};
	VkPipelineVertexInputStateCreateInfo rvi = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &rb, .vertexAttributeDescriptionCount = 2, .pVertexAttributeDescriptions = ra};
	VkGraphicsPipelineCreateInfo rpInfo = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = 2, .pStages = rStages, .pVertexInputState = &rvi, .pInputAssemblyState = &eia, .pViewportState = &vpS, .pRasterizationState = &ras, .pMultisampleState = &mul, .pColorBlendState = &colS, .pDepthStencilState = &nodeDS, .pDynamicState = &dynS, .layout = r->pipelineLayout, .renderPass = r->renderPass.renderPass};
	vkCreateGraphicsPipelines(r->core.device, VK_NULL_HANDLE, 1, &rpInfo, NULL, &r->rayPipeline);

	vkDestroyShaderModule(r->core.device, rayFMod, NULL);
	vkDestroyShaderModule(r->core.device, rayVMod, NULL);
	vkDestroyShaderModule(r->core.device, sfMod, NULL);
	vkDestroyShaderModule(r->core.device, svMod, NULL);
	vkDestroyShaderModule(r->core.device, efMod, NULL);
	vkDestroyShaderModule(r->core.device, eVMod, NULL);
	vkDestroyShaderModule(r->core.device, fMod, NULL);
	vkDestroyShaderModule(r->core.device, vMod, NULL);
}
