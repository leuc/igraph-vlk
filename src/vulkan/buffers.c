/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "vulkan/buffers.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vulkan/commands.h"
#include "vulkan/utils.h"

uint32_t find_memory_type(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
	VkPhysicalDeviceMemoryProperties memProperties;
	vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
	for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
		if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
			return i;
	}
	return 0;
}

static bool try_find_memory_type(VkPhysicalDevice physical_device, uint32_t type_filter, VkMemoryPropertyFlags properties, uint32_t *memory_type)
{
	VkPhysicalDeviceMemoryProperties memory_properties;
	vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);
	for (uint32_t i = 0; i < memory_properties.memoryTypeCount; i++) {
		if ((type_filter & (1u << i)) && (memory_properties.memoryTypes[i].propertyFlags & properties) == properties) {
			*memory_type = i;
			return true;
		}
	}
	return false;
}

VkResult try_create_buffer(VkDevice device, VkPhysicalDevice physical_device, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer *buffer, VkDeviceMemory *buffer_memory)
{
	*buffer = VK_NULL_HANDLE;
	*buffer_memory = VK_NULL_HANDLE;
	VkBufferCreateInfo bufferInfo = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size, .usage = usage, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
	VkResult result = vkCreateBuffer(device, &bufferInfo, NULL, buffer);
	if (result != VK_SUCCESS)
		return result;
	VkMemoryRequirements memReqs;
	vkGetBufferMemoryRequirements(device, *buffer, &memReqs);
	VkPhysicalDeviceProperties props;
	vkGetPhysicalDeviceProperties(physical_device, &props);
	VkDeviceSize atomSize = props.limits.nonCoherentAtomSize;
	VkDeviceSize allocSize = VK_ALIGN_UP(memReqs.size, atomSize);
	uint32_t memory_type;
	if (!try_find_memory_type(physical_device, memReqs.memoryTypeBits, properties, &memory_type)) {
		vkDestroyBuffer(device, *buffer, NULL);
		*buffer = VK_NULL_HANDLE;
		return VK_ERROR_FEATURE_NOT_PRESENT;
	}
	VkMemoryAllocateInfo allocInfo = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = allocSize, .memoryTypeIndex = memory_type};
	result = vkAllocateMemory(device, &allocInfo, NULL, buffer_memory);
	if (result != VK_SUCCESS) {
		vkDestroyBuffer(device, *buffer, NULL);
		*buffer = VK_NULL_HANDLE;
		return result;
	}
	result = vkBindBufferMemory(device, *buffer, *buffer_memory, 0);
	if (result != VK_SUCCESS) {
		vkFreeMemory(device, *buffer_memory, NULL);
		vkDestroyBuffer(device, *buffer, NULL);
		*buffer_memory = VK_NULL_HANDLE;
		*buffer = VK_NULL_HANDLE;
	}
	return result;
}

void create_buffer(VkDevice device, VkPhysicalDevice physicalDevice, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer *buffer, VkDeviceMemory *bufferMemory)
{
	VK_CHECK(try_create_buffer(device, physicalDevice, size, usage, properties, buffer, bufferMemory), "Failed to create buffer");
}

void update_buffer(VkDevice device, VkDeviceMemory memory, VkDeviceSize size, const void *data)
{
	void *mapped;
	VK_CHECK(vkMapMemory(device, memory, 0, size, 0, &mapped), "Failed to map buffer memory for update");
	memcpy(mapped, data, size);
	vkUnmapMemory(device, memory);
}

void update_buffer_mapped(VkDevice device, VkDeviceMemory memory, VkDeviceSize size, const void *data, const VkPhysicalDeviceProperties *deviceProps)
{
	if (size == 0)
		return;
	VkDeviceSize atomSize = deviceProps->limits.nonCoherentAtomSize;
	VkDeviceSize alignedSize = VK_ALIGN_UP(size, atomSize);
	void *mapped;
	VK_CHECK(vkMapMemory(device, memory, 0, alignedSize, 0, &mapped), "Failed to map buffer memory for updateMapped");
	memcpy(mapped, data, size);
	VkMappedMemoryRange range = {.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, .memory = memory, .size = alignedSize};
	VK_CHECK(vkFlushMappedMemoryRanges(device, 1, &range), "Failed to flush mapped memory ranges");
	vkUnmapMemory(device, memory);
}

