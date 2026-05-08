#include "vulkan/pipeline_compute.h"
#include "vulkan/renderer.h"
#include "vulkan/utils.h"

void renderer_create_compute_pipelines(Renderer *r)
{
	VkDescriptorSetLayoutBinding cBindings[] = {{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}, {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}, {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}};
	VkDescriptorSetLayoutCreateInfo cLayInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 3, .pBindings = cBindings};
	vkCreateDescriptorSetLayout(r->core.device, &cLayInfo, NULL, &r->computeDescriptorSetLayout);

	VkPushConstantRange cPush = {.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0, .size = sizeof(int) * 3};
	VkPipelineLayoutCreateInfo cPlyLayInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1, .pSetLayouts = &r->computeDescriptorSetLayout, .pushConstantRangeCount = 1, .pPushConstantRanges = &cPush};
	vkCreatePipelineLayout(r->core.device, &cPlyLayInfo, NULL, &r->computePipelineLayout);

	VkShaderModule sphMod = VK_NULL_HANDLE;
	create_shader_module(r->core.device, ROUTING_COMP_SHADER_PATH, &sphMod);
	VkPipelineShaderStageCreateInfo cStageSph = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = sphMod, .pName = "main"};
	VkComputePipelineCreateInfo cpInfoSph = {.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, .stage = cStageSph, .layout = r->computePipelineLayout};
	vkCreateComputePipelines(r->core.device, VK_NULL_HANDLE, 1, &cpInfoSph, NULL, &r->computeSphericalPipeline);
	vkDestroyShaderModule(r->core.device, sphMod, NULL);
}
