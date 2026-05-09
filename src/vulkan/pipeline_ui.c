#include "vulkan/pipeline_ui.h"
#include "vulkan/renderer.h"
#include "vulkan/renderer_ui.h"
#include "vulkan/utils.h"
#include <stddef.h>

void renderer_create_ui_pipelines(Renderer *r)
{
	VkShaderModule uiVertexShaderModule, uiFragmentShaderModule, labelVertexShaderModule, labelFragmentShaderModule, menuVertexShaderModule, menuFragmentShaderModule;
	create_shader_module(r->core.device, UI_VERT_SHADER_PATH, &uiVertexShaderModule);
	create_shader_module(r->core.device, UI_FRAG_SHADER_PATH, &uiFragmentShaderModule);
	create_shader_module(r->core.device, LABEL_VERT_SHADER_PATH, &labelVertexShaderModule);
	create_shader_module(r->core.device, LABEL_FRAG_SHADER_PATH, &labelFragmentShaderModule);
	create_shader_module(r->core.device, MENU_VERT_SHADER_PATH, &menuVertexShaderModule);
	create_shader_module(r->core.device, MENU_FRAG_SHADER_PATH, &menuFragmentShaderModule);

	VkPipelineViewportStateCreateInfo viewportState = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1};
	VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
	VkPipelineDynamicStateCreateInfo dynamicState = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, .dynamicStateCount = 2, .pDynamicStates = dynamicStates};
	VkPipelineRasterizationStateCreateInfo rasterizationState = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, .polygonMode = VK_POLYGON_MODE_FILL, .lineWidth = 1.0f, .cullMode = VK_CULL_MODE_NONE, .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE};
	VkPipelineMultisampleStateCreateInfo multisampleState = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
	VkPipelineColorBlendAttachmentState colorBlendAttachment = VK_DEFAULT_COLOR_BLEND_ATTACHMENT;
	VkPipelineColorBlendStateCreateInfo colorBlendState = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1, .pAttachments = &colorBlendAttachment};
	VkPipelineDepthStencilStateCreateInfo uiDepthStencilState = VK_DEPTH_STENCIL_STATE_DISABLED;
	VkPipelineDepthStencilStateCreateInfo menuDepthStencilState = VK_DEPTH_STENCIL_STATE_TEST_NO_WRITE_LESS_EQUAL;

	// UI
	VkPipelineShaderStageCreateInfo uiShaderStages[] = {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, uiVertexShaderModule, "main", NULL}, {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, uiFragmentShaderModule, "main", NULL}};
	VkVertexInputBindingDescription uiBindings[] = {{0, sizeof(UIVertex), VK_VERTEX_INPUT_RATE_VERTEX}, {1, sizeof(UIInstance), VK_VERTEX_INPUT_RATE_INSTANCE}};
	VkVertexInputAttributeDescription uiAttributes[] = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(UIVertex, pos)}, {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(UIVertex, tex)}, {2, 1, VK_FORMAT_R32G32_SFLOAT, offsetof(UIInstance, screenPos)}, {3, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(UIInstance, charRect)}, {4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(UIInstance, charUV)}, {5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(UIInstance, color)}};
	VkPipelineVertexInputStateCreateInfo uiVertexInput = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, .vertexBindingDescriptionCount = 2, .pVertexBindingDescriptions = uiBindings, .vertexAttributeDescriptionCount = 6, .pVertexAttributeDescriptions = uiAttributes};
	VkPipelineInputAssemblyStateCreateInfo labelInputAssembly = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP};
	VkGraphicsPipelineCreateInfo uiPipelineInfo = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = 2, .pStages = uiShaderStages, .pVertexInputState = &uiVertexInput, .pInputAssemblyState = &labelInputAssembly, .pViewportState = &viewportState, .pRasterizationState = &rasterizationState, .pMultisampleState = &multisampleState, .pColorBlendState = &colorBlendState, .pDepthStencilState = &uiDepthStencilState, .pDynamicState = &dynamicState, .layout = r->pipelineLayout, .renderPass = r->renderPass.renderPass};
	VK_CHECK(vkCreateGraphicsPipelines(r->core.device, VK_NULL_HANDLE, 1, &uiPipelineInfo, NULL, &r->uiPipeline), "Failed to create UI graphics pipeline");

	// Labels
	VkPipelineShaderStageCreateInfo labelShaderStages[] = {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, labelVertexShaderModule, "main", NULL}, {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, labelFragmentShaderModule, "main", NULL}};
	VkVertexInputBindingDescription labelBindings[] = {{0, sizeof(LabelVertex), VK_VERTEX_INPUT_RATE_VERTEX}, {1, sizeof(LabelInstance), VK_VERTEX_INPUT_RATE_INSTANCE}};
	VkVertexInputAttributeDescription labelAttributes[] = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(LabelVertex, pos)}, {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(LabelVertex, tex)}, {2, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(LabelInstance, nodePos)}, {3, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(LabelInstance, charRect)}, {4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(LabelInstance, charUV)}, {5, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(LabelInstance, right)}, {6, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(LabelInstance, up)}};
	VkPipelineVertexInputStateCreateInfo labelVertexInput = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, .vertexBindingDescriptionCount = 2, .pVertexBindingDescriptions = labelBindings, .vertexAttributeDescriptionCount = 7, .pVertexAttributeDescriptions = labelAttributes};
	VkGraphicsPipelineCreateInfo labelPipelineInfo = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = 2, .pStages = labelShaderStages, .pVertexInputState = &labelVertexInput, .pInputAssemblyState = &labelInputAssembly, .pViewportState = &viewportState, .pRasterizationState = &rasterizationState, .pMultisampleState = &multisampleState, .pColorBlendState = &colorBlendState, .pDepthStencilState = &menuDepthStencilState, .pDynamicState = &dynamicState, .layout = r->pipelineLayout, .renderPass = r->renderPass.renderPass};
	VK_CHECK(vkCreateGraphicsPipelines(r->core.device, VK_NULL_HANDLE, 1, &labelPipelineInfo, NULL, &r->labelPipeline), "Failed to create label graphics pipeline");

	// Menu
	VkPipelineShaderStageCreateInfo menuShaderStages[] = {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, menuVertexShaderModule, "main", NULL}, {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, menuFragmentShaderModule, "main", NULL}};
	VkVertexInputBindingDescription menuBindings[] = {{0, sizeof(QuadVertex), VK_VERTEX_INPUT_RATE_VERTEX}, {1, sizeof(MenuInstance), VK_VERTEX_INPUT_RATE_INSTANCE}};
	VkVertexInputAttributeDescription menuAttributes[] = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(QuadVertex, pos)}, {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(QuadVertex, tex)}, {2, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MenuInstance, worldPos)}, {3, 1, VK_FORMAT_R32G32_SFLOAT, offsetof(MenuInstance, texCoord)}, {4, 1, VK_FORMAT_R32_SFLOAT, offsetof(MenuInstance, texId)}, {5, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MenuInstance, scale)}, {6, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(MenuInstance, rotation)}, {7, 1, VK_FORMAT_R32_SFLOAT, offsetof(MenuInstance, hovered)}};
	VkPipelineVertexInputStateCreateInfo menuVertexInput = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, .vertexBindingDescriptionCount = 2, .pVertexBindingDescriptions = menuBindings, .vertexAttributeDescriptionCount = 8, .pVertexAttributeDescriptions = menuAttributes};
	VkPipelineInputAssemblyStateCreateInfo menuInputAssembly = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
	VkGraphicsPipelineCreateInfo menuPipelineInfo = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = 2, .pStages = menuShaderStages, .pVertexInputState = &menuVertexInput, .pInputAssemblyState = &menuInputAssembly, .pViewportState = &viewportState, .pRasterizationState = &rasterizationState, .pMultisampleState = &multisampleState, .pColorBlendState = &colorBlendState, .pDepthStencilState = &menuDepthStencilState, .pDynamicState = &dynamicState, .layout = r->pipelineLayout, .renderPass = r->renderPass.renderPass};
	VK_CHECK(vkCreateGraphicsPipelines(r->core.device, VK_NULL_HANDLE, 1, &menuPipelineInfo, NULL, &r->menuPipeline), "Failed to create menu graphics pipeline");

	vkDestroyShaderModule(r->core.device, menuFragmentShaderModule, NULL);
	vkDestroyShaderModule(r->core.device, menuVertexShaderModule, NULL);
	vkDestroyShaderModule(r->core.device, labelFragmentShaderModule, NULL);
	vkDestroyShaderModule(r->core.device, labelVertexShaderModule, NULL);
	vkDestroyShaderModule(r->core.device, uiFragmentShaderModule, NULL);
	vkDestroyShaderModule(r->core.device, uiVertexShaderModule, NULL);
}
