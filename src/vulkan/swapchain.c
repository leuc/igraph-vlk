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

typedef enum {
	HDR10_STATUS_NO_COLORSPACE_EXTENSION,
	HDR10_STATUS_NO_SURFACE_FORMAT,
	HDR10_STATUS_DISPLAY_UNKNOWN,
	HDR10_STATUS_DISPLAY_UNSUPPORTED,
	HDR10_STATUS_AVAILABLE,
} Hdr10Status;

static Hdr10Status hdr10_status(const VulkanSwapchain *swapchain)
{
	if (!swapchain->hdr10ColorspaceEnabled) {
		return HDR10_STATUS_NO_COLORSPACE_EXTENSION;
	}
	if (!swapchain->hdr10SurfaceSupported) {
		return HDR10_STATUS_NO_SURFACE_FORMAT;
	}
	if (!swapchain->hdr10DisplayKnown) {
		return HDR10_STATUS_DISPLAY_UNKNOWN;
	}
	if (!swapchain->hdr10DisplaySupported) {
		return HDR10_STATUS_DISPLAY_UNSUPPORTED;
	}
	return HDR10_STATUS_AVAILABLE;
}

static void report_hdr10_status(const VulkanSwapchain *swapchain, Hdr10Status status)
{
	switch (status) {
	case HDR10_STATUS_AVAILABLE: {
		const char *formatName = vulkan_surface_format_name(swapchain->hdr10SurfaceFormat.format);
		if (formatName) {
			printf("[Vulkan] HDR10 available on current display set: format=%s, colorSpace=VK_COLOR_SPACE_HDR10_ST2084_EXT\n", formatName);
		} else {
			printf("[Vulkan] HDR10 available on current display set: format=%d, colorSpace=VK_COLOR_SPACE_HDR10_ST2084_EXT\n", (int)swapchain->hdr10SurfaceFormat.format);
		}
		break;
	}
	case HDR10_STATUS_NO_COLORSPACE_EXTENSION:
		printf("[Vulkan] HDR10 unavailable on current display set: VK_EXT_swapchain_colorspace is not enabled\n");
		break;
	case HDR10_STATUS_NO_SURFACE_FORMAT:
		printf("[Vulkan] HDR10 unavailable on current display set: Vulkan surface does not advertise VK_COLOR_SPACE_HDR10_ST2084_EXT\n");
		break;
	case HDR10_STATUS_DISPLAY_UNKNOWN:
		printf("[Vulkan] HDR10 unavailable on current display set: output color state is unknown\n");
		break;
	case HDR10_STATUS_DISPLAY_UNSUPPORTED:
		printf("[Vulkan] HDR10 unavailable on current display set: compositor prefers a non-HDR10 output encoding\n");
		break;
	}
}

static bool update_hdr10_state(VulkanSwapchain *swapchain, const VulkanCore *core, const VkSurfaceFormatKHR *formats, uint32_t count, const DisplayColorInfo *display, bool force_report)
{
	Hdr10Status previous_status = hdr10_status(swapchain);
	VkSurfaceFormatKHR previous_format = swapchain->hdr10SurfaceFormat;
	VulkanOutputMode previous_mode = swapchain->desiredOutputMode;
	uint32_t previous_revision = swapchain->displayColor.revision;

	if (core) {
		VkSurfaceFormatKHR hdr10_format = {.format = VK_FORMAT_UNDEFINED, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
		swapchain->hdr10ColorspaceEnabled = core->swapchainColorspaceEnabled;
		swapchain->hdr10SurfaceSupported = core->swapchainColorspaceEnabled && vulkan_find_hdr10_surface_format(formats, count, &hdr10_format);
		swapchain->hdr10SurfaceFormat = hdr10_format;
	}

	if (display) {
		if (previous_revision != display->revision) {
			swapchain->hdr10Suppressed = false;
		}
		swapchain->displayColor = *display;
		swapchain->hdr10DisplayKnown = display->known;
		swapchain->hdr10DisplaySupported = display->known && display->hdr10;
	}

	swapchain->hdr10Supported = vulkan_hdr10_presentation_supported(swapchain->hdr10SurfaceSupported, swapchain->hdr10DisplayKnown, swapchain->hdr10DisplaySupported);
	swapchain->desiredOutputMode = swapchain->hdr10Supported && !(swapchain->hdr10Suppressed && swapchain->hdr10SuppressedRevision == swapchain->displayColor.revision) ? VULKAN_OUTPUT_HDR10 : VULKAN_OUTPUT_SDR;

	Hdr10Status status = hdr10_status(swapchain);
	bool format_changed = previous_format.format != swapchain->hdr10SurfaceFormat.format || previous_format.colorSpace != swapchain->hdr10SurfaceFormat.colorSpace;
	bool mode_changed = previous_mode != swapchain->desiredOutputMode;
	if (force_report || !swapchain->hdr10StatusReported || status != previous_status || (status == HDR10_STATUS_AVAILABLE && format_changed) || mode_changed) {
		report_hdr10_status(swapchain, status);
		swapchain->hdr10StatusReported = true;
	}

	return mode_changed || (display && previous_revision != display->revision);
}

bool vulkan_swapchain_set_display_color_info(VulkanSwapchain *swapchain, const DisplayColorInfo *info)
{
	if (!info) {
		return false;
	}
	return update_hdr10_state(swapchain, NULL, NULL, 0, info, false);
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
	swapchain->hdr10ColorspaceEnabled = false;
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
	update_hdr10_state(swapchain, core, surfaceFormats, formatCount, NULL, true);

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
	update_hdr10_state(swapchain, core, surfaceFormats, formatCount, NULL, false);

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
