/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "test_utilities.h"
#include "vulkan/surface_format.h"

static int test_empty_formats(void)
{
	VkSurfaceFormatKHR hdr10 = {.format = VK_FORMAT_R8G8B8A8_UNORM, .colorSpace = VK_COLOR_SPACE_HDR10_ST2084_EXT};
	IGRAPH_ASSERT(!vulkan_find_hdr10_surface_format(NULL, 0, &hdr10));
	IGRAPH_ASSERT(hdr10.format == VK_FORMAT_UNDEFINED);
	IGRAPH_ASSERT(hdr10.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);

	VkSurfaceFormatKHR selected = choose_swap_surface_format(NULL, 0);
	IGRAPH_ASSERT(selected.format == VK_FORMAT_UNDEFINED);
	IGRAPH_ASSERT(selected.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);
	return 0;
}

static int test_sdr_only_formats(void)
{
	const VkSurfaceFormatKHR formats[] = {
		{.format = VK_FORMAT_B8G8R8A8_UNORM, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
		{.format = VK_FORMAT_A2B10G10R10_UNORM_PACK32, .colorSpace = VK_COLOR_SPACE_BT2020_LINEAR_EXT},
		{.format = VK_FORMAT_A2R10G10B10_UNORM_PACK32, .colorSpace = VK_COLOR_SPACE_HDR10_HLG_EXT},
	};
	VkSurfaceFormatKHR hdr10;
	IGRAPH_ASSERT(!vulkan_find_hdr10_surface_format(formats, 3, &hdr10));
	IGRAPH_ASSERT(hdr10.format == VK_FORMAT_UNDEFINED);
	return 0;
}

static int test_hdr10_format_detection(void)
{
	const VkSurfaceFormatKHR formats[] = {
		{.format = VK_FORMAT_B8G8R8A8_UNORM, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
		{.format = VK_FORMAT_R16G16B16A16_SFLOAT, .colorSpace = VK_COLOR_SPACE_HDR10_ST2084_EXT},
		{.format = VK_FORMAT_A2R10G10B10_UNORM_PACK32, .colorSpace = VK_COLOR_SPACE_HDR10_ST2084_EXT},
		{.format = VK_FORMAT_A2B10G10R10_UNORM_PACK32, .colorSpace = VK_COLOR_SPACE_HDR10_ST2084_EXT},
	};
	VkSurfaceFormatKHR hdr10;
	IGRAPH_ASSERT(vulkan_find_hdr10_surface_format(formats, 4, &hdr10));
	IGRAPH_ASSERT(hdr10.format == VK_FORMAT_A2B10G10R10_UNORM_PACK32);
	IGRAPH_ASSERT(hdr10.colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT);
	IGRAPH_ASSERT(vulkan_find_hdr10_surface_format(formats, 4, NULL));

	const VkSurfaceFormatKHR without_a2b10[] = {
		{.format = VK_FORMAT_R16G16B16A16_SFLOAT, .colorSpace = VK_COLOR_SPACE_HDR10_ST2084_EXT},
		{.format = VK_FORMAT_A2R10G10B10_UNORM_PACK32, .colorSpace = VK_COLOR_SPACE_HDR10_ST2084_EXT},
	};
	IGRAPH_ASSERT(vulkan_find_hdr10_surface_format(without_a2b10, 2, &hdr10));
	IGRAPH_ASSERT(hdr10.format == VK_FORMAT_A2R10G10B10_UNORM_PACK32);
	return 0;
}

static int test_driver_advertised_hdr10_format(void)
{
	const VkSurfaceFormatKHR formats[] = {
		{.format = VK_FORMAT_R5G6B5_UNORM_PACK16, .colorSpace = VK_COLOR_SPACE_HDR10_ST2084_EXT},
	};
	VkSurfaceFormatKHR hdr10;
	IGRAPH_ASSERT(vulkan_find_hdr10_surface_format(formats, 1, &hdr10));
	IGRAPH_ASSERT(hdr10.format == VK_FORMAT_R5G6B5_UNORM_PACK16);
	IGRAPH_ASSERT(vulkan_surface_format_name(hdr10.format) == NULL);
	return 0;
}

static int test_float_hdr10_output_format(void)
{
	const VkSurfaceFormatKHR formats[] = {
		{.format = VK_FORMAT_B8G8R8A8_UNORM, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
		{.format = VK_FORMAT_R16G16B16A16_SFLOAT, .colorSpace = VK_COLOR_SPACE_HDR10_ST2084_EXT},
	};
	VkSurfaceFormatKHR selected = vulkan_choose_output_surface_format(formats, 2, true);
	IGRAPH_ASSERT(selected.format == VK_FORMAT_R16G16B16A16_SFLOAT);
	IGRAPH_ASSERT(selected.colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT);
	return 0;
}

static int test_sdr_surface_format_selection(void)
{
	const VkSurfaceFormatKHR formats[] = {
		{.format = VK_FORMAT_R8G8B8A8_UNORM, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
		{.format = VK_FORMAT_B8G8R8A8_UNORM, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
		{.format = VK_FORMAT_A2B10G10R10_UNORM_PACK32, .colorSpace = VK_COLOR_SPACE_HDR10_ST2084_EXT},
	};
	VkSurfaceFormatKHR selected = choose_swap_surface_format(formats, 3);
	IGRAPH_ASSERT(selected.format == VK_FORMAT_B8G8R8A8_UNORM);
	IGRAPH_ASSERT(selected.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);

	selected = choose_swap_surface_format(formats, 1);
	IGRAPH_ASSERT(selected.format == formats[0].format);
	IGRAPH_ASSERT(selected.colorSpace == formats[0].colorSpace);

	selected = vulkan_choose_output_surface_format(formats, 3, true);
	IGRAPH_ASSERT(selected.format == VK_FORMAT_A2B10G10R10_UNORM_PACK32);
	IGRAPH_ASSERT(selected.colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT);
	selected = vulkan_choose_output_surface_format(formats, 3, false);
	IGRAPH_ASSERT(selected.format == VK_FORMAT_B8G8R8A8_UNORM);
	return 0;
}

static int test_effective_hdr10_support(void)
{
	IGRAPH_ASSERT(vulkan_hdr10_presentation_supported(true, true, true));
	IGRAPH_ASSERT(!vulkan_hdr10_presentation_supported(false, true, true));
	IGRAPH_ASSERT(!vulkan_hdr10_presentation_supported(true, false, true));
	IGRAPH_ASSERT(!vulkan_hdr10_presentation_supported(true, true, false));
	return 0;
}

int main(void)
{
	RUN_TEST(test_empty_formats);
	RUN_TEST(test_sdr_only_formats);
	RUN_TEST(test_hdr10_format_detection);
	RUN_TEST(test_driver_advertised_hdr10_format);
	RUN_TEST(test_float_hdr10_output_format);
	RUN_TEST(test_sdr_surface_format_selection);
	RUN_TEST(test_effective_hdr10_support);
	return 0;
}
