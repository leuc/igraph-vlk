#ifndef VULKAN_UTILS_H
#define VULKAN_UTILS_H

#include <stdint.h>
#include <vulkan/vulkan.h>

uint32_t findMemoryType (VkPhysicalDevice physicalDevice, uint32_t typeFilter,
                         VkMemoryPropertyFlags properties);
void createBuffer (VkDevice device, VkPhysicalDevice physicalDevice,
                   VkDeviceSize size, VkBufferUsageFlags usage,
                   VkMemoryPropertyFlags properties, VkBuffer *buffer,
                   VkDeviceMemory *bufferMemory);
void updateBuffer (VkDevice device, VkDeviceMemory memory, VkDeviceSize size,
                   const void *data);
void createImage (VkDevice device, VkPhysicalDevice physicalDevice,
                  uint32_t width, uint32_t height, VkFormat format,
                  VkImageTiling tiling, VkImageUsageFlags usage,
                  VkMemoryPropertyFlags properties, VkImage *image,
                  VkDeviceMemory *imageMemory);
void transitionImageLayout (VkDevice device, VkCommandPool commandPool,
                            VkQueue graphicsQueue, VkImage image,
                            VkFormat format, VkImageLayout oldLayout,
                            VkImageLayout newLayout);
VkResult create_shader_module (VkDevice device, const char *path,
                               VkShaderModule *shaderModule);

#endif