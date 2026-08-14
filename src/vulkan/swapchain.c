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

#include "vulkan/color_space.h"
#include "vulkan/surface_format.h"
#include "vulkan/utils.h"

static void update_effective_hdr10_support(VulkanSwapchain *swapchain, bool report)
{
	bool supported = vulkan_hdr10_presentation_supported(swapchain->hdr10SurfaceSupported, swapchain->hdr10DisplayKnown, swapchain->hdr10DisplaySupported);
	bool changed = supported != swapchain->hdr10Supported;
	swapchain->hdr10Supported = supported;
	swapchain->desiredOutputMode = supported && !(swapchain->hdr10Suppressed && swapchain->hdr10SuppressedRevision == swapchain->displayColor.revision) ? VULKAN_OUTPUT_HDR10 : VULKAN_OUTPUT_SDR;
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

bool vulkan_swapchain_set_display_color_info(VulkanSwapchain *swapchain, const DisplayColorInfo *info)
{
	VulkanOutputMode previous = swapchain->desiredOutputMode;
	if (!info) {
		return false;
	}
	bool revision_changed = swapchain->displayColor.revision != info->revision;
	bool changed = swapchain->hdr10DisplayKnown != info->known || swapchain->hdr10DisplaySupported != (info->known && info->hdr10);
	if (revision_changed) {
		swapchain->hdr10Suppressed = false;
	}
	swapchain->displayColor = *info;
	swapchain->hdr10DisplayKnown = info->known;
	swapchain->hdr10DisplaySupported = info->known && info->hdr10;
	update_effective_hdr10_support(swapchain, changed);
	return revision_changed || previous != swapchain->desiredOutputMode;
}

static ColorPrimaries metadata_primaries(const DisplayColorInfo *display)
{
	if (display->has_target_primaries) {
		return display->target_primaries;
	}
	if (display->has_primaries) {
		return display->primaries;
	}
	return (ColorPrimaries){.r_x = 0.708f, .r_y = 0.292f, .g_x = 0.170f, .g_y = 0.797f, .b_x = 0.131f, .b_y = 0.046f, .w_x = 0.3127f, .w_y = 0.3290f};
}

static void vulkan_swapchain_apply_hdr_metadata(VulkanSwapchain *swapchain, VulkanCore *core)
{
	if (swapchain->outputMode != VULKAN_OUTPUT_HDR10 || !core->hdrMetadataEnabled || !core->setHdrMetadata) {
		return;
	}
	RendererColorState color = renderer_color_state(VULKAN_OUTPUT_HDR10, &swapchain->displayColor);
	ColorPrimaries p = metadata_primaries(&swapchain->displayColor);
	float min_luminance = swapchain->displayColor.has_target_luminance ? swapchain->displayColor.target_min_luminance : swapchain->displayColor.min_luminance;
	float max_fall = swapchain->displayColor.target_max_fall > 0.0f && swapchain->displayColor.target_max_fall < color.reference_nits ? swapchain->displayColor.target_max_fall : color.reference_nits;
	VkHdrMetadataEXT metadata = {
		.sType = VK_STRUCTURE_TYPE_HDR_METADATA_EXT,
		.displayPrimaryRed = {p.r_x, p.r_y},
		.displayPrimaryGreen = {p.g_x, p.g_y},
		.displayPrimaryBlue = {p.b_x, p.b_y},
		.whitePoint = {p.w_x, p.w_y},
		.maxLuminance = color.peak_nits,
		.minLuminance = min_luminance,
		.maxContentLightLevel = color.highlight_nits,
		.maxFrameAverageLightLevel = max_fall,
	};
	core->setHdrMetadata(core->device, 1, &swapchain->swapchain, &metadata);
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
	swapchain->depthFormat = VK_FORMAT_D32_SFLOAT;
	swapchain->hdr10SurfaceSupported = false;
	swapchain->hdr10DisplayKnown = false;
	swapchain->hdr10DisplaySupported = false;
	swapchain->hdr10StatusReported = false;
	swapchain->hdr10Supported = false;
	swapchain->hdr10SurfaceFormat = (VkSurfaceFormatKHR){.format = VK_FORMAT_UNDEFINED, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
	swapchain->displayColor = (DisplayColorInfo){0};
	swapchain->outputMode = VULKAN_OUTPUT_SDR;
	swapchain->desiredOutputMode = VULKAN_OUTPUT_SDR;
	swapchain->hdr10Suppressed = false;
	swapchain->hdr10SuppressedRevision = 0;

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

	VkSurfaceFormatKHR surfaceFormat = vulkan_choose_output_surface_format(surfaceFormats, formatCount, swapchain->desiredOutputMode == VULKAN_OUTPUT_HDR10);
	VkPresentModeKHR presentMode = choose_swap_present_mode(presentModes, presentModeCount);
	VkExtent2D extent = choose_swap_extent(&capabilities, window);

	uint32_t imageCount = capabilities.minImageCount + 1;
	if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount)
		imageCount = capabilities.maxImageCount;

	VkSwapchainCreateInfoKHR swapchainInfo = {.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, .surface = core->surface, .minImageCount = imageCount, .imageFormat = surfaceFormat.format, .imageColorSpace = surfaceFormat.colorSpace, .imageExtent = extent, .imageArrayLayers = 1, .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE, .preTransform = capabilities.currentTransform, .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, .presentMode = presentMode, .clipped = VK_TRUE};

	VkResult createResult = vkCreateSwapchainKHR(core->device, &swapchainInfo, NULL, &swapchain->swapchain);
	if (createResult != VK_SUCCESS && swapchain->desiredOutputMode == VULKAN_OUTPUT_HDR10) {
		fprintf(stderr, "[Vulkan] HDR10 swapchain creation failed (%d); falling back to SDR for display revision %u\n", createResult, swapchain->displayColor.revision);
		swapchain->hdr10Suppressed = true;
		swapchain->hdr10SuppressedRevision = swapchain->displayColor.revision;
		swapchain->desiredOutputMode = VULKAN_OUTPUT_SDR;
		surfaceFormat = choose_swap_surface_format(surfaceFormats, formatCount);
		swapchainInfo.imageFormat = surfaceFormat.format;
		swapchainInfo.imageColorSpace = surfaceFormat.colorSpace;
		createResult = vkCreateSwapchainKHR(core->device, &swapchainInfo, NULL, &swapchain->swapchain);
	}
	VK_CHECK(createResult, "Failed to create swapchain");

	swapchain->imageFormat = surfaceFormat.format;
	swapchain->imageColorSpace = surfaceFormat.colorSpace;
	swapchain->extent = extent;
	swapchain->imageCount = imageCount;
	swapchain->outputMode = surfaceFormat.colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT ? VULKAN_OUTPUT_HDR10 : VULKAN_OUTPUT_SDR;

	VK_CHECK(vkGetSwapchainImagesKHR(core->device, swapchain->swapchain, &imageCount, NULL), "Failed to get swapchain images (count)");
	swapchain->images = malloc(sizeof(VkImage) * imageCount);
	VK_CHECK(vkGetSwapchainImagesKHR(core->device, swapchain->swapchain, &imageCount, swapchain->images), "Failed to get swapchain images");
	swapchain->imageCount = imageCount;

	swapchain->views = malloc(sizeof(VkImageView) * imageCount);
	for (uint32_t i = 0; i < imageCount; i++) {
		VK_CHECK(vkCreateImageView(core->device, &VK_IMAGE_VIEW_2D(swapchain->images[i], swapchain->imageFormat, VK_IMAGE_ASPECT_COLOR_BIT), NULL, &swapchain->views[i]), "Failed to create image views");
	}

	free(surfaceFormats);
	free(presentModes);
	vulkan_swapchain_apply_hdr_metadata(swapchain, core);
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

	VkSurfaceFormatKHR surfaceFormat = vulkan_choose_output_surface_format(surfaceFormats, formatCount, swapchain->desiredOutputMode == VULKAN_OUTPUT_HDR10);
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
	VkResult createResult = vkCreateSwapchainKHR(core->device, &swapchainInfo, NULL, &swapchain->swapchain);
	if (createResult != VK_SUCCESS && swapchain->desiredOutputMode == VULKAN_OUTPUT_HDR10) {
		fprintf(stderr, "[Vulkan] HDR10 swapchain creation failed (%d); falling back to SDR for display revision %u\n", createResult, swapchain->displayColor.revision);
		swapchain->hdr10Suppressed = true;
		swapchain->hdr10SuppressedRevision = swapchain->displayColor.revision;
		swapchain->desiredOutputMode = VULKAN_OUTPUT_SDR;
		surfaceFormat = choose_swap_surface_format(surfaceFormats, formatCount);
		swapchainInfo.imageFormat = surfaceFormat.format;
		swapchainInfo.imageColorSpace = surfaceFormat.colorSpace;
		createResult = vkCreateSwapchainKHR(core->device, &swapchainInfo, NULL, &swapchain->swapchain);
	}
	VK_CHECK(createResult, "Failed to recreate swapchain");

	// Destroy old swapchain handle now that new one is created
	if (oldSwapchain != VK_NULL_HANDLE)
		vkDestroySwapchainKHR(core->device, oldSwapchain, NULL);

	swapchain->imageFormat = surfaceFormat.format;
	swapchain->imageColorSpace = surfaceFormat.colorSpace;
	swapchain->extent = extent;
	swapchain->imageCount = imageCount;
	swapchain->outputMode = surfaceFormat.colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT ? VULKAN_OUTPUT_HDR10 : VULKAN_OUTPUT_SDR;

	// Get new swapchain images and create image views
	VK_CHECK(vkGetSwapchainImagesKHR(core->device, swapchain->swapchain, &imageCount, NULL), "Failed to get swapchain images (count)");
	swapchain->images = malloc(sizeof(VkImage) * imageCount);
	VK_CHECK(vkGetSwapchainImagesKHR(core->device, swapchain->swapchain, &imageCount, swapchain->images), "Failed to get swapchain images");
	swapchain->imageCount = imageCount;

	swapchain->views = malloc(sizeof(VkImageView) * imageCount);
	for (uint32_t i = 0; i < imageCount; i++) {
		VK_CHECK(vkCreateImageView(core->device, &VK_IMAGE_VIEW_2D(swapchain->images[i], swapchain->imageFormat, VK_IMAGE_ASPECT_COLOR_BIT), NULL, &swapchain->views[i]), "Failed to create image views");
	}

	free(surfaceFormats);
	free(presentModes);
	vulkan_swapchain_apply_hdr_metadata(swapchain, core);
}
