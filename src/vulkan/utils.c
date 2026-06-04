#include "vulkan/utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

#include "vulkan/app_path.h"

uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
	VkPhysicalDeviceMemoryProperties memProperties;
	vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
	for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
		if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
			return i;
	}
	return 0;
}

void createBuffer(VkDevice device, VkPhysicalDevice physicalDevice, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer *buffer, VkDeviceMemory *bufferMemory)
{
	VkBufferCreateInfo bufferInfo = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size, .usage = usage, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
	vkCreateBuffer(device, &bufferInfo, NULL, buffer);
	VkMemoryRequirements memReqs;
	vkGetBufferMemoryRequirements(device, *buffer, &memReqs);
	VkPhysicalDeviceProperties props;
	vkGetPhysicalDeviceProperties(physicalDevice, &props);
	VkDeviceSize atomSize = props.limits.nonCoherentAtomSize;
	VkDeviceSize allocSize = (memReqs.size + atomSize - 1) & ~(atomSize - 1);
	VkMemoryAllocateInfo allocInfo = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = allocSize, .memoryTypeIndex = findMemoryType(physicalDevice, memReqs.memoryTypeBits, properties)};
	vkAllocateMemory(device, &allocInfo, NULL, bufferMemory);
	vkBindBufferMemory(device, *buffer, *bufferMemory, 0);
}

void updateBuffer(VkDevice device, VkDeviceMemory memory, VkDeviceSize size, const void *data)
{
	void *mapped;
	vkMapMemory(device, memory, 0, size, 0, &mapped);
	memcpy(mapped, data, size);
	vkUnmapMemory(device, memory);
}

void updateBufferMapped(VkDevice device, VkDeviceMemory memory, VkDeviceSize size, const void *data, const VkPhysicalDeviceProperties *deviceProps)
{
	if (size == 0)
		return;
	VkDeviceSize atomSize = deviceProps->limits.nonCoherentAtomSize;
	VkDeviceSize alignedSize = (size + atomSize - 1) & ~(atomSize - 1);
	void *mapped;
	vkMapMemory(device, memory, 0, alignedSize, 0, &mapped);
	memcpy(mapped, data, size);
	VkMappedMemoryRange range = {.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, .memory = memory, .size = alignedSize};
	vkFlushMappedMemoryRanges(device, 1, &range);
	vkUnmapMemory(device, memory);
}

