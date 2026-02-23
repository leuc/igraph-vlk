#include "renderer_pipelines.h"
#include "renderer_geometry.h"
#include "vulkan_utils.h"
#include <stddef.h>
#include <stdlib.h>

int renderer_create_pipelines(Renderer *r) {
	VkShaderModule vMod, fMod, eVMod, efMod, lVMod, lfMod, uiVMod, uiFMod;
	create_shader_module(r->device, VERT_SHADER_PATH, &vMod);
	create_shader_module(r->device, FRAG_SHADER_PATH, &fMod);
	create_shader_module(r->device, EDGE_VERT_SHADER_PATH, &eVMod);
	create_shader_module(r->device, EDGE_FRAG_SHADER_PATH, &efMod);
	create_shader_module(r->device, LABEL_VERT_SHADER_PATH, &lVMod);
	create_shader_module(r->device, LABEL_FRAG_SHADER_PATH, &lfMod);
	create_shader_module(r->device, UI_VERT_SHADER_PATH, &uiVMod);
	create_shader_module(r->device, UI_FRAG_SHADER_PATH, &uiFMod);

	VkViewport vp = {0, 0, 3440, 1440, 0, 1};
	VkRect2D sc = {{0, 0}, {3440, 1440}};
	VkPipelineViewportStateCreateInfo vpS = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.pViewports = &vp,
		.scissorCount = 1,
		.pScissors = &sc};
	VkPipelineRasterizationStateCreateInfo ras = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.lineWidth = 1.0f,
		.cullMode = VK_CULL_MODE_NONE,
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE};
	VkPipelineMultisampleStateCreateInfo mul = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
	VkPipelineColorBlendAttachmentState colB = {
		.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
						  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
		.blendEnable = VK_TRUE,
		.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
		.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
		.colorBlendOp = VK_BLEND_OP_ADD,
		.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
		.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
		.alphaBlendOp = VK_BLEND_OP_ADD,
	};
	VkPipelineColorBlendStateCreateInfo colS = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &colB};

	VkPipelineShaderStageCreateInfo nstages[] = {
		{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0,
		 VK_SHADER_STAGE_VERTEX_BIT, vMod, "main", NULL},
		{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0,
		 VK_SHADER_STAGE_FRAGMENT_BIT, fMod, "main", NULL}};
	VkVertexInputBindingDescription nb[] = {
		{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX},
		{1, sizeof(Node), VK_VERTEX_INPUT_RATE_INSTANCE}};
	VkVertexInputAttributeDescription na[] = {
		{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
		{1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12},
		{2, 0, VK_FORMAT_R32_SFLOAT, 24},
		{3, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Node, position)},
		{4, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Node, color)},
		{5, 1, VK_FORMAT_R32_SFLOAT, offsetof(Node, size)},
		{6, 1, VK_FORMAT_R32_SFLOAT, offsetof(Node, glow)},
		{7, 1, VK_FORMAT_R32_SINT, offsetof(Node, degree)},
		{8, 1, VK_FORMAT_R32_SFLOAT, offsetof(Node, selected)}};
	VkPipelineVertexInputStateCreateInfo nvi = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 2,
		.pVertexBindingDescriptions = nb,
		.vertexAttributeDescriptionCount = 9,
		.pVertexAttributeDescriptions = na};
	VkPipelineInputAssemblyStateCreateInfo niAs = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
	VkGraphicsPipelineCreateInfo pInfo = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = 2,
		.pStages = nstages,
		.pVertexInputState = &nvi,
		.pInputAssemblyState = &niAs,
		.pViewportState = &vpS,
		.pRasterizationState = &ras,
		.pMultisampleState = &mul,
		.pColorBlendState = &colS,
		.layout = r->pipelineLayout,
		.renderPass = r->renderPass};
	vkCreateGraphicsPipelines(r->device, VK_NULL_HANDLE, 1, &pInfo, NULL,
							  &r->graphicsPipeline);

	// Create pipeline for node edges (wireframe)
	VkPipelineRasterizationStateCreateInfo rasLine = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_LINE,
		.lineWidth = 2.0f,
		.cullMode = VK_CULL_MODE_NONE,
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE};
	VkPipelineColorBlendAttachmentState colB_no_blend = {
		.colorWriteMask = 0xF, .blendEnable = VK_FALSE};
	VkPipelineColorBlendStateCreateInfo colS_no_blend = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &colB_no_blend};
	VkGraphicsPipelineCreateInfo linePInfo = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = 2,
		.pStages = nstages,
		.pVertexInputState = &nvi,
		.pInputAssemblyState = &niAs,
		.pViewportState = &vpS,
		.pRasterizationState = &rasLine,
		.pMultisampleState = &mul,
		.pColorBlendState = &colS_no_blend,
		.layout = r->pipelineLayout,
		.renderPass = r->renderPass};
	vkCreateGraphicsPipelines(r->device, VK_NULL_HANDLE, 1, &linePInfo, NULL,
							  &r->nodeEdgePipeline);

	VkShaderModule svMod, sfMod;
	create_shader_module(r->device, SPHERE_VERT_SHADER_PATH, &svMod);
	create_shader_module(r->device, SPHERE_FRAG_SHADER_PATH, &sfMod);
	VkPipelineShaderStageCreateInfo sstages[] = {
		{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0,
		 VK_SHADER_STAGE_VERTEX_BIT, svMod, "main", NULL},
		{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0,
		 VK_SHADER_STAGE_FRAGMENT_BIT, sfMod, "main", NULL}};
	VkVertexInputBindingDescription sb[] = {
		{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX}};
	VkVertexInputAttributeDescription sa[] = {
		{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
		{1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12},
		{2, 0, VK_FORMAT_R32G32_SFLOAT, 24}};
	VkPipelineVertexInputStateCreateInfo svi = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = sb,
		.vertexAttributeDescriptionCount = 3,
		.pVertexAttributeDescriptions = sa};

	// Transparent blending
	VkPipelineColorBlendAttachmentState colB_trans = {
		.colorWriteMask = 0xF,
		.blendEnable = VK_TRUE,
		.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
		.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
		.colorBlendOp = VK_BLEND_OP_ADD,
		.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
		.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
		.alphaBlendOp = VK_BLEND_OP_ADD};
	VkPipelineColorBlendStateCreateInfo colS_trans = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &colB_trans};

	// Disable depth write for transparent objects (usually), but keeping depth
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
	vkCreateGraphicsPipelines(r->device, VK_NULL_HANDLE, 1, &spInfo, NULL,
							  &r->spherePipeline);
	vkDestroyShaderModule(r->device, sfMod, NULL);
	vkDestroyShaderModule(r->device, svMod, NULL);

	VkPipelineShaderStageCreateInfo estages[] = {
		{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0,
		 VK_SHADER_STAGE_VERTEX_BIT, eVMod, "main", NULL},
		{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0,
		 VK_SHADER_STAGE_FRAGMENT_BIT, efMod, "main", NULL}};
	VkVertexInputBindingDescription eb[] = {
		{0, sizeof(EdgeVertex), VK_VERTEX_INPUT_RATE_VERTEX}};
	VkVertexInputAttributeDescription ea[] = {
		{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
		{1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12},
		{2, 0, VK_FORMAT_R32_SFLOAT, 24},
		{3, 0, VK_FORMAT_R32_SFLOAT, 28},
		{4, 0, VK_FORMAT_R32_SFLOAT, offsetof(EdgeVertex, animation_progress)},
		{5, 0, VK_FORMAT_R32_SINT, offsetof(EdgeVertex, animation_direction)},
		{6, 0, VK_FORMAT_R32_SINT, offsetof(EdgeVertex, is_animating)},
		{7, 0, VK_FORMAT_R32_SFLOAT, offsetof(EdgeVertex, normalized_pos)}};
	VkPipelineVertexInputStateCreateInfo evi = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = eb,
		.vertexAttributeDescriptionCount = 8,
		.pVertexAttributeDescriptions = ea};
	VkPipelineInputAssemblyStateCreateInfo eia = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST};
	VkGraphicsPipelineCreateInfo epInfo = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = 2,
		.pStages = estages,
		.pVertexInputState = &evi,
		.pInputAssemblyState = &eia,
		.pViewportState = &vpS,
		.pRasterizationState = &ras,
		.pMultisampleState = &mul,
		.pColorBlendState = &colS,
		.layout = r->pipelineLayout,
		.renderPass = r->renderPass};
	vkCreateGraphicsPipelines(r->device, VK_NULL_HANDLE, 1, &epInfo, NULL,
							  &r->edgePipeline);

	VkPipelineInputAssemblyStateCreateInfo lias = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP};
	VkPipelineShaderStageCreateInfo lstages[] = {
		{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0,
		 VK_SHADER_STAGE_VERTEX_BIT, lVMod, "main", NULL},
		{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0,
		 VK_SHADER_STAGE_FRAGMENT_BIT, lfMod, "main", NULL}};
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
	vkCreateGraphicsPipelines(r->device, VK_NULL_HANDLE, 1, &lpInfo, NULL,
							  &r->labelPipeline);

	// Define stages for UI
	VkPipelineShaderStageCreateInfo uiStages[] = {
		{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0,
		 VK_SHADER_STAGE_VERTEX_BIT, uiVMod, "main", NULL},
		{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0,
		 VK_SHADER_STAGE_FRAGMENT_BIT, uiFMod, "main", NULL}};

	// Define Vertex Input (Matching UIVertex and UIInstance)
	// Assuming UIVertex has pos (vec3) and tex (vec2)
	// Assuming UIInstance has color (vec4)
	VkVertexInputBindingDescription uib[] = {
		{0, sizeof(UIVertex), VK_VERTEX_INPUT_RATE_VERTEX},
		{1, sizeof(UIInstance), VK_VERTEX_INPUT_RATE_INSTANCE}};

	VkVertexInputAttributeDescription uia[] = {
		// --- UIVertex (Binding 0) ---
		{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(UIVertex, pos)},
		{1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(UIVertex, tex)},

		// --- UIInstance (Binding 1) ---
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

	// Create the pipeline
	VkGraphicsPipelineCreateInfo uiPInfo = pInfo; // Reuse basic config
	uiPInfo.stageCount = 2;
	uiPInfo.pStages = uiStages;
	uiPInfo.pVertexInputState = &uivi;
	uiPInfo.pInputAssemblyState =
		&lias; // UI usually uses triangle strips/lists

	vkCreateGraphicsPipelines(r->device, VK_NULL_HANDLE, 1, &uiPInfo, NULL,
							  &r->uiPipeline);

	vkDestroyShaderModule(r->device, uiFMod, NULL);
	vkDestroyShaderModule(r->device, uiVMod, NULL);
	vkDestroyShaderModule(r->device, lfMod, NULL);
	vkDestroyShaderModule(r->device, lVMod, NULL);
	vkDestroyShaderModule(r->device, efMod, NULL);
	vkDestroyShaderModule(r->device, eVMod, NULL);
	vkDestroyShaderModule(r->device, fMod, NULL);
	vkDestroyShaderModule(r->device, vMod, NULL);

	// --- COMPUTE PIPELINE SETUP ---
	VkDescriptorSetLayoutBinding cBindings[] = {
		{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT,
		 NULL},
		{1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT,
		 NULL},
		{2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT,
		 NULL}};
	VkDescriptorSetLayoutCreateInfo cLayInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 3,
		.pBindings = cBindings};
	vkCreateDescriptorSetLayout(r->device, &cLayInfo, NULL,
								&r->computeDescriptorSetLayout);
	VkPushConstantRange cPush = {.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
								 .offset = 0,
								 .size = sizeof(int) * 3};
	VkPipelineLayoutCreateInfo cPlyLayInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &r->computeDescriptorSetLayout,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &cPush};
	vkCreatePipelineLayout(r->device, &cPlyLayInfo, NULL,
						   &r->computePipelineLayout);

	VkShaderModule sphMod = VK_NULL_HANDLE, hubMod = VK_NULL_HANDLE;
	create_shader_module(r->device, ROUTING_COMP_SHADER_PATH, &sphMod);
	create_shader_module(r->device, ROUTING_HUB_COMP_SHADER_PATH, &hubMod);
	VkPipelineShaderStageCreateInfo cStageSph = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.stage = VK_SHADER_STAGE_COMPUTE_BIT,
		.module = sphMod,
		.pName = "main"};
	VkPipelineShaderStageCreateInfo cStageHub = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.stage = VK_SHADER_STAGE_COMPUTE_BIT,
		.module = hubMod,
		.pName = "main"};
	VkComputePipelineCreateInfo cpInfoSph = {
		.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
		.stage = cStageSph,
		.layout = r->computePipelineLayout};
	VkComputePipelineCreateInfo cpInfoHub = {
		.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
		.stage = cStageHub,
		.layout = r->computePipelineLayout};
	vkCreateComputePipelines(r->device, VK_NULL_HANDLE, 1, &cpInfoSph, NULL,
							 &r->computeSphericalPipeline);
	vkCreateComputePipelines(r->device, VK_NULL_HANDLE, 1, &cpInfoHub, NULL,
							 &r->computeHubSpokePipeline);
	vkDestroyShaderModule(r->device, sphMod, NULL);
	vkDestroyShaderModule(r->device, hubMod, NULL);
	// ------------------------------

	return 0;
}
