/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "vulkan/surface_format.h"

VkSurfaceFormatKHR choose_swap_surface_format(const VkSurfaceFormatKHR *formats, uint32_t count)
{
	if (!formats || count == 0) {
		return (VkSurfaceFormatKHR){.format = VK_FORMAT_UNDEFINED, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
	}

	for (uint32_t i = 0; i < count; i++) {
		if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM && formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
			return formats[i];
		}
	}
	return formats[0];
}

bool vulkan_find_hdr10_surface_format(const VkSurfaceFormatKHR *formats, uint32_t count, VkSurfaceFormatKHR *out_format)
{
	if (out_format) {
		*out_format = (VkSurfaceFormatKHR){.format = VK_FORMAT_UNDEFINED, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
	}
	if (!formats || count == 0) {
		return false;
	}

	for (uint32_t i = 0; i < count; i++) {
		if (formats[i].colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT) {
			if (out_format) {
				*out_format = formats[i];
			}
			return true;
		}
	}
	return false;
}

const char *vulkan_surface_format_name(VkFormat format)
{
	switch (format) {
	case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
		return "VK_FORMAT_A2B10G10R10_UNORM_PACK32";
	case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
		return "VK_FORMAT_A2R10G10B10_UNORM_PACK32";
	case VK_FORMAT_R16G16B16A16_SFLOAT:
		return "VK_FORMAT_R16G16B16A16_SFLOAT";
	default:
		return NULL;
	}
}
