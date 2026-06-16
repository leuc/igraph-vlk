/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef VULKAN_COMMANDS_H
#define VULKAN_COMMANDS_H

#include "vulkan/vulkan_types.h"

void vulkan_commands_create(VulkanCommands *cmds, VulkanCore *core, uint32_t imageCount);
void vulkan_commands_destroy(VulkanCommands *cmds, VkDevice device);

VkCommandBuffer begin_single_time_commands(VkDevice device, VkCommandPool commandPool);
void end_single_time_commands(VkDevice device, VkCommandPool commandPool, VkQueue queue, VkCommandBuffer commandBuffer);

#endif // VULKAN_COMMANDS_H
