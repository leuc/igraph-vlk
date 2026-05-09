#include "vulkan/pipeline_compute.h"
#include "vulkan/renderer.h"
#include "vulkan/utils.h"

void renderer_create_compute_pipelines(Renderer *r)
{
	VkDescriptorSetLayoutBinding computeBindings[] = {{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}, {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}, {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}};
	VkDescriptorSetLayoutCreateInfo computeLayoutInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 3, .pBindings = computeBindings};
	vkCreateDescriptorSetLayout(r->core.device, &computeLayoutInfo, NULL, &r->computeDescriptorSetLayout);

	VkPushConstantRange computePushConstant = {.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0, .size = sizeof(int) * 3};
	VkPipelineLayoutCreateInfo computePipelineLayoutInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1, .pSetLayouts = &r->computeDescriptorSetLayout, .pushConstantRangeCount = 1, .pPushConstantRanges = &computePushConstant};
	vkCreatePipelineLayout(r->core.device, &computePipelineLayoutInfo, NULL, &r->computePipelineLayout);

	VkShaderModule sphericalShaderModule = VK_NULL_HANDLE;
	create_shader_module(r->core.device, ROUTING_COMP_SHADER_PATH, &sphericalShaderModule);
	VkPipelineShaderStageCreateInfo computeStageSpherical = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = sphericalShaderModule, .pName = "main"};
	VkComputePipelineCreateInfo computePipelineInfoSpherical = {.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, .stage = computeStageSpherical, .layout = r->computePipelineLayout};
	vkCreateComputePipelines(r->core.device, VK_NULL_HANDLE, 1, &computePipelineInfoSpherical, NULL, &r->computeSphericalPipeline);
	vkDestroyShaderModule(r->core.device, sphericalShaderModule, NULL);
}
