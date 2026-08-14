/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef VULKAN_SURFACE_FORMAT_H
#define VULKAN_SURFACE_FORMAT_H

#include <stdbool.h>
#include <stdint.h>

#include <vulkan/vulkan.h>

VkSurfaceFormatKHR choose_swap_surface_format(const VkSurfaceFormatKHR *formats, uint32_t count);
VkSurfaceFormatKHR vulkan_choose_output_surface_format(const VkSurfaceFormatKHR *formats, uint32_t count, bool hdr10);
bool vulkan_find_hdr10_surface_format(const VkSurfaceFormatKHR *formats, uint32_t count, VkSurfaceFormatKHR *out_format);
bool vulkan_hdr10_presentation_supported(bool surface_supported, bool display_known, bool display_supported);
const char *vulkan_surface_format_name(VkFormat format);

#endif // VULKAN_SURFACE_FORMAT_H