void createMappedBuffer(VkDevice device, VkPhysicalDevice physicalDevice, VkDeviceSize size, VkBuffer *buffer, VkDeviceMemory *memory)
{
	VkBufferCreateInfo info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size, .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
	vkCreateBuffer(device, &info, NULL, buffer);

	VkMemoryRequirements req;
	vkGetBufferMemoryRequirements(device, *buffer, &req);

	VkPhysicalDeviceProperties props;
	vkGetPhysicalDeviceProperties(physicalDevice, &props);
	VkDeviceSize atomSize = props.limits.nonCoherentAtomSize;
	VkDeviceSize allocSize = (req.size + atomSize - 1) & ~(atomSize - 1);
	VkMemoryAllocateInfo alloc = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = allocSize, .memoryTypeIndex = findMemoryType(physicalDevice, req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
	vkAllocateMemory(device, &alloc, NULL, memory);
	vkBindBufferMemory(device, *buffer, *memory, 0);
}

void createStagingBuffer(VkDevice device, VkPhysicalDevice physicalDevice, VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer *stagingBuf, VkDeviceMemory *stagingMem, VkBuffer *deviceBuf, VkDeviceMemory *deviceMem)
{
	VkBufferCreateInfo bufferInfo = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size, .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
	vkCreateBuffer(device, &bufferInfo, NULL, stagingBuf);
	VkMemoryRequirements memReqs;
	vkGetBufferMemoryRequirements(device, *stagingBuf, &memReqs);
	VkPhysicalDeviceProperties props;
	vkGetPhysicalDeviceProperties(physicalDevice, &props);
	VkDeviceSize atomSize = props.limits.nonCoherentAtomSize;
	VkDeviceSize allocSize = (memReqs.size + atomSize - 1) & ~(atomSize - 1);
	VkMemoryAllocateInfo allocInfo = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = allocSize, .memoryTypeIndex = findMemoryType(physicalDevice, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
	vkAllocateMemory(device, &allocInfo, NULL, stagingMem);
	vkBindBufferMemory(device, *stagingBuf, *stagingMem, 0);

	bufferInfo.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	vkCreateBuffer(device, &bufferInfo, NULL, deviceBuf);
	vkGetBufferMemoryRequirements(device, *deviceBuf, &memReqs);
	allocInfo.allocationSize = memReqs.size;
	allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	vkAllocateMemory(device, &allocInfo, NULL, deviceMem);
	vkBindBufferMemory(device, *deviceBuf, *deviceMem, 0);
}

void updateBufferStaged(VkDevice device, VkCommandPool commandPool, VkQueue queue, VkDeviceSize size, const void *data, VkBuffer stagingBuf, VkDeviceMemory stagingMem, VkBuffer deviceBuf, const VkPhysicalDeviceProperties *deviceProps)
{
	VkDeviceSize atomSize = deviceProps->limits.nonCoherentAtomSize;
	VkDeviceSize alignedSize = (size + atomSize - 1) & ~(atomSize - 1);
	void *mapped;
	vkMapMemory(device, stagingMem, 0, alignedSize, 0, &mapped);
	memcpy(mapped, data, size);
	VkMappedMemoryRange range = {.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, .memory = stagingMem, .size = alignedSize};
	vkFlushMappedMemoryRanges(device, 1, &range);
	vkUnmapMemory(device, stagingMem);

	VkCommandBuffer commandBuffer = begin_single_time_commands(device, commandPool);
	VkBufferCopy copyRegion = {.size = size};
	vkCmdCopyBuffer(commandBuffer, stagingBuf, deviceBuf, 1, &copyRegion);
	end_single_time_commands(device, commandPool, queue, commandBuffer);
}

void createImage(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage *image, VkDeviceMemory *imageMemory)
{
	VkImageCreateInfo imageInfo = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D, .extent = {width, height, 1}, .mipLevels = 1, .arrayLayers = 1, .format = format, .tiling = tiling, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .usage = usage, .samples = VK_SAMPLE_COUNT_1_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
	vkCreateImage(device, &imageInfo, NULL, image);
	VkMemoryRequirements memReqs;
	vkGetImageMemoryRequirements(device, *image, &memReqs);
	VkMemoryAllocateInfo allocInfo = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = memReqs.size, .memoryTypeIndex = findMemoryType(physicalDevice, memReqs.memoryTypeBits, properties)};
	vkAllocateMemory(device, &allocInfo, NULL, imageMemory);
	vkBindImageMemory(device, *image, *imageMemory, 0);
}

void transitionImageLayout(VkDevice device, VkCommandPool commandPool, VkQueue graphicsQueue, VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout)
{
	VkCommandBuffer commandBuffer = begin_single_time_commands(device, commandPool);
	VkImageMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, .oldLayout = oldLayout, .newLayout = newLayout, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = image, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
	VkPipelineStageFlags srcStage, dstStage;
	if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
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
	vkCmdPipelineBarrier(commandBuffer, srcStage, dstStage, 0, 0, NULL, 0, NULL, 1, &barrier);
	end_single_time_commands(device, commandPool, graphicsQueue, commandBuffer);
}

// Point 9: One-time submit command buffer utilities
VkCommandBuffer begin_single_time_commands(VkDevice device, VkCommandPool commandPool)
{
	VkCommandBufferAllocateInfo allocInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandPool = commandPool, .commandBufferCount = 1};
	VkCommandBuffer commandBuffer;
	VK_CHECK(vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer), "Failed to allocate one-time command buffer");
	VkCommandBufferBeginInfo beginInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
	vkBeginCommandBuffer(commandBuffer, &beginInfo);
	return commandBuffer;
}

void end_single_time_commands(VkDevice device, VkCommandPool commandPool, VkQueue queue, VkCommandBuffer commandBuffer)
{
	vkEndCommandBuffer(commandBuffer);
	VkSubmitInfo submitInfo = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &commandBuffer};
	VK_CHECK(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE), "Failed to submit one-time command buffer");
	vkQueueWaitIdle(queue);
	vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}

VkResult create_shader_module(VkDevice device, const char *rel, VkShaderModule *shaderModule)
{
	const char *path = app_path_resolve(rel);
	FILE *file = fopen(path, "rb");
	if (!file) {
		fprintf(stderr, "Failed to open shader: %s\n", path);
		return VK_ERROR_INITIALIZATION_FAILED;
	}
	fseek(file, 0, SEEK_END);
	long size = ftell(file);
	fseek(file, 0, SEEK_SET);
	uint32_t *code = malloc(size);
	fread(code, 1, size, file);
	fclose(file);
	VkShaderModuleCreateInfo createInfo = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = size, .pCode = code};
	VkResult result = vkCreateShaderModule(device, &createInfo, NULL, shaderModule);
	free(code);
	return result;
}

void exit_with_error(const char *msg)
{
	fprintf(stderr, "Fatal Error: %s\n", msg);
	exit(1);
}
