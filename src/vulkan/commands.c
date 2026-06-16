/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "vulkan/commands.h"

#include <stdio.h>
#include <stdlib.h>

#include <vulkan/vulkan.h>

#include "vulkan/utils.h"

void vulkan_commands_create(VulkanCommands *cmds, VulkanCore *core, uint32_t imageCount)
{
	cmds->commandBuffers = NULL;
	cmds->imageAvailableSemaphores = NULL;
	cmds->renderFinishedSemaphores = NULL;
	cmds->inFlightFences = NULL;
	cmds->currentFrame = 0;
	cmds->imageCount = imageCount;

	// Create command pool
	VkCommandPoolCreateInfo poolInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = core->graphicsQueueFamily, .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT};
	VK_CHECK(vkCreateCommandPool(core->device, &poolInfo, NULL, &cmds->commandPool), "Failed to create command pool");

	// Allocate command buffers
	cmds->commandBuffers = malloc(sizeof(VkCommandBuffer) * MAX_FRAMES_IN_FLIGHT);
	VkCommandBufferAllocateInfo allocInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = cmds->commandPool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = MAX_FRAMES_IN_FLIGHT};
	VK_CHECK(vkAllocateCommandBuffers(core->device, &allocInfo, cmds->commandBuffers), "Failed to allocate command buffers");
	// Create synchronization primitives
	cmds->imageAvailableSemaphores = malloc(sizeof(VkSemaphore) * MAX_FRAMES_IN_FLIGHT);
	cmds->renderFinishedSemaphores = malloc(sizeof(VkSemaphore) * imageCount);
	cmds->inFlightFences = malloc(sizeof(VkFence) * MAX_FRAMES_IN_FLIGHT);

	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		VK_CHECK(vkCreateSemaphore(core->device, &VK_SEMAPHORE_INFO, NULL, &cmds->imageAvailableSemaphores[i]), "Failed to create image available semaphore");
		VK_CHECK(vkCreateFence(core->device, &VK_SIGNALED_FENCE_INFO, NULL, &cmds->inFlightFences[i]), "Failed to create in-flight fence");
	}

	for (uint32_t i = 0; i < imageCount; i++) {
		VK_CHECK(vkCreateSemaphore(core->device, &VK_SEMAPHORE_INFO, NULL, &cmds->renderFinishedSemaphores[i]), "Failed to create render finished semaphore");
	}
}

void vulkan_commands_destroy(VulkanCommands *cmds, VkDevice device)
{
	VK_CHECK(vkDeviceWaitIdle(device), "Failed to wait for device idle before command destruction");
	if (cmds->imageAvailableSemaphores) {
		for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
			if (cmds->imageAvailableSemaphores[i] != VK_NULL_HANDLE)
				vkDestroySemaphore(device, cmds->imageAvailableSemaphores[i], NULL);
			if (cmds->inFlightFences[i] != VK_NULL_HANDLE)
				vkDestroyFence(device, cmds->inFlightFences[i], NULL);
		}
		for (uint32_t i = 0; i < cmds->imageCount; i++) {
			if (cmds->renderFinishedSemaphores[i] != VK_NULL_HANDLE)
				vkDestroySemaphore(device, cmds->renderFinishedSemaphores[i], NULL);
		}
		free(cmds->imageAvailableSemaphores);
		free(cmds->renderFinishedSemaphores);
		free(cmds->inFlightFences);
		cmds->imageAvailableSemaphores = NULL;
		cmds->renderFinishedSemaphores = NULL;
		cmds->inFlightFences = NULL;
	}

	if (cmds->commandBuffers) {
		vkFreeCommandBuffers(device, cmds->commandPool, MAX_FRAMES_IN_FLIGHT, cmds->commandBuffers);
		free(cmds->commandBuffers);
		cmds->commandBuffers = NULL;
	}

	if (cmds->commandPool != VK_NULL_HANDLE) {
		vkDestroyCommandPool(device, cmds->commandPool, NULL);
		cmds->commandPool = VK_NULL_HANDLE;
	}
}

VkCommandBuffer begin_single_time_commands(VkDevice device, VkCommandPool commandPool)
{
	VkCommandBufferAllocateInfo allocInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandPool = commandPool, .commandBufferCount = 1};
	VkCommandBuffer commandBuffer;
	VK_CHECK(vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer), "Failed to allocate one-time command buffer");
	VK_CHECK(vkBeginCommandBuffer(commandBuffer, &VK_CMD_BEGIN_INFO_ONETIME), "Failed to begin one-time command buffer");
	return commandBuffer;
}

void end_single_time_commands(VkDevice device, VkCommandPool commandPool, VkQueue queue, VkCommandBuffer commandBuffer)
{
	VK_CHECK(vkEndCommandBuffer(commandBuffer), "Failed to end one-time command buffer");
	VkSubmitInfo submitInfo = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &commandBuffer};
	VK_CHECK(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE), "Failed to submit one-time command buffer");
	VK_CHECK(vkQueueWaitIdle(queue), "Failed to wait for one-time command queue idle");
	vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}
