#ifndef VULKAN_DEVICE_H
#define VULKAN_DEVICE_H

#include "vulkan/vulkan_types.h"

void vulkan_device_create(VulkanCore *core, GLFWwindow *window, void *xr);
void vulkan_device_destroy(VulkanCore *core);

#endif // VULKAN_DEVICE_H
