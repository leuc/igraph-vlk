/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef VULKAN_SWAPCHAIN_H
#define VULKAN_SWAPCHAIN_H

#include "vulkan/surface_format.h"
#include "vulkan/vulkan_types.h"

void vulkan_swapchain_create(VulkanSwapchain *swapchain, VulkanCore *core, GLFWwindow *window);
void vulkan_swapchain_recreate(VulkanSwapchain *swapchain, VulkanCore *core, GLFWwindow *window);
void vulkan_swapchain_destroy(VulkanSwapchain *swapchain, VkDevice device);
void vulkan_swapchain_set_display_hdr10_support(VulkanSwapchain *swapchain, bool known, bool supported);
VkPresentModeKHR choose_swap_present_mode(VkPresentModeKHR *modes, uint32_t count);
VkExtent2D choose_swap_extent(VkSurfaceCapabilitiesKHR *caps, GLFWwindow *window);

#endif // VULKAN_SWAPCHAIN_H
