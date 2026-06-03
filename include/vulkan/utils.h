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
// Function Declarations
// ============================================================================

uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties);
void createBuffer(VkDevice device, VkPhysicalDevice physicalDevice, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer *buffer, VkDeviceMemory *bufferMemory);
void createStagingBuffer(VkDevice device, VkPhysicalDevice physicalDevice, VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer *stagingBuf, VkDeviceMemory *stagingMem, VkBuffer *deviceBuf, VkDeviceMemory *deviceMem);
void createMappedBuffer(VkDevice device, VkPhysicalDevice physicalDevice, VkDeviceSize size, VkBuffer *buffer, VkDeviceMemory *memory);
void updateBuffer(VkDevice device, VkDeviceMemory memory, VkDeviceSize size, const void *data);
void updateBufferMapped(VkDevice device, VkDeviceMemory memory, VkDeviceSize size, const void *data, const VkPhysicalDeviceProperties *deviceProps);
void updateBufferStaged(VkDevice device, VkCommandPool commandPool, VkQueue queue, VkDeviceSize size, const void *data, VkBuffer stagingBuf, VkDeviceMemory stagingMem, VkBuffer deviceBuf, const VkPhysicalDeviceProperties *deviceProps);
void createImage(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage *image, VkDeviceMemory *imageMemory);
void transitionImageLayout(VkDevice device, VkCommandPool commandPool, VkQueue graphicsQueue, VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout);

// Point 9: One-time submit command buffer utilities
VkCommandBuffer begin_single_time_commands(VkDevice device, VkCommandPool commandPool);
void end_single_time_commands(VkDevice device, VkCommandPool commandPool, VkQueue queue, VkCommandBuffer commandBuffer);

VkResult create_shader_module(VkDevice device, const char *path, VkShaderModule *shaderModule);

#define VK_CHECK(res, msg) \
	if (res != VK_SUCCESS) { \
		fprintf(stderr, "Vulkan Error: %s (Result: %d)\n", msg, res); \
		exit(1); \
	}

void exit_with_error(const char *msg);

#ifndef CLAMP
#define CLAMP(x, min, max) ((x) < (min) ? (min) : ((x) > (max) ? (max) : (x)))
#endif

#endif