/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "vulkan/images.h"

#include "vulkan/buffers.h"
#include "vulkan/commands.h"
#include "vulkan/utils.h"

void create_image(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage *image, VkDeviceMemory *imageMemory)
{
	VkImageCreateInfo imageInfo = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D, .extent = {width, height, 1}, .mipLevels = 1, .arrayLayers = 1, .format = format, .tiling = tiling, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .usage = usage, .samples = VK_SAMPLE_COUNT_1_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
	VK_CHECK(vkCreateImage(device, &imageInfo, NULL, image), "Failed to create image");
	VkMemoryRequirements memReqs;
	vkGetImageMemoryRequirements(device, *image, &memReqs);
	VkMemoryAllocateInfo allocInfo = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = memReqs.size, .memoryTypeIndex = find_memory_type(physicalDevice, memReqs.memoryTypeBits, properties)};
	VK_CHECK(vkAllocateMemory(device, &allocInfo, NULL, imageMemory), "Failed to allocate image memory");
	VK_CHECK(vkBindImageMemory(device, *image, *imageMemory, 0), "Failed to bind image memory");
}

void transition_image_layout(VkDevice device, VkCommandPool commandPool, VkQueue graphicsQueue, VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout)
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
