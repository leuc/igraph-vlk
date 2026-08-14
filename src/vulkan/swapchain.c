/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "vulkan/swapchain.h"

#include <stdio.h>
#include <stdlib.h>

#include <vulkan/vulkan.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "vulkan/images.h"
#include "vulkan/surface_format.h"
#include "vulkan/utils.h"

static void update_effective_hdr10_support(VulkanSwapchain *swapchain, bool report)
{
	bool supported = vulkan_hdr10_presentation_supported(swapchain->hdr10SurfaceSupported, swapchain->hdr10DisplayKnown, swapchain->hdr10DisplaySupported);
	bool changed = supported != swapchain->hdr10Supported;
	swapchain->hdr10Supported = supported;
	if (!report && swapchain->hdr10StatusReported && !changed) {
		return;
	}
	swapchain->hdr10StatusReported = true;

	if (supported) {
		const char *formatName = vulkan_surface_format_name(swapchain->hdr10SurfaceFormat.format);
		if (formatName) {
			printf("[Vulkan] HDR10 available on current display set: format=%s, colorSpace=VK_COLOR_SPACE_HDR10_ST2084_EXT\n", formatName);
		} else {
			printf("[Vulkan] HDR10 available on current display set: format=%d, colorSpace=VK_COLOR_SPACE_HDR10_ST2084_EXT\n", (int)swapchain->hdr10SurfaceFormat.format);
		}
	} else if (!swapchain->hdr10SurfaceSupported) {
		printf("[Vulkan] HDR10 unavailable on current display set: Vulkan surface does not advertise VK_COLOR_SPACE_HDR10_ST2084_EXT\n");
	} else if (!swapchain->hdr10DisplayKnown) {
		printf("[Vulkan] HDR10 unavailable on current display set: output color state is unknown\n");
	} else {
		printf("[Vulkan] HDR10 unavailable on current display set: compositor prefers a non-HDR10 output encoding\n");
	}
}

