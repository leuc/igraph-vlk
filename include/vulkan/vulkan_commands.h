#ifndef VULKAN_COMMANDS_H
#define VULKAN_COMMANDS_H

#include <vulkan/vulkan.h>

#include "vulkan/vulkan_device.h"

#define MAX_FRAMES_IN_FLIGHT 2

typedef struct {
	VkCommandPool commandPool;
	VkCommandBuffer *commandBuffers;
	VkSemaphore *imageAvailableSemaphores;
	VkSemaphore *renderFinishedSemaphores;
	VkFence *inFlightFences;
	uint32_t currentFrame;
} VulkanCommands;

void vulkan_commands_create(VulkanCommands *cmds, VulkanCore *core,
							 uint32_t imageCount);
void vulkan_commands_destroy(VulkanCommands *cmds, VkDevice device);

#endif // VULKAN_COMMANDS_H
