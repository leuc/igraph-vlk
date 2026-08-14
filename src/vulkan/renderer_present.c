/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "vulkan/renderer_present.h"

#include <stdio.h>
#include <stdlib.h>

#include "vulkan/color_space.h"
#include "vulkan/utils.h"

typedef struct
{
	uint32_t output_mode;
	float reference_nits;
	float peak_nits;
	float highlight_nits;
} PresentPushConstants;

static void destroy_target_resources(Renderer *r)
{
	if (r->pipelines.present != VK_NULL_HANDLE) {
		vkDestroyPipeline(r->core.device, r->pipelines.present, NULL);
		r->pipelines.present = VK_NULL_HANDLE;
	}
	if (r->descriptors.present_pool != VK_NULL_HANDLE) {
		vkDestroyDescriptorPool(r->core.device, r->descriptors.present_pool, NULL);
		r->descriptors.present_pool = VK_NULL_HANDLE;
	}
	free(r->descriptors.present_sets);
	r->descriptors.present_sets = NULL;
}

static void create_pipeline(Renderer *r)
{
	VkShaderModule vertex_module;
	VkShaderModule fragment_module;
	VK_CHECK(create_shader_module(r->core.device, PRESENT_VERT_SHADER_PATH, &vertex_module), "Failed to create presentation vertex shader module");
	VK_CHECK(create_shader_module(r->core.device, PRESENT_FRAG_SHADER_PATH, &fragment_module), "Failed to create presentation fragment shader module");

	VkPipelineShaderStageCreateInfo stages[] = {VK_SHADER_STAGE_VERT(vertex_module), VK_SHADER_STAGE_FRAG(fragment_module)};
	VkPipelineVertexInputStateCreateInfo vertex_input = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
	VkPipelineInputAssemblyStateCreateInfo input_assembly = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
	VkPipelineViewportStateCreateInfo viewport = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1};
	VkPipelineRasterizationStateCreateInfo rasterization = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, .polygonMode = VK_POLYGON_MODE_FILL, .lineWidth = 1.0f, .cullMode = VK_CULL_MODE_NONE, .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE};
	VkPipelineMultisampleStateCreateInfo multisample = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
	VkPipelineColorBlendAttachmentState blend_attachment = {.colorWriteMask = 0xF};
	VkPipelineColorBlendStateCreateInfo blend = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1, .pAttachments = &blend_attachment};
	VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
	VkPipelineDynamicStateCreateInfo dynamic = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, .dynamicStateCount = 2, .pDynamicStates = dynamic_states};
	VkGraphicsPipelineCreateInfo info = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = 2, .pStages = stages, .pVertexInputState = &vertex_input, .pInputAssemblyState = &input_assembly, .pViewportState = &viewport, .pRasterizationState = &rasterization, .pMultisampleState = &multisample, .pColorBlendState = &blend, .pDynamicState = &dynamic, .layout = r->presentPipelineLayout, .renderPass = r->renderPass.presentRenderPass};
	VK_CHECK(vkCreateGraphicsPipelines(r->core.device, VK_NULL_HANDLE, 1, &info, NULL, &r->pipelines.present), "Failed to create presentation pipeline");

	vkDestroyShaderModule(r->core.device, fragment_module, NULL);
	vkDestroyShaderModule(r->core.device, vertex_module, NULL);
}

void renderer_present_init(Renderer *r)
{
	r->pipelines.present = VK_NULL_HANDLE;
	r->descriptors.present_layout = VK_NULL_HANDLE;
	r->descriptors.present_pool = VK_NULL_HANDLE;
	r->descriptors.present_sets = NULL;
	r->presentPipelineLayout = VK_NULL_HANDLE;
	r->presentSampler = VK_NULL_HANDLE;

	VkDescriptorSetLayoutBinding binding = {.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT};
	VkDescriptorSetLayoutCreateInfo layout_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 1, .pBindings = &binding};
	VK_CHECK(vkCreateDescriptorSetLayout(r->core.device, &layout_info, NULL, &r->descriptors.present_layout), "Failed to create presentation descriptor layout");

	VkPushConstantRange push = {.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT, .size = sizeof(PresentPushConstants)};
	VkPipelineLayoutCreateInfo pipeline_layout_info = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1, .pSetLayouts = &r->descriptors.present_layout, .pushConstantRangeCount = 1, .pPushConstantRanges = &push};
	VK_CHECK(vkCreatePipelineLayout(r->core.device, &pipeline_layout_info, NULL, &r->presentPipelineLayout), "Failed to create presentation pipeline layout");

	VkSamplerCreateInfo sampler_info = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, .magFilter = VK_FILTER_NEAREST, .minFilter = VK_FILTER_NEAREST, .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST, .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, .maxLod = 0.0f};
	VK_CHECK(vkCreateSampler(r->core.device, &sampler_info, NULL, &r->presentSampler), "Failed to create presentation sampler");
	renderer_present_recreate(r);
}

