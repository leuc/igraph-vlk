#include "vulkan/pipeline_compute.h"
#include "vulkan/renderer.h"
#include "vulkan/utils.h"

void renderer_create_compute_pipelines(Renderer *r)
{
	VkDescriptorSetLayoutBinding computeBindings[] = {{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}, {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}, {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}};
	VkDescriptorSetLayoutCreateInfo computeLayoutInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 3, .pBindings = computeBindings};
	VK_CHECK(vkCreateDescriptorSetLayout(r->core.device, &computeLayoutInfo, NULL, &r->computeDescriptorSetLayout), "Failed to create compute descriptor set layout");

	VkPushConstantRange computePushConstant = {.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0, .size = sizeof(int) * 3};
	VkPipelineLayoutCreateInfo computePipelineLayoutInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1, .pSetLayouts = &r->computeDescriptorSetLayout, .pushConstantRangeCount = 1, .pPushConstantRanges = &computePushConstant};
	VK_CHECK(vkCreatePipelineLayout(r->core.device, &computePipelineLayoutInfo, NULL, &r->computePipelineLayout), "Failed to create compute pipeline layout");

	VkShaderModule sphericalShaderModule = VK_NULL_HANDLE;
	VK_CHECK(create_shader_module(r->core.device, ROUTING_COMP_SHADER_PATH, &sphericalShaderModule), "Failed to create compute shader module");
	VkPipelineShaderStageCreateInfo computeStageSpherical = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = sphericalShaderModule, .pName = "main"};
	VkComputePipelineCreateInfo computePipelineInfoSpherical = {.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, .stage = computeStageSpherical, .layout = r->computePipelineLayout};
	VK_CHECK(vkCreateComputePipelines(r->core.device, VK_NULL_HANDLE, 1, &computePipelineInfoSpherical, NULL, &r->computeSphericalPipeline), "Failed to create compute spherical pipeline");
	vkDestroyShaderModule(r->core.device, sphericalShaderModule, NULL);

	renderer_create_splc_compute_pipeline(r);
}

void renderer_create_splc_compute_pipeline(Renderer *r)
{
	VkDescriptorSetLayoutBinding splcBindings[] = {
		{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
		{1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
		{2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
		{3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
	};
	VkDescriptorSetLayoutCreateInfo splcLayoutInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 4, .pBindings = splcBindings};
	VK_CHECK(vkCreateDescriptorSetLayout(r->core.device, &splcLayoutInfo, NULL, &r->splc_compute_descriptor_set_layout), "Failed to create SPLC compute descriptor set layout");

	VkPushConstantRange splcPushConstant = {.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0, .size = sizeof(uint32_t)};
	VkPipelineLayoutCreateInfo splcPipelineLayoutInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1, .pSetLayouts = &r->splc_compute_descriptor_set_layout, .pushConstantRangeCount = 1, .pPushConstantRanges = &splcPushConstant};
	VK_CHECK(vkCreatePipelineLayout(r->core.device, &splcPipelineLayoutInfo, NULL, &r->splc_compute_pipeline_layout), "Failed to create SPLC compute pipeline layout");

	VkShaderModule splcShaderModule = VK_NULL_HANDLE;
	VK_CHECK(create_shader_module(r->core.device, SPLC_COMP_SHADER_PATH, &splcShaderModule), "Failed to create SPLC compute shader module");
	VkPipelineShaderStageCreateInfo splcStage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = splcShaderModule, .pName = "main"};
	VkComputePipelineCreateInfo splcPipelineInfo = {.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, .stage = splcStage, .layout = r->splc_compute_pipeline_layout};
	VK_CHECK(vkCreateComputePipelines(r->core.device, VK_NULL_HANDLE, 1, &splcPipelineInfo, NULL, &r->splc_compute_pipeline), "Failed to create SPLC compute pipeline");
	vkDestroyShaderModule(r->core.device, splcShaderModule, NULL);

	VkDescriptorPoolSize splcPoolSizes = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4};
	VkDescriptorPoolCreateInfo splcPoolInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &splcPoolSizes};
	VK_CHECK(vkCreateDescriptorPool(r->core.device, &splcPoolInfo, NULL, &r->splc_descriptor_pool), "Failed to create SPLC descriptor pool");

	VkDescriptorSetAllocateInfo splcDescSetInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = r->splc_descriptor_pool, .descriptorSetCount = 1, .pSetLayouts = &r->splc_compute_descriptor_set_layout};
	VK_CHECK(vkAllocateDescriptorSets(r->core.device, &splcDescSetInfo, &r->splc_descriptor_set), "Failed to allocate SPLC descriptor set");
}
