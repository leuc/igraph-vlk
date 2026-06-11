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
	VkPipelineShaderStageCreateInfo computeStageSpherical = VK_SHADER_STAGE_COMP(sphericalShaderModule);
	VkComputePipelineCreateInfo computePipelineInfoSpherical = {.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, .stage = computeStageSpherical, .layout = r->computePipelineLayout};
	VK_CHECK(vkCreateComputePipelines(r->core.device, VK_NULL_HANDLE, 1, &computePipelineInfoSpherical, NULL, &r->computeSphericalPipeline), "Failed to create compute spherical pipeline");
	vkDestroyShaderModule(r->core.device, sphericalShaderModule, NULL);

	renderer_create_splc_compute_pipeline(r);
	renderer_create_escape_compute_pipelines(r);
}

void renderer_create_splc_compute_pipeline(Renderer *r)
{
	VkDescriptorSetLayoutBinding splcBindings[] = {
		{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}, {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}, {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}, {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}, {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
	};
	VkDescriptorSetLayoutCreateInfo splcLayoutInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 5, .pBindings = splcBindings};
	VK_CHECK(vkCreateDescriptorSetLayout(r->core.device, &splcLayoutInfo, NULL, &r->splc_compute_descriptor_set_layout), "Failed to create SPLC compute descriptor set layout");

	VkPushConstantRange splcPushConstant = {.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0, .size = sizeof(uint32_t)};
	VkPipelineLayoutCreateInfo splcPipelineLayoutInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1, .pSetLayouts = &r->splc_compute_descriptor_set_layout, .pushConstantRangeCount = 1, .pPushConstantRanges = &splcPushConstant};
	VK_CHECK(vkCreatePipelineLayout(r->core.device, &splcPipelineLayoutInfo, NULL, &r->splc_compute_pipeline_layout), "Failed to create SPLC compute pipeline layout");

	VkShaderModule splcShaderModule = VK_NULL_HANDLE;
	VK_CHECK(create_shader_module(r->core.device, SPLC_COMP_SHADER_PATH, &splcShaderModule), "Failed to create SPLC compute shader module");
	VkPipelineShaderStageCreateInfo splcStage = VK_SHADER_STAGE_COMP(splcShaderModule);
	VkComputePipelineCreateInfo splcPipelineInfo = {.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, .stage = splcStage, .layout = r->splc_compute_pipeline_layout};
	VK_CHECK(vkCreateComputePipelines(r->core.device, VK_NULL_HANDLE, 1, &splcPipelineInfo, NULL, &r->splc_compute_pipeline), "Failed to create SPLC compute pipeline");
	vkDestroyShaderModule(r->core.device, splcShaderModule, NULL);

	VkDescriptorPoolSize splcPoolSizes = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 5};
	VkDescriptorPoolCreateInfo splcPoolInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &splcPoolSizes};
	VK_CHECK(vkCreateDescriptorPool(r->core.device, &splcPoolInfo, NULL, &r->splc_descriptor_pool), "Failed to create SPLC descriptor pool");

	VkDescriptorSetAllocateInfo splcDescSetInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = r->splc_descriptor_pool, .descriptorSetCount = 1, .pSetLayouts = &r->splc_compute_descriptor_set_layout};
	VK_CHECK(vkAllocateDescriptorSets(r->core.device, &splcDescSetInfo, &r->splc_descriptor_set), "Failed to allocate SPLC descriptor set");
}

