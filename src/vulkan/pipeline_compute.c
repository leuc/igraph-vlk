#include "vulkan/pipeline_compute.h"

#include <stdio.h>
#include <stdlib.h>

#include "vulkan/renderer_pipelines.h"
#include "vulkan/utils.h"

int pipeline_compute_create(Renderer *r) {
    VkDevice device = r->device;

    // =========================================================================
    // COMPUTE PIPELINE SETUP
    // =========================================================================
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
    VkResult result = vkCreateDescriptorSetLayout(device, &cLayInfo, NULL,
                                                  &r->computeDescriptorSetLayout);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to create compute descriptor set layout\n");
        return -1;
    }

    VkPushConstantRange cPush = {.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                                 .offset = 0,
                                 .size = sizeof(int) * 3};
    VkPipelineLayoutCreateInfo cPlyLayInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &r->computeDescriptorSetLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &cPush};
    result = vkCreatePipelineLayout(device, &cPlyLayInfo, NULL,
                                   &r->computePipelineLayout);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to create compute pipeline layout\n");
        return -1;
    }

    VkShaderModule sphMod = VK_NULL_HANDLE, hubMod = VK_NULL_HANDLE;
    create_shader_module(device, ROUTING_COMP_SHADER_PATH, &sphMod);
    create_shader_module(device, ROUTING_HUB_COMP_SHADER_PATH, &hubMod);

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

    result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpInfoSph,
                                     NULL, &r->computeSphericalPipeline);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to create spherical compute pipeline\n");
        return -1;
    }

    result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpInfoHub,
                                     NULL, &r->computeHubSpokePipeline);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to create hub-spoke compute pipeline\n");
        return -1;
    }

    vkDestroyShaderModule(device, sphMod, NULL);
    vkDestroyShaderModule(device, hubMod, NULL);

    return 0;
}