void renderer_present_recreate(Renderer *r)
{
	destroy_target_resources(r);
	uint32_t count = r->renderPass.imageCount;
	VkDescriptorPoolSize pool_size = {.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = count};
	VkDescriptorPoolCreateInfo pool_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = count, .poolSizeCount = 1, .pPoolSizes = &pool_size};
	VK_CHECK(vkCreateDescriptorPool(r->core.device, &pool_info, NULL, &r->descriptors.present_pool), "Failed to create presentation descriptor pool");
	VkDescriptorSetLayout *layouts = malloc(sizeof(*layouts) * count);
	for (uint32_t i = 0; i < count; i++) {
		layouts[i] = r->descriptors.present_layout;
	}
	r->descriptors.present_sets = malloc(sizeof(*r->descriptors.present_sets) * count);
	VkDescriptorSetAllocateInfo allocate_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = r->descriptors.present_pool, .descriptorSetCount = count, .pSetLayouts = layouts};
	VK_CHECK(vkAllocateDescriptorSets(r->core.device, &allocate_info, r->descriptors.present_sets), "Failed to allocate presentation descriptor sets");
	free(layouts);
	for (uint32_t i = 0; i < count; i++) {
		VkDescriptorImageInfo image = {.sampler = r->presentSampler, .imageView = r->renderPass.colorViews[i], .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
		VkWriteDescriptorSet write = VK_WRITE_DESC_IMAGE(r->descriptors.present_sets[i], 0, &image, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
		vkUpdateDescriptorSets(r->core.device, 1, &write, 0, NULL);
	}
	create_pipeline(r);
	r->colorState = renderer_color_state(r->swapchain.outputMode, &r->swapchain.displayColor);
	printf("[Vulkan] Presentation output active: %s, reference=%.0f nits, peak=%.0f nits, highlight=%.0f nits\n", r->swapchain.outputMode == VULKAN_OUTPUT_HDR10 ? "HDR10 BT.2020/PQ" : "SDR sRGB", r->colorState.reference_nits, r->colorState.peak_nits, r->colorState.highlight_nits);
}

void renderer_present_record(Renderer *r, VkCommandBuffer command_buffer, uint32_t image_index)
{
	VkRenderPassBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, .renderPass = r->renderPass.presentRenderPass, .framebuffer = r->renderPass.presentFramebuffers[image_index], .renderArea = {{0, 0}, r->swapchain.extent}};
	vkCmdBeginRenderPass(command_buffer, &begin, VK_SUBPASS_CONTENTS_INLINE);
	VkViewport viewport = {0.0f, 0.0f, (float)r->swapchain.extent.width, (float)r->swapchain.extent.height, 0.0f, 1.0f};
	VkRect2D scissor = {{0, 0}, r->swapchain.extent};
	vkCmdSetViewport(command_buffer, 0, 1, &viewport);
	vkCmdSetScissor(command_buffer, 0, 1, &scissor);
	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipelines.present);
	vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, r->presentPipelineLayout, 0, 1, &r->descriptors.present_sets[image_index], 0, NULL);
	PresentPushConstants push = {.output_mode = (uint32_t)r->colorState.mode, .reference_nits = r->colorState.reference_nits, .peak_nits = r->colorState.peak_nits, .highlight_nits = r->colorState.highlight_nits};
	vkCmdPushConstants(command_buffer, r->presentPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
	vkCmdDraw(command_buffer, 3, 1, 0, 0);
	vkCmdEndRenderPass(command_buffer);
}

void renderer_present_destroy(Renderer *r)
{
	destroy_target_resources(r);
	if (r->presentSampler != VK_NULL_HANDLE) {
		vkDestroySampler(r->core.device, r->presentSampler, NULL);
		r->presentSampler = VK_NULL_HANDLE;
	}
	if (r->presentPipelineLayout != VK_NULL_HANDLE) {
		vkDestroyPipelineLayout(r->core.device, r->presentPipelineLayout, NULL);
		r->presentPipelineLayout = VK_NULL_HANDLE;
	}
	if (r->descriptors.present_layout != VK_NULL_HANDLE) {
		vkDestroyDescriptorSetLayout(r->core.device, r->descriptors.present_layout, NULL);
		r->descriptors.present_layout = VK_NULL_HANDLE;
	}
}