void renderer_create_escape_compute_pipelines(Renderer *r)
{
	// Physics pipeline: 4 bindings (physics rw, neighbors ro, offsets ro, counts ro)
	VkDescriptorSetLayoutBinding physicsBindings[] = {
		{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
		{1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
		{2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
		{3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
	};
	VkDescriptorSetLayoutCreateInfo physicsLayoutInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 4, .pBindings = physicsBindings};
	VK_CHECK(vkCreateDescriptorSetLayout(r->core.device, &physicsLayoutInfo, NULL, &r->escape_physics_desc_layout), "Failed to create escape physics desc set layout");

	VkPushConstantRange physicsPushConstant = {.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0, .size = sizeof(float) * 2 + sizeof(uint32_t)};
	VkPipelineLayoutCreateInfo physicsPipelineLayoutInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1, .pSetLayouts = &r->escape_physics_desc_layout, .pushConstantRangeCount = 1, .pPushConstantRanges = &physicsPushConstant};
	VK_CHECK(vkCreatePipelineLayout(r->core.device, &physicsPipelineLayoutInfo, NULL, &r->escape_physics_pipeline_layout), "Failed to create escape physics pipeline layout");

	VkShaderModule physicsModule = VK_NULL_HANDLE;
	VK_CHECK(create_shader_module(r->core.device, PHYSICS_STEP_COMP_SHADER_PATH, &physicsModule), "Failed to create escape physics shader module");
	VkPipelineShaderStageCreateInfo physicsStage = VK_SHADER_STAGE_COMP(physicsModule);
	VkComputePipelineCreateInfo physicsPipelineInfo = {.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, .stage = physicsStage, .layout = r->escape_physics_pipeline_layout};
	VK_CHECK(vkCreateComputePipelines(r->core.device, VK_NULL_HANDLE, 1, &physicsPipelineInfo, NULL, &r->escape_physics_pipeline), "Failed to create escape physics pipeline");
	vkDestroyShaderModule(r->core.device, physicsModule, NULL);

	// Stress pipeline: 3 bindings (positions ro, edges ro, global_stress rw)
	VkDescriptorSetLayoutBinding stressBindings[] = {
		{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
		{1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
		{2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
	};
	VkDescriptorSetLayoutCreateInfo stressLayoutInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 3, .pBindings = stressBindings};
	VK_CHECK(vkCreateDescriptorSetLayout(r->core.device, &stressLayoutInfo, NULL, &r->escape_stress_desc_layout), "Failed to create escape stress desc set layout");

	VkPushConstantRange stressPushConstant = {.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0, .size = sizeof(float) + sizeof(uint32_t)};
	VkPipelineLayoutCreateInfo stressPipelineLayoutInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1, .pSetLayouts = &r->escape_stress_desc_layout, .pushConstantRangeCount = 1, .pPushConstantRanges = &stressPushConstant};
	VK_CHECK(vkCreatePipelineLayout(r->core.device, &stressPipelineLayoutInfo, NULL, &r->escape_stress_pipeline_layout), "Failed to create escape stress pipeline layout");

	VkShaderModule stressModule = VK_NULL_HANDLE;
	VK_CHECK(create_shader_module(r->core.device, STRESS_CALC_COMP_SHADER_PATH, &stressModule), "Failed to create escape stress shader module");
	VkPipelineShaderStageCreateInfo stressStage = VK_SHADER_STAGE_COMP(stressModule);
	VkComputePipelineCreateInfo stressPipelineInfo = {.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, .stage = stressStage, .layout = r->escape_stress_pipeline_layout};
	VK_CHECK(vkCreateComputePipelines(r->core.device, VK_NULL_HANDLE, 1, &stressPipelineInfo, NULL, &r->escape_stress_pipeline), "Failed to create escape stress pipeline");
	vkDestroyShaderModule(r->core.device, stressModule, NULL);

	// Descriptor pool (4 physics + 3 stress = 7 total)
	VkDescriptorPoolSize escapePoolSizes[] = {
		{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 7},
	};
	VkDescriptorPoolCreateInfo escapePoolInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 2, .poolSizeCount = 1, .pPoolSizes = escapePoolSizes};
	VK_CHECK(vkCreateDescriptorPool(r->core.device, &escapePoolInfo, NULL, &r->escape_descriptor_pool), "Failed to create escape descriptor pool");

	VkDescriptorSetLayout bothLayouts[2] = {r->escape_physics_desc_layout, r->escape_stress_desc_layout};
	VkDescriptorSetAllocateInfo escapeDescSetInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = r->escape_descriptor_pool, .descriptorSetCount = 2, .pSetLayouts = bothLayouts};
	VkDescriptorSet sets[2];
	VK_CHECK(vkAllocateDescriptorSets(r->core.device, &escapeDescSetInfo, sets), "Failed to allocate escape descriptor sets");
	r->escape_physics_desc_set = sets[0];
	r->escape_stress_desc_set = sets[1];

	r->escape_initialized = VK_FALSE;
}