static void update_hdr10_surface_support(VulkanSwapchain *swapchain, const VulkanCore *core, const VkSurfaceFormatKHR *formats, uint32_t count, bool report)
{
	VkSurfaceFormatKHR hdr10Format = {.format = VK_FORMAT_UNDEFINED, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
	bool supported = core->swapchainColorspaceEnabled && vulkan_find_hdr10_surface_format(formats, count, &hdr10Format);
	bool changed = supported != swapchain->hdr10SurfaceSupported;
	if (supported && swapchain->hdr10SurfaceSupported) {
		changed = hdr10Format.format != swapchain->hdr10SurfaceFormat.format || hdr10Format.colorSpace != swapchain->hdr10SurfaceFormat.colorSpace;
	}

	swapchain->hdr10SurfaceSupported = supported;
	swapchain->hdr10SurfaceFormat = hdr10Format;
	if (!report && !changed) {
		return;
	}

	if (supported) {
		const char *formatName = vulkan_surface_format_name(hdr10Format.format);
		if (formatName) {
			printf("[Vulkan] HDR10 surface format available: format=%s, colorSpace=VK_COLOR_SPACE_HDR10_ST2084_EXT\n", formatName);
		} else {
			printf("[Vulkan] HDR10 surface format available: format=%d, colorSpace=VK_COLOR_SPACE_HDR10_ST2084_EXT\n", (int)hdr10Format.format);
		}
	} else if (!core->swapchainColorspaceEnabled) {
		printf("[Vulkan] HDR10 surface format unavailable: VK_EXT_swapchain_colorspace is not enabled\n");
	} else {
		printf("[Vulkan] HDR10 surface format unavailable: surface does not advertise VK_COLOR_SPACE_HDR10_ST2084_EXT\n");
	}
	if (swapchain->hdr10StatusReported) {
		update_effective_hdr10_support(swapchain, true);
	}
}

void vulkan_swapchain_set_display_hdr10_support(VulkanSwapchain *swapchain, bool known, bool supported)
{
	bool changed = known != swapchain->hdr10DisplayKnown || supported != swapchain->hdr10DisplaySupported;
	swapchain->hdr10DisplayKnown = known;
	swapchain->hdr10DisplaySupported = known && supported;
	update_effective_hdr10_support(swapchain, changed);
}

VkPresentModeKHR choose_swap_present_mode(VkPresentModeKHR *modes, uint32_t count)
{
	for (uint32_t i = 0; i < count; i++) {
		if (modes[i] == VK_PRESENT_MODE_FIFO_KHR)
			return modes[i];
	}
	return VK_PRESENT_MODE_MAILBOX_KHR;
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
	swapchain->imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
	swapchain->hdr10SurfaceSupported = false;
	swapchain->hdr10DisplayKnown = false;
	swapchain->hdr10DisplaySupported = false;
	swapchain->hdr10StatusReported = false;
	swapchain->hdr10Supported = false;
	swapchain->hdr10SurfaceFormat = (VkSurfaceFormatKHR){.format = VK_FORMAT_UNDEFINED, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};

	VkSurfaceCapabilitiesKHR capabilities;
	VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(core->physicalDevice, core->surface, &capabilities), "Failed to get physical device surface capabilities");

	uint32_t formatCount;
	VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(core->physicalDevice, core->surface, &formatCount, NULL), "Failed to get physical device surface formats (count)");
	VkSurfaceFormatKHR *surfaceFormats = malloc(sizeof(VkSurfaceFormatKHR) * formatCount);
	VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(core->physicalDevice, core->surface, &formatCount, surfaceFormats), "Failed to get physical device surface formats");
	update_hdr10_surface_support(swapchain, core, surfaceFormats, formatCount, true);

	uint32_t presentModeCount;
	VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(core->physicalDevice, core->surface, &presentModeCount, NULL), "Failed to get physical device surface present modes (count)");
	VkPresentModeKHR *presentModes = malloc(sizeof(VkPresentModeKHR) * presentModeCount);
	VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(core->physicalDevice, core->surface, &presentModeCount, presentModes), "Failed to get physical device surface present modes");

	VkSurfaceFormatKHR surfaceFormat = choose_swap_surface_format(surfaceFormats, formatCount);
	VkPresentModeKHR presentMode = choose_swap_present_mode(presentModes, presentModeCount);
	VkExtent2D extent = choose_swap_extent(&capabilities, window);

	uint32_t imageCount = capabilities.minImageCount + 1;
	if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount)
		imageCount = capabilities.maxImageCount;

	VkSwapchainCreateInfoKHR swapchainInfo = {.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, .surface = core->surface, .minImageCount = imageCount, .imageFormat = surfaceFormat.format, .imageColorSpace = surfaceFormat.colorSpace, .imageExtent = extent, .imageArrayLayers = 1, .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE, .preTransform = capabilities.currentTransform, .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, .presentMode = presentMode, .clipped = VK_TRUE};

	VK_CHECK(vkCreateSwapchainKHR(core->device, &swapchainInfo, NULL, &swapchain->swapchain), "Failed to create swapchain");

	swapchain->imageFormat = surfaceFormat.format;
	swapchain->imageColorSpace = surfaceFormat.colorSpace;
	swapchain->extent = extent;
	swapchain->imageCount = imageCount;

	VK_CHECK(vkGetSwapchainImagesKHR(core->device, swapchain->swapchain, &imageCount, NULL), "Failed to get swapchain images (count)");
	swapchain->images = malloc(sizeof(VkImage) * imageCount);
	VK_CHECK(vkGetSwapchainImagesKHR(core->device, swapchain->swapchain, &imageCount, swapchain->images), "Failed to get swapchain images");

	swapchain->views = malloc(sizeof(VkImageView) * imageCount);
	for (uint32_t i = 0; i < imageCount; i++) {
		VK_CHECK(vkCreateImageView(core->device, &VK_IMAGE_VIEW_2D(swapchain->images[i], swapchain->imageFormat, VK_IMAGE_ASPECT_COLOR_BIT), NULL, &swapchain->views[i]), "Failed to create image views");
	}

	// Depth Buffer
	swapchain->depthFormat = VK_FORMAT_D32_SFLOAT;
	create_image(core->device, core->physicalDevice, extent.width, extent.height, swapchain->depthFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &swapchain->depthImage, &swapchain->depthMemory);

	VK_CHECK(vkCreateImageView(core->device, &VK_IMAGE_VIEW_2D(swapchain->depthImage, swapchain->depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT), NULL, &swapchain->depthView), "Failed to create depth image view");

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

