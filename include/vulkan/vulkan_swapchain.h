#ifndef VULKAN_SWAPCHAIN_H
#define VULKAN_SWAPCHAIN_H

#include <vulkan/vulkan.h>

#include "vulkan/vulkan_device.h"

typedef struct {
	VkSwapchainKHR swapchain;
	VkImage *images;
	VkImageView *views;
	uint32_t imageCount;
	VkFormat imageFormat;
	VkExtent2D extent;
} VulkanSwapchain;

void vulkan_swapchain_create(VulkanSwapchain *swap, VulkanCore *core,
							  GLFWwindow *window);
void vulkan_swapchain_destroy(VulkanSwapchain *swap, VkDevice device);
VkSurfaceFormatKHR choose_swap_surface_format(VkSurfaceFormatKHR *formats,
											   uint32_t count);
VkPresentModeKHR choose_swap_present_mode(VkPresentModeKHR *modes,
										   uint32_t count);
VkExtent2D choose_swap_extent(VkSurfaceCapabilitiesKHR *caps,
							   GLFWwindow *window);

#endif // VULKAN_SWAPCHAIN_H
