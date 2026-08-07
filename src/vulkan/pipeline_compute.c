/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "vulkan/pipeline_compute.h"
#include "vulkan/renderer.h"
#include "vulkan/renderer_bcgl.h"
#include "vulkan/utils.h"

void renderer_create_compute_pipelines(Renderer *r)
{
	VkDescriptorSetLayoutBinding computeBindings[] = {{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}, {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}, {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}};
	VkDescriptorSetLayoutCreateInfo computeLayoutInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 3, .pBindings = computeBindings};
	VK_CHECK(vkCreateDescriptorSetLayout(r->core.device, &computeLayoutInfo, NULL, &r->descriptors.compute_layout), "Failed to create compute descriptor set layout");

	VkPushConstantRange computePushConstant = {.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0, .size = sizeof(int) * 3};
	VkPipelineLayoutCreateInfo computePipelineLayoutInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1, .pSetLayouts = &r->descriptors.compute_layout, .pushConstantRangeCount = 1, .pPushConstantRanges = &computePushConstant};
	VK_CHECK(vkCreatePipelineLayout(r->core.device, &computePipelineLayoutInfo, NULL, &r->computePipelineLayout), "Failed to create compute pipeline layout");

	VkShaderModule sphericalShaderModule = VK_NULL_HANDLE;
	VK_CHECK(create_shader_module(r->core.device, ROUTING_COMP_SHADER_PATH, &sphericalShaderModule), "Failed to create compute shader module");
	VkPipelineShaderStageCreateInfo computeStageSpherical = VK_SHADER_STAGE_COMP(sphericalShaderModule);
	VkComputePipelineCreateInfo computePipelineInfoSpherical = {.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, .stage = computeStageSpherical, .layout = r->computePipelineLayout};
	VK_CHECK(vkCreateComputePipelines(r->core.device, VK_NULL_HANDLE, 1, &computePipelineInfoSpherical, NULL, &r->pipelines.compute_spherical), "Failed to create compute spherical pipeline");
	vkDestroyShaderModule(r->core.device, sphericalShaderModule, NULL);

	// Main Path is a pure gather DP using integer atomics for the shared maximum.
	renderer_create_criticality_compute_pipeline(r);
	renderer_create_bcgl_compute_pipeline(r);
}

// Bindings 0-3: forward and reverse CSR (nodes, edges) x (out, in)
// Binding 4:    node ids grouped by level
// Bindings 5-8: lnW, lnX, height, depth
// Binding 9: shared live edge presentation state
// Binding 10: packed analysis, predecessor, and selection result
// Binding 11: NPPC reachability bitsets
#define CRIT_BINDING_COUNT 12

void renderer_create_criticality_compute_pipeline(Renderer *r)
{
	VkDescriptorSetLayoutBinding critBindings[CRIT_BINDING_COUNT];
	for (uint32_t i = 0; i < CRIT_BINDING_COUNT; i++)
		critBindings[i] = (VkDescriptorSetLayoutBinding){i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL};

	VkDescriptorSetLayoutCreateInfo critLayoutInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = CRIT_BINDING_COUNT, .pBindings = critBindings};
	VK_CHECK(vkCreateDescriptorSetLayout(r->core.device, &critLayoutInfo, NULL, &r->descriptors.crit_compute_layout), "Failed to create criticality compute descriptor set layout");

	VkPushConstantRange critPushConstant = {.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0, .size = sizeof(CritPushConstants)};
	VkPipelineLayoutCreateInfo critPipelineLayoutInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1, .pSetLayouts = &r->descriptors.crit_compute_layout, .pushConstantRangeCount = 1, .pPushConstantRanges = &critPushConstant};
	VK_CHECK(vkCreatePipelineLayout(r->core.device, &critPipelineLayoutInfo, NULL, &r->crit.pipeline_layout), "Failed to create criticality compute pipeline layout");

	VkShaderModule critShaderModule = VK_NULL_HANDLE;
	VK_CHECK(create_shader_module(r->core.device, MAIN_PATH_COMP_SHADER_PATH, &critShaderModule), "Failed to create Main Path compute shader module");
	VkPipelineShaderStageCreateInfo critStage = VK_SHADER_STAGE_COMP(critShaderModule);
	VkComputePipelineCreateInfo critPipelineInfo = {.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, .stage = critStage, .layout = r->crit.pipeline_layout};
	VK_CHECK(vkCreateComputePipelines(r->core.device, VK_NULL_HANDLE, 1, &critPipelineInfo, NULL, &r->pipelines.compute_criticality), "Failed to create criticality compute pipeline");
	vkDestroyShaderModule(r->core.device, critShaderModule, NULL);

	VkDescriptorPoolSize critPoolSizes = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, CRIT_BINDING_COUNT};
	VkDescriptorPoolCreateInfo critPoolInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &critPoolSizes};
	VK_CHECK(vkCreateDescriptorPool(r->core.device, &critPoolInfo, NULL, &r->descriptors.crit_pool), "Failed to create criticality descriptor pool");

	VkDescriptorSetAllocateInfo critDescSetInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = r->descriptors.crit_pool, .descriptorSetCount = 1, .pSetLayouts = &r->descriptors.crit_compute_layout};
	VK_CHECK(vkAllocateDescriptorSets(r->core.device, &critDescSetInfo, &r->descriptors.crit_set), "Failed to allocate criticality descriptor set");
}