void vulkan_swapchain_recreate(VulkanSwapchain *swapchain, VulkanCore *core, GLFWwindow *window)
{
	VkSwapchainKHR oldSwapchain = swapchain->swapchain;

	// Destroy old depth buffer
	vkDestroyImageView(core->device, swapchain->depthView, NULL);
	vkDestroyImage(core->device, swapchain->depthImage, NULL);
	vkFreeMemory(core->device, swapchain->depthMemory, NULL);
	swapchain->depthView = VK_NULL_HANDLE;
	swapchain->depthImage = VK_NULL_HANDLE;
	swapchain->depthMemory = VK_NULL_HANDLE;

	// Destroy old image views
	for (uint32_t i = 0; i < swapchain->imageCount; i++) {
		if (swapchain->views[i] != VK_NULL_HANDLE)
			vkDestroyImageView(core->device, swapchain->views[i], NULL);
	}
	free(swapchain->views);
	free(swapchain->images);

	// Query surface capabilities for new extent
	VkSurfaceCapabilitiesKHR capabilities;
	VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(core->physicalDevice, core->surface, &capabilities), "Failed to get physical device surface capabilities");

	VkExtent2D extent = choose_swap_extent(&capabilities, window);

	// Choose format and present mode (same logic as initial create)
	uint32_t formatCount;
	VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(core->physicalDevice, core->surface, &formatCount, NULL), "Failed to get physical device surface formats (count)");
	VkSurfaceFormatKHR *surfaceFormats = malloc(sizeof(VkSurfaceFormatKHR) * formatCount);
	VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(core->physicalDevice, core->surface, &formatCount, surfaceFormats), "Failed to get physical device surface formats");
	update_hdr10_surface_support(swapchain, core, surfaceFormats, formatCount, false);

	uint32_t presentModeCount;
	VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(core->physicalDevice, core->surface, &presentModeCount, NULL), "Failed to get physical device surface present modes (count)");
	VkPresentModeKHR *presentModes = malloc(sizeof(VkPresentModeKHR) * presentModeCount);
	VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(core->physicalDevice, core->surface, &presentModeCount, presentModes), "Failed to get physical device surface present modes");

	VkSurfaceFormatKHR surfaceFormat = choose_swap_surface_format(surfaceFormats, formatCount);
	VkPresentModeKHR presentMode = choose_swap_present_mode(presentModes, presentModeCount);

	uint32_t imageCount = capabilities.minImageCount + 1;
	if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount)
		imageCount = capabilities.maxImageCount;

	// Create new swapchain with old swapchain as hint for driver optimization
	VkSwapchainCreateInfoKHR swapchainInfo = {
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface = core->surface,
		.minImageCount = imageCount,
		.imageFormat = surfaceFormat.format,
		.imageColorSpace = surfaceFormat.colorSpace,
		.imageExtent = extent,
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.preTransform = capabilities.currentTransform,
		.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.presentMode = presentMode,
		.clipped = VK_TRUE,
		.oldSwapchain = oldSwapchain,
	};
	VK_CHECK(vkCreateSwapchainKHR(core->device, &swapchainInfo, NULL, &swapchain->swapchain), "Failed to recreate swapchain");

	// Destroy old swapchain handle now that new one is created
	if (oldSwapchain != VK_NULL_HANDLE)
		vkDestroySwapchainKHR(core->device, oldSwapchain, NULL);

	swapchain->imageFormat = surfaceFormat.format;
	swapchain->imageColorSpace = surfaceFormat.colorSpace;
	swapchain->extent = extent;
	swapchain->imageCount = imageCount;

	// Get new swapchain images and create image views
	VK_CHECK(vkGetSwapchainImagesKHR(core->device, swapchain->swapchain, &imageCount, NULL), "Failed to get swapchain images (count)");
	swapchain->images = malloc(sizeof(VkImage) * imageCount);
	VK_CHECK(vkGetSwapchainImagesKHR(core->device, swapchain->swapchain, &imageCount, swapchain->images), "Failed to get swapchain images");

	swapchain->views = malloc(sizeof(VkImageView) * imageCount);
	for (uint32_t i = 0; i < imageCount; i++) {
		VK_CHECK(vkCreateImageView(core->device, &VK_IMAGE_VIEW_2D(swapchain->images[i], swapchain->imageFormat, VK_IMAGE_ASPECT_COLOR_BIT), NULL, &swapchain->views[i]), "Failed to create image views");
	}

	// Create new depth buffer with new extent
	create_image(core->device, core->physicalDevice, extent.width, extent.height, swapchain->depthFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &swapchain->depthImage, &swapchain->depthMemory);

	VK_CHECK(vkCreateImageView(core->device, &VK_IMAGE_VIEW_2D(swapchain->depthImage, swapchain->depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT), NULL, &swapchain->depthView), "Failed to create depth image view");

	free(surfaceFormats);
	free(presentModes);
}
