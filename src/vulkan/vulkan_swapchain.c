#include "vulkan/vulkan_swapchain.h"

#include <stdio.h>
#include <stdlib.h>

#include <vulkan/vulkan.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "vulkan/utils.h"

VkSurfaceFormatKHR choose_swap_surface_format(VkSurfaceFormatKHR *formats, uint32_t count)
{
	for (uint32_t i = 0; i < count; i++) {
		if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM && formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
			return formats[i];
		}
	}
	return formats[0];
}

VkPresentModeKHR choose_swap_present_mode(VkPresentModeKHR *modes, uint32_t count)
{
	for (uint32_t i = 0; i < count; i++) {
		if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
			return modes[i];
	}
	return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D choose_swap_extent(VkSurfaceCapabilitiesKHR *caps, GLFWwindow *window)
{
	if (caps->currentExtent.width != UINT32_MAX) {
		return caps->currentExtent;
	}
	int width, height;
	glfwGetFramebufferSize(window, &width, &height);
	VkExtent2D actualExtent = {(uint32_t)width, (uint32_t)height};
	actualExtent.width = CLAMP(actualExtent.width, caps->minImageExtent.width, caps->maxImageExtent.width);
	actualExtent.height = CLAMP(actualExtent.height, caps->minImageExtent.height, caps->maxImageExtent.height);
	return actualExtent;
}

void vulkan_swapchain_create(VulkanSwapchain *swap, VulkanCore *core, GLFWwindow *window)
{
	swap->images = NULL;
	swap->views = NULL;
	swap->swapchain = VK_NULL_HANDLE;
	swap->depthImage = VK_NULL_HANDLE;
	swap->depthView = VK_NULL_HANDLE;
	swap->depthMemory = VK_NULL_HANDLE;

	VkSurfaceCapabilitiesKHR caps;
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(core->physicalDevice, core->surface, &caps);

	uint32_t formatCount;
	vkGetPhysicalDeviceSurfaceFormatsKHR(core->physicalDevice, core->surface, &formatCount, NULL);
	VkSurfaceFormatKHR *surfFormats = malloc(sizeof(VkSurfaceFormatKHR) * formatCount);
	vkGetPhysicalDeviceSurfaceFormatsKHR(core->physicalDevice, core->surface, &formatCount, surfFormats);

	uint32_t presentModeCount;
	vkGetPhysicalDeviceSurfacePresentModesKHR(core->physicalDevice, core->surface, &presentModeCount, NULL);
	VkPresentModeKHR *presentModes = malloc(sizeof(VkPresentModeKHR) * presentModeCount);
	vkGetPhysicalDeviceSurfacePresentModesKHR(core->physicalDevice, core->surface, &presentModeCount, presentModes);

	VkSurfaceFormatKHR surfaceFormat = choose_swap_surface_format(surfFormats, formatCount);
	VkPresentModeKHR presentMode = choose_swap_present_mode(presentModes, presentModeCount);
	VkExtent2D extent = choose_swap_extent(&caps, window);

	uint32_t imageCount = caps.minImageCount + 1;
	if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount)
		imageCount = caps.maxImageCount;

	VkSwapchainCreateInfoKHR swpInfo = {.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, .surface = core->surface, .minImageCount = imageCount, .imageFormat = surfaceFormat.format, .imageColorSpace = surfaceFormat.colorSpace, .imageExtent = extent, .imageArrayLayers = 1, .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE, .preTransform = caps.currentTransform, .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, .presentMode = presentMode, .clipped = VK_TRUE};

	VK_CHECK(vkCreateSwapchainKHR(core->device, &swpInfo, NULL, &swap->swapchain), "Failed to create swapchain");

	swap->imageFormat = surfaceFormat.format;
	swap->extent = extent;
	swap->imageCount = imageCount;

	vkGetSwapchainImagesKHR(core->device, swap->swapchain, &imageCount, NULL);
	swap->images = malloc(sizeof(VkImage) * imageCount);
	vkGetSwapchainImagesKHR(core->device, swap->swapchain, &imageCount, swap->images);

	swap->views = malloc(sizeof(VkImageView) * imageCount);
	for (uint32_t i = 0; i < imageCount; i++) {
		VkImageViewCreateInfo viewInfo = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = swap->images[i], .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = swap->imageFormat, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
		VK_CHECK(vkCreateImageView(core->device, &viewInfo, NULL, &swap->views[i]), "Failed to create image views");
	}

	// Depth Buffer
	swap->depthFormat = VK_FORMAT_D32_SFLOAT;
	createImage(core->device, core->physicalDevice, extent.width, extent.height, swap->depthFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &swap->depthImage, &swap->depthMemory);

	VkImageViewCreateInfo depthViewInfo = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = swap->depthImage, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = swap->depthFormat, .subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1}};
	VK_CHECK(vkCreateImageView(core->device, &depthViewInfo, NULL, &swap->depthView), "Failed to create depth image view");

	free(surfFormats);
	free(presentModes);
}

void vulkan_swapchain_destroy(VulkanSwapchain *swap, VkDevice device)
{
	if (swap->depthView != VK_NULL_HANDLE)
		vkDestroyImageView(device, swap->depthView, NULL);
	if (swap->depthImage != VK_NULL_HANDLE)
		vkDestroyImage(device, swap->depthImage, NULL);
	if (swap->depthMemory != VK_NULL_HANDLE)
		vkFreeMemory(device, swap->depthMemory, NULL);

	if (swap->views) {
		for (uint32_t i = 0; i < swap->imageCount; i++) {
			if (swap->views[i] != VK_NULL_HANDLE)
				vkDestroyImageView(device, swap->views[i], NULL);
		}
		free(swap->views);
	}
	if (swap->images)
		free(swap->images);
	if (swap->swapchain != VK_NULL_HANDLE)
		vkDestroySwapchainKHR(device, swap->swapchain, NULL);
}
