#ifndef VULKAN_COMMANDS_H
#define VULKAN_COMMANDS_H

#include "vulkan/vulkan_types.h"

void vulkan_commands_create(VulkanCommands *cmds, VulkanCore *core, uint32_t imageCount);
void vulkan_commands_destroy(VulkanCommands *cmds, VkDevice device);

#endif // VULKAN_COMMANDS_H
