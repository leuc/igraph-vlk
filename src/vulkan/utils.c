#include "vulkan/utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter,
						VkMemoryPropertyFlags properties) {
	VkPhysicalDeviceMemoryProperties memProperties;
	vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
	for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
		if ((typeFilter & (1 << i)) &&
			(memProperties.memoryTypes[i].propertyFlags & properties) ==
				properties)
			return i;
	}
	return 0;
}

void createBuffer(VkDevice device, VkPhysicalDevice physicalDevice,
				  VkDeviceSize size, VkBufferUsageFlags usage,
				  VkMemoryPropertyFlags properties, VkBuffer *buffer,
				  VkDeviceMemory *bufferMemory) {
	VkBufferCreateInfo bufferInfo = {.sType =
										 VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
									 .size = size,
									 .usage = usage,
									 .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
	vkCreateBuffer(device, &bufferInfo, NULL, buffer);
	VkMemoryRequirements memReqs;
	vkGetBufferMemoryRequirements(device, *buffer, &memReqs);
	VkMemoryAllocateInfo allocInfo = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = memReqs.size,
		.memoryTypeIndex =
			findMemoryType(physicalDevice, memReqs.memoryTypeBits, properties)};
	vkAllocateMemory(device, &allocInfo, NULL, bufferMemory);
	vkBindBufferMemory(device, *buffer, *bufferMemory, 0);
}

void updateBuffer(VkDevice device, VkDeviceMemory memory, VkDeviceSize size,
				  const void *data) {
	void *mapped;
	vkMapMemory(device, memory, 0, size, 0, &mapped);
	memcpy(mapped, data, size);
	vkUnmapMemory(device, memory);
}

void createImage(VkDevice device, VkPhysicalDevice physicalDevice,
				 uint32_t width, uint32_t height, VkFormat format,
				 VkImageTiling tiling, VkImageUsageFlags usage,
				 VkMemoryPropertyFlags properties, VkImage *image,
				 VkDeviceMemory *imageMemory) {
	VkImageCreateInfo imageInfo = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
								   .imageType = VK_IMAGE_TYPE_2D,
								   .extent = {width, height, 1},
								   .mipLevels = 1,
								   .arrayLayers = 1,
								   .format = format,
								   .tiling = tiling,
								   .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
								   .usage = usage,
								   .samples = VK_SAMPLE_COUNT_1_BIT,
								   .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
	vkCreateImage(device, &imageInfo, NULL, image);
	VkMemoryRequirements memReqs;
	vkGetImageMemoryRequirements(device, *image, &memReqs);
	VkMemoryAllocateInfo allocInfo = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = memReqs.size,
		.memoryTypeIndex =
			findMemoryType(physicalDevice, memReqs.memoryTypeBits, properties)};
	vkAllocateMemory(device, &allocInfo, NULL, imageMemory);
	vkBindImageMemory(device, *image, *imageMemory, 0);
}

void transitionImageLayout(VkDevice device, VkCommandPool commandPool,
						   VkQueue graphicsQueue, VkImage image,
						   VkFormat format, VkImageLayout oldLayout,
						   VkImageLayout newLayout) {
	VkCommandBufferAllocateInfo allocInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandPool = commandPool,
		.commandBufferCount = 1};
	VkCommandBuffer commandBuffer;
	vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);
	VkCommandBufferBeginInfo beginInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
	vkBeginCommandBuffer(commandBuffer, &beginInfo);
	VkImageMemoryBarrier barrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.oldLayout = oldLayout,
		.newLayout = newLayout,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = image,
		.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
	VkPipelineStageFlags srcStage, dstStage;
	if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
		newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
	} else {
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	}
	vkCmdPipelineBarrier(commandBuffer, srcStage, dstStage, 0, 0, NULL, 0, NULL,
						 1, &barrier);
	vkEndCommandBuffer(commandBuffer);
	VkSubmitInfo submitInfo = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
							   .commandBufferCount = 1,
							   .pCommandBuffers = &commandBuffer};
	vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
	vkQueueWaitIdle(graphicsQueue);
	vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}

VkResult create_shader_module(VkDevice device, const char *path,
							  VkShaderModule *shaderModule) {
	FILE *file = fopen(path, "rb");
	if (!file)
		return VK_ERROR_INITIALIZATION_FAILED;
	fseek(file, 0, SEEK_END);
	long size = ftell(file);
	fseek(file, 0, SEEK_SET);
	uint32_t *code = malloc(size);
	fread(code, 1, size, file);
	fclose(file);
	VkShaderModuleCreateInfo createInfo = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = size,
		.pCode = code};
	VkResult result =
		vkCreateShaderModule(device, &createInfo, NULL, shaderModule);
	free(code);
	return result;
}
