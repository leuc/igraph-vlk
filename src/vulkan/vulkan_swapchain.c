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

void vulkan_swapchain_create(VulkanSwapchain *swapchain, VulkanCore *core, GLFWwindow *window)
{
	swapchain->images = NULL;
	swapchain->views = NULL;
	swapchain->swapchain = VK_NULL_HANDLE;
	swapchain->depthImage = VK_NULL_HANDLE;
	swapchain->depthView = VK_NULL_HANDLE;
	swapchain->depthMemory = VK_NULL_HANDLE;

	VkSurfaceCapabilitiesKHR capabilities;
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(core->physicalDevice, core->surface, &capabilities);

	uint32_t formatCount;
	vkGetPhysicalDeviceSurfaceFormatsKHR(core->physicalDevice, core->surface, &formatCount, NULL);
	VkSurfaceFormatKHR *surfaceFormats = malloc(sizeof(VkSurfaceFormatKHR) * formatCount);
	vkGetPhysicalDeviceSurfaceFormatsKHR(core->physicalDevice, core->surface, &formatCount, surfaceFormats);

	uint32_t presentModeCount;
	vkGetPhysicalDeviceSurfacePresentModesKHR(core->physicalDevice, core->surface, &presentModeCount, NULL);
	VkPresentModeKHR *presentModes = malloc(sizeof(VkPresentModeKHR) * presentModeCount);
	vkGetPhysicalDeviceSurfacePresentModesKHR(core->physicalDevice, core->surface, &presentModeCount, presentModes);

	VkSurfaceFormatKHR surfaceFormat = choose_swap_surface_format(surfaceFormats, formatCount);
	VkPresentModeKHR presentMode = choose_swap_present_mode(presentModes, presentModeCount);
	VkExtent2D extent = choose_swap_extent(&capabilities, window);

	uint32_t imageCount = capabilities.minImageCount + 1;
	if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount)
		imageCount = capabilities.maxImageCount;

	VkSwapchainCreateInfoKHR swapchainInfo = {.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, .surface = core->surface, .minImageCount = imageCount, .imageFormat = surfaceFormat.format, .imageColorSpace = surfaceFormat.colorSpace, .imageExtent = extent, .imageArrayLayers = 1, .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE, .preTransform = capabilities.currentTransform, .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, .presentMode = presentMode, .clipped = VK_TRUE};

	VK_CHECK(vkCreateSwapchainKHR(core->device, &swapchainInfo, NULL, &swapchain->swapchain), "Failed to create swapchain");

	swapchain->imageFormat = surfaceFormat.format;
	swapchain->extent = extent;
	swapchain->imageCount = imageCount;

	vkGetSwapchainImagesKHR(core->device, swapchain->swapchain, &imageCount, NULL);
	swapchain->images = malloc(sizeof(VkImage) * imageCount);
	vkGetSwapchainImagesKHR(core->device, swapchain->swapchain, &imageCount, swapchain->images);

	swapchain->views = malloc(sizeof(VkImageView) * imageCount);
	for (uint32_t i = 0; i < imageCount; i++) {
		VkImageViewCreateInfo imageViewInfo = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = swapchain->images[i], .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = swapchain->imageFormat, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
		VK_CHECK(vkCreateImageView(core->device, &imageViewInfo, NULL, &swapchain->views[i]), "Failed to create image views");
	}

	// Depth Buffer
	swapchain->depthFormat = VK_FORMAT_D32_SFLOAT;
	createImage(core->device, core->physicalDevice, extent.width, extent.height, swapchain->depthFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &swapchain->depthImage, &swapchain->depthMemory);

	VkImageViewCreateInfo depthViewInfo = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = swapchain->depthImage, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = swapchain->depthFormat, .subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1}};
	VK_CHECK(vkCreateImageView(core->device, &depthViewInfo, NULL, &swapchain->depthView), "Failed to create depth image view");

	free(surfaceFormats);
	free(presentModes);
}

void vulkan_swapchain_destroy(VulkanSwapchain *swapchain, VkDevice device)
{
	if (swapchain->depthView != VK_NULL_HANDLE)
		vkDestroyImageView(device, swapchain->depthView, NULL);
	if (swapchain->depthImage != VK_NULL_HANDLE)
		vkDestroyImage(device, swapchain->depthImage, NULL);
	if (swapchain->depthMemory != VK_NULL_HANDLE)
		vkFreeMemory(device, swapchain->depthMemory, NULL);

	if (swapchain->views) {
		for (uint32_t i = 0; i < swapchain->imageCount; i++) {
			if (swapchain->views[i] != VK_NULL_HANDLE)
				vkDestroyImageView(device, swapchain->views[i], NULL);
		}
		free(swapchain->views);
	}
	if (swapchain->images)
		free(swapchain->images);
	if (swapchain->swapchain != VK_NULL_HANDLE)
		vkDestroySwapchainKHR(device, swapchain->swapchain, NULL);
}
