#ifndef VULKAN_DEVICE_H
#define VULKAN_DEVICE_H

#include <vulkan/vulkan.h>

typedef struct {
	VkInstance instance;
	VkPhysicalDevice physicalDevice;
	VkDevice device;
	VkQueue graphicsQueue;
	VkQueue presentQueue;
	VkSurfaceKHR surface;
} VulkanCore;

void vulkan_device_create(VulkanCore *core, GLFWwindow *window);
void vulkan_device_destroy(VulkanCore *core);

#endif // VULKAN_DEVICE_H
