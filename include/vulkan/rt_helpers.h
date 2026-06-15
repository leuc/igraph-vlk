#ifndef RT_HELPERS_H
#define RT_HELPERS_H

#include "vulkan/vulkan_types.h"
#include <stdint.h>
#include <vulkan/vulkan.h>

// RT extension function pointers (loaded once, shared by all RT modules)
void rt_helpers_load_functions(VulkanCore *core);

// Device address queries
uint64_t rt_helpers_get_buffer_device_address(VkDevice device, VkBuffer buf);
uint64_t rt_helpers_get_as_device_address(VkDevice device, VkAccelerationStructureKHR as);

// Buffer creation with SHADER_DEVICE_ADDRESS_BIT + DEVICE_ADDRESS allocation
// create_buffer: HOST_VISIBLE | HOST_COHERENT (mappable)
// create_device_buffer: DEVICE_LOCAL (GPU-only)
void rt_helpers_create_buffer(VulkanCore *core, VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer *buf, VkDeviceMemory *mem);
void rt_helpers_create_device_buffer(VulkanCore *core, VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer *buf, VkDeviceMemory *mem);

// One-shot staging upload: creates temp staging buffer + command pool, copies, waits via fence, cleans up
void rt_helpers_staging_upload(VulkanCore *core, VkBuffer dst, const void *data, VkDeviceSize size);

// Destroy a VkAccelerationStructureKHR via loaded function pointer
void rt_helpers_destroy_as(VkDevice device, VkAccelerationStructureKHR as);

// Function pointer accessors (for BLAS/TLAS build operations)
PFN_vkCreateAccelerationStructureKHR rt_helpers_get_CreateAccelerationStructureKHR(void);
PFN_vkDestroyAccelerationStructureKHR rt_helpers_get_DestroyAccelerationStructureKHR(void);
PFN_vkGetAccelerationStructureBuildSizesKHR rt_helpers_get_GetAccelerationStructureBuildSizesKHR(void);
PFN_vkCmdBuildAccelerationStructuresKHR rt_helpers_get_CmdBuildAccelerationStructuresKHR(void);
PFN_vkGetAccelerationStructureDeviceAddressKHR rt_helpers_get_GetAccelerationStructureDeviceAddressKHR(void);

#endif // RT_HELPERS_H
