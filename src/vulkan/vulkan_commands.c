#include "vulkan/vulkan_commands.h"

#include <stdlib.h>

#include <vulkan/vulkan.h>

#include "vulkan/utils.h"

void vulkan_commands_create(VulkanCommands *cmds, VulkanCore *core,
							 uint32_t imageCount) {
	cmds->commandBuffers = NULL;
	cmds->imageAvailableSemaphores = NULL;
	cmds->renderFinishedSemaphores = NULL;
	cmds->inFlightFences = NULL;
	cmds->currentFrame = 0;

	// Create command pool
	VkCommandPoolCreateInfo poolInfo = {
		VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		NULL,
		0,
		core->graphicsQueueFamily};
	VK_CHECK(vkCreateCommandPool(core->device, &poolInfo, NULL, &cmds->commandPool),
			 "Failed to create command pool");

	// Allocate command buffers
	cmds->commandBuffers = malloc(sizeof(VkCommandBuffer) * MAX_FRAMES_IN_FLIGHT);
	VkCommandBufferAllocateInfo allocInfo = {
		VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		NULL,
		VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		cmds->commandPool,
		MAX_FRAMES_IN_FLIGHT};
	VK_CHECK(vkAllocateCommandBuffers(core->device, &allocInfo, cmds->commandBuffers),
			 "Failed to allocate command buffers");

	// Create synchronization primitives
	cmds->imageAvailableSemaphores =
		malloc(sizeof(VkSemaphore) * MAX_FRAMES_IN_FLIGHT);
	cmds->renderFinishedSemaphores =
		malloc(sizeof(VkSemaphore) * MAX_FRAMES_IN_FLIGHT);
	cmds->inFlightFences = malloc(sizeof(VkFence) * MAX_FRAMES_IN_FLIGHT);

	VkSemaphoreCreateInfo semaphoreInfo = {
		VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, NULL, 0};
	VkFenceCreateInfo fenceInfo = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, NULL,
								   VK_FENCE_CREATE_SIGNALED_BIT};

	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		VK_CHECK(vkCreateSemaphore(core->device, &semaphoreInfo, NULL,
								   &cmds->imageAvailableSemaphores[i]),
				 "Failed to create image available semaphore");
		VK_CHECK(vkCreateSemaphore(core->device, &semaphoreInfo, NULL,
								   &cmds->renderFinishedSemaphores[i]),
				 "Failed to create render finished semaphore");
		VK_CHECK(vkCreateFence(core->device, &fenceInfo, NULL, &cmds->inFlightFences[i]),
				 "Failed to create fence");
	}
}

void vulkan_commands_destroy(VulkanCommands *cmds, VkDevice device) {
	// Destroy synchronization primitives
	if (cmds->imageAvailableSemaphores) {
		for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
			if (cmds->imageAvailableSemaphores[i] != VK_NULL_HANDLE)
				vkDestroySemaphore(device, cmds->imageAvailableSemaphores[i], NULL);
			if (cmds->renderFinishedSemaphores[i] != VK_NULL_HANDLE)
				vkDestroySemaphore(device, cmds->renderFinishedSemaphores[i], NULL);
			if (cmds->inFlightFences[i] != VK_NULL_HANDLE)
				vkDestroyFence(device, cmds->inFlightFences[i], NULL);
		}
		free(cmds->imageAvailableSemaphores);
		free(cmds->renderFinishedSemaphores);
		free(cmds->inFlightFences);
	}

	// Free command buffers
	if (cmds->commandBuffers)
		free(cmds->commandBuffers);

	// Destroy command pool
	if (cmds->commandPool != VK_NULL_HANDLE)
		vkDestroyCommandPool(device, cmds->commandPool, NULL);
}
