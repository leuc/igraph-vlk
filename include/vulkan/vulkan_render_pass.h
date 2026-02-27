#ifndef VULKAN_RENDER_PASS_H
#define VULKAN_RENDER_PASS_H

#include <vulkan/vulkan.h>

#include "vulkan/vulkan_device.h"
#include "vulkan/vulkan_swapchain.h"

typedef struct {
	VkRenderPass renderPass;
	VkFramebuffer *framebuffers;
} VulkanRenderPass;

void vulkan_render_pass_create(VulkanRenderPass *pass, VulkanCore *core,
								VulkanSwapchain *swap);
void vulkan_render_pass_destroy(VulkanRenderPass *pass, VkDevice device);

#endif // VULKAN_RENDER_PASS_H
