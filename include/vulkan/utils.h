/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef VULKAN_UTILS_H
#define VULKAN_UTILS_H

#include <stdint.h>
#include <vulkan/vulkan.h>

// Standard Pipeline State Constants

static const VkPipelineColorBlendAttachmentState VK_DEFAULT_COLOR_BLEND_ATTACHMENT = {.colorWriteMask = 0xF, .blendEnable = VK_TRUE, .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA, .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, .colorBlendOp = VK_BLEND_OP_ADD, .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE, .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO, .alphaBlendOp = VK_BLEND_OP_ADD};

static const VkPipelineDepthStencilStateCreateInfo VK_DEPTH_STENCIL_STATE_TEST_WRITE_LESS = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, .depthTestEnable = VK_TRUE, .depthWriteEnable = VK_TRUE, .depthCompareOp = VK_COMPARE_OP_LESS};

static const VkPipelineDepthStencilStateCreateInfo VK_DEPTH_STENCIL_STATE_TEST_NO_WRITE_LESS_EQUAL = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, .depthTestEnable = VK_TRUE, .depthWriteEnable = VK_FALSE, .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL};

static const VkPipelineDepthStencilStateCreateInfo VK_DEPTH_STENCIL_STATE_DISABLED = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, .depthTestEnable = VK_FALSE, .depthWriteEnable = VK_FALSE, .depthCompareOp = VK_COMPARE_OP_ALWAYS};

// Shared Macros

#define VK_CHECK(res, msg) \
	if (res != VK_SUCCESS) { \
		fprintf(stderr, "Vulkan Error: %s (Result: %d)\n", msg, res); \
		exit(1); \
	}

#ifndef CLAMP
#define CLAMP(x, min, max) ((x) < (min) ? (min) : ((x) > (max) ? (max) : (x)))
#endif

#define VK_ALIGN_UP(size, align) (((size) + (align) - 1) & ~((align) - 1))

#define VK_DESTROY_BUFFER(dev, buf, mem) \
	do { \
		if ((buf) != VK_NULL_HANDLE) { \
			vkDestroyBuffer(dev, buf, NULL); \
			(buf) = VK_NULL_HANDLE; \
		} \
		if ((mem) != VK_NULL_HANDLE) { \
			vkFreeMemory(dev, mem, NULL); \
			(mem) = VK_NULL_HANDLE; \
		} \
	} while (0)

#define VK_CREATE_HOST_BUFFER(dev, phys, size, usage, pBuf, pMem) create_buffer(dev, phys, size, usage, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, pBuf, pMem)

#define VK_WRITE_DESC_BUFFER(set, binding, bufInfo, type) ((VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, set, binding, 0, 1, type, NULL, bufInfo, NULL})

#define VK_WRITE_DESC_IMAGE(set, binding, imgInfo, type) ((VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, set, binding, 0, 1, type, imgInfo, NULL, NULL})

#define VK_SHADER_STAGE_VERT(mod) ((VkPipelineShaderStageCreateInfo){VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, mod, "main", NULL})

#define VK_SHADER_STAGE_FRAG(mod) ((VkPipelineShaderStageCreateInfo){VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, mod, "main", NULL})

#define VK_SHADER_STAGE_COMP(mod) ((VkPipelineShaderStageCreateInfo){.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = mod, .pName = "main"})

#define VK_PIPELINE_COMMON_STATE_DECLS() \
	VkPipelineViewportStateCreateInfo viewportState = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1}; \
	VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR}; \
	VkPipelineDynamicStateCreateInfo dynamicState = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, .dynamicStateCount = 2, .pDynamicStates = dynamicStates}; \
	VkPipelineRasterizationStateCreateInfo rasterizationState = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, .polygonMode = VK_POLYGON_MODE_FILL, .lineWidth = 1.0f, .cullMode = VK_CULL_MODE_NONE, .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE}; \
	VkPipelineMultisampleStateCreateInfo multisampleState = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT}; \
	VkPipelineColorBlendAttachmentState colorBlendAttachment = VK_DEFAULT_COLOR_BLEND_ATTACHMENT; \
	VkPipelineColorBlendStateCreateInfo colorBlendState = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1, .pAttachments = &colorBlendAttachment}

#define VK_IMAGE_VIEW_2D(img, fmt, aspect) ((VkImageViewCreateInfo){.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = img, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = fmt, .subresourceRange = {aspect, 0, 1, 0, 1}})

#define VK_SIGNALED_FENCE_INFO ((VkFenceCreateInfo){.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT})

#define VK_SEMAPHORE_INFO ((VkSemaphoreCreateInfo){.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO})

#define VK_FRAMEBUFFER_INFO(rp, views, w, h) ((VkFramebufferCreateInfo){.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, .renderPass = rp, .attachmentCount = 2, .pAttachments = views, .width = w, .height = h, .layers = 1})

#define VK_CMD_BEGIN_INFO ((VkCommandBufferBeginInfo){.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO})

#define VK_CMD_BEGIN_INFO_ONETIME ((VkCommandBufferBeginInfo){.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT})

// Function Declarations

VkResult create_shader_module(VkDevice device, const char *path, VkShaderModule *shaderModule);
void exit_with_error(const char *msg);

#endif
