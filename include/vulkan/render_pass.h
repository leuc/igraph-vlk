/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef VULKAN_RENDER_PASS_H
#define VULKAN_RENDER_PASS_H

#include "vulkan/vulkan_types.h"

void vulkan_render_pass_create(VulkanRenderPass *pass, VulkanCore *core, VulkanSwapchain *swapchain);
void vulkan_render_pass_destroy(VulkanRenderPass *pass, VkDevice device);

#endif // VULKAN_RENDER_PASS_H