void create_mapped_buffer(VkDevice device, VkPhysicalDevice physicalDevice, VkDeviceSize size, VkBuffer *buffer, VkDeviceMemory *memory)
{
	VkBufferCreateInfo info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size, .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
	VK_CHECK(vkCreateBuffer(device, &info, NULL, buffer), "Failed to create mapped buffer");

	VkMemoryRequirements req;
	vkGetBufferMemoryRequirements(device, *buffer, &req);

	VkPhysicalDeviceProperties props;
	vkGetPhysicalDeviceProperties(physicalDevice, &props);
	VkDeviceSize atomSize = props.limits.nonCoherentAtomSize;
	VkDeviceSize allocSize = VK_ALIGN_UP(req.size, atomSize);
	VkMemoryAllocateInfo alloc = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = allocSize, .memoryTypeIndex = find_memory_type(physicalDevice, req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
	VK_CHECK(vkAllocateMemory(device, &alloc, NULL, memory), "Failed to allocate mapped buffer memory");
	VK_CHECK(vkBindBufferMemory(device, *buffer, *memory, 0), "Failed to bind mapped buffer memory");
}

void create_staging_buffer(VkDevice device, VkPhysicalDevice physicalDevice, VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer *stagingBuf, VkDeviceMemory *stagingMem, VkBuffer *deviceBuf, VkDeviceMemory *deviceMem)
{
	VkBufferCreateInfo bufferInfo = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size, .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
	VK_CHECK(vkCreateBuffer(device, &bufferInfo, NULL, stagingBuf), "Failed to create staging buffer");
	VkMemoryRequirements memReqs;
	vkGetBufferMemoryRequirements(device, *stagingBuf, &memReqs);
	VkPhysicalDeviceProperties props;
	vkGetPhysicalDeviceProperties(physicalDevice, &props);
	VkDeviceSize atomSize = props.limits.nonCoherentAtomSize;
	VkDeviceSize allocSize = VK_ALIGN_UP(memReqs.size, atomSize);
	VkMemoryAllocateInfo allocInfo = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = allocSize, .memoryTypeIndex = find_memory_type(physicalDevice, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
	VK_CHECK(vkAllocateMemory(device, &allocInfo, NULL, stagingMem), "Failed to allocate staging buffer memory");
	VK_CHECK(vkBindBufferMemory(device, *stagingBuf, *stagingMem, 0), "Failed to bind staging buffer memory");

	bufferInfo.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	VK_CHECK(vkCreateBuffer(device, &bufferInfo, NULL, deviceBuf), "Failed to create device-local buffer");
	vkGetBufferMemoryRequirements(device, *deviceBuf, &memReqs);
	allocInfo.allocationSize = memReqs.size;
	allocInfo.memoryTypeIndex = find_memory_type(physicalDevice, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	VK_CHECK(vkAllocateMemory(device, &allocInfo, NULL, deviceMem), "Failed to allocate device-local buffer memory");
	VK_CHECK(vkBindBufferMemory(device, *deviceBuf, *deviceMem, 0), "Failed to bind device-local buffer memory");
}

void update_buffer_staged(VkDevice device, VkCommandPool commandPool, VkQueue queue, VkDeviceSize size, const void *data, VkBuffer stagingBuf, VkDeviceMemory stagingMem, VkBuffer deviceBuf, const VkPhysicalDeviceProperties *deviceProps)
{
	if (size == 0)
		return;
	VkDeviceSize atomSize = deviceProps->limits.nonCoherentAtomSize;
	VkDeviceSize alignedSize = VK_ALIGN_UP(size, atomSize);
	void *mapped;
	VK_CHECK(vkMapMemory(device, stagingMem, 0, alignedSize, 0, &mapped), "Failed to map staging buffer memory");
	memcpy(mapped, data, size);
	VkMappedMemoryRange range = {.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, .memory = stagingMem, .size = alignedSize};
	VK_CHECK(vkFlushMappedMemoryRanges(device, 1, &range), "Failed to flush staging mapped memory ranges");
	vkUnmapMemory(device, stagingMem);

	VkCommandBuffer commandBuffer = begin_single_time_commands(device, commandPool);
	VkBufferCopy copyRegion = {.size = size};
	vkCmdCopyBuffer(commandBuffer, stagingBuf, deviceBuf, 1, &copyRegion);
	end_single_time_commands(device, commandPool, queue, commandBuffer);
}
