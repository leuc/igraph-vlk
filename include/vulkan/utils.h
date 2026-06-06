#ifndef VULKAN_UTILS_H
#define VULKAN_UTILS_H

#include <stdint.h>
#include <vulkan/vulkan.h>

// ============================================================================
// Standard Pipeline State Constants (Point 7 & 8: Reduce Duplication)
// ============================================================================

// Standard alpha blending attachment state - used by all graphics pipelines
static const VkPipelineColorBlendAttachmentState VK_DEFAULT_COLOR_BLEND_ATTACHMENT = {.colorWriteMask = 0xF, .blendEnable = VK_TRUE, .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA, .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, .colorBlendOp = VK_BLEND_OP_ADD, .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE, .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO, .alphaBlendOp = VK_BLEND_OP_ADD};

// Depth-stencil states for different rendering modes
static const VkPipelineDepthStencilStateCreateInfo VK_DEPTH_STENCIL_STATE_TEST_WRITE_LESS = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, .depthTestEnable = VK_TRUE, .depthWriteEnable = VK_TRUE, .depthCompareOp = VK_COMPARE_OP_LESS};

static const VkPipelineDepthStencilStateCreateInfo VK_DEPTH_STENCIL_STATE_TEST_NO_WRITE_LESS_EQUAL = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, .depthTestEnable = VK_TRUE, .depthWriteEnable = VK_FALSE, .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL};

static const VkPipelineDepthStencilStateCreateInfo VK_DEPTH_STENCIL_STATE_DISABLED = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, .depthTestEnable = VK_FALSE, .depthWriteEnable = VK_FALSE, .depthCompareOp = VK_COMPARE_OP_ALWAYS};

// ============================================================================
// Sub-module Headers
// ============================================================================

#include "vulkan/vk_buffers.h"
#include "vulkan/vk_images.h"
#include "vulkan/vk_utils.h"
#include "vulkan/vulkan_commands.h"

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

#endif
