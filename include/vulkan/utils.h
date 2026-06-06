#ifndef VULKAN_UTILS_H
#define VULKAN_UTILS_H

#include <stdint.h>
#include <vulkan/vulkan.h>

// ============================================================================
// Standard Pipeline State Constants
// ============================================================================

static const VkPipelineColorBlendAttachmentState VK_DEFAULT_COLOR_BLEND_ATTACHMENT = {.colorWriteMask = 0xF, .blendEnable = VK_TRUE, .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA, .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, .colorBlendOp = VK_BLEND_OP_ADD, .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE, .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO, .alphaBlendOp = VK_BLEND_OP_ADD};

static const VkPipelineDepthStencilStateCreateInfo VK_DEPTH_STENCIL_STATE_TEST_WRITE_LESS = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, .depthTestEnable = VK_TRUE, .depthWriteEnable = VK_TRUE, .depthCompareOp = VK_COMPARE_OP_LESS};

static const VkPipelineDepthStencilStateCreateInfo VK_DEPTH_STENCIL_STATE_TEST_NO_WRITE_LESS_EQUAL = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, .depthTestEnable = VK_TRUE, .depthWriteEnable = VK_FALSE, .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL};

static const VkPipelineDepthStencilStateCreateInfo VK_DEPTH_STENCIL_STATE_DISABLED = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, .depthTestEnable = VK_FALSE, .depthWriteEnable = VK_FALSE, .depthCompareOp = VK_COMPARE_OP_ALWAYS};

// ============================================================================
// Shared Macros
// ============================================================================

#define VK_CHECK(res, msg) \
	if (res != VK_SUCCESS) { \
		fprintf(stderr, "Vulkan Error: %s (Result: %d)\n", msg, res); \
		exit(1); \
	}

#ifndef CLAMP
#define CLAMP(x, min, max) ((x) < (min) ? (min) : ((x) > (max) ? (max) : (x)))
#endif

// ============================================================================
// Function Declarations
// ============================================================================

VkResult create_shader_module(VkDevice device, const char *path, VkShaderModule *shaderModule);
void exit_with_error(const char *msg);

#endif
