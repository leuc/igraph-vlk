#ifndef VK_BUFFERS_H
#define VK_BUFFERS_H

#include <stdint.h>
#include <vulkan/vulkan.h>

uint32_t find_memory_type(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties);
void create_buffer(VkDevice device, VkPhysicalDevice physicalDevice, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer *buffer, VkDeviceMemory *bufferMemory);
void create_staging_buffer(VkDevice device, VkPhysicalDevice physicalDevice, VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer *stagingBuf, VkDeviceMemory *stagingMem, VkBuffer *deviceBuf, VkDeviceMemory *deviceMem);
void create_mapped_buffer(VkDevice device, VkPhysicalDevice physicalDevice, VkDeviceSize size, VkBuffer *buffer, VkDeviceMemory *memory);
void update_buffer(VkDevice device, VkDeviceMemory memory, VkDeviceSize size, const void *data);
void update_buffer_mapped(VkDevice device, VkDeviceMemory memory, VkDeviceSize size, const void *data, const VkPhysicalDeviceProperties *deviceProps);
void update_buffer_staged(VkDevice device, VkCommandPool commandPool, VkQueue queue, VkDeviceSize size, const void *data, VkBuffer stagingBuf, VkDeviceMemory stagingMem, VkBuffer deviceBuf, const VkPhysicalDeviceProperties *deviceProps);

#endif
