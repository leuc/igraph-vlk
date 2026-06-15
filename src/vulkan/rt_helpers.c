// =============================================================================
// RT Helpers — Shared Vulkan Ray-Tracing Utilities
// =============================================================================
//
// Provides RT extension function pointer loading, device address queries,
// buffer creation with device-address support, and staging upload.
// Used by both rt_base and rt_barnes_hut modules.
//
// =============================================================================

#include "vulkan/rt_helpers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vulkan/utils.h"

// ============================================================================
// RT Function Pointers (loaded once via vkGetDeviceProcAddr)
// ============================================================================

static struct
{
	PFN_vkCreateAccelerationStructureKHR CreateAccelerationStructureKHR;
	PFN_vkDestroyAccelerationStructureKHR DestroyAccelerationStructureKHR;
	PFN_vkGetAccelerationStructureBuildSizesKHR GetAccelerationStructureBuildSizesKHR;
	PFN_vkCmdBuildAccelerationStructuresKHR CmdBuildAccelerationStructuresKHR;
	PFN_vkGetAccelerationStructureDeviceAddressKHR GetAccelerationStructureDeviceAddressKHR;
	PFN_vkGetBufferDeviceAddressKHR GetBufferDeviceAddressKHR;
} rt_funcs;

void rt_helpers_load_functions(VulkanCore *core)
{
	if (rt_funcs.CreateAccelerationStructureKHR)
		return;
	rt_funcs.CreateAccelerationStructureKHR = (PFN_vkCreateAccelerationStructureKHR)vkGetDeviceProcAddr(core->device, "vkCreateAccelerationStructureKHR");
	rt_funcs.DestroyAccelerationStructureKHR = (PFN_vkDestroyAccelerationStructureKHR)vkGetDeviceProcAddr(core->device, "vkDestroyAccelerationStructureKHR");
	rt_funcs.GetAccelerationStructureBuildSizesKHR = (PFN_vkGetAccelerationStructureBuildSizesKHR)vkGetDeviceProcAddr(core->device, "vkGetAccelerationStructureBuildSizesKHR");
	rt_funcs.CmdBuildAccelerationStructuresKHR = (PFN_vkCmdBuildAccelerationStructuresKHR)vkGetDeviceProcAddr(core->device, "vkCmdBuildAccelerationStructuresKHR");
	rt_funcs.GetAccelerationStructureDeviceAddressKHR = (PFN_vkGetAccelerationStructureDeviceAddressKHR)vkGetDeviceProcAddr(core->device, "vkGetAccelerationStructureDeviceAddressKHR");
	// vkGetBufferDeviceAddress: KHR promoted to core in 1.2; loaders only register core name
	rt_funcs.GetBufferDeviceAddressKHR = (PFN_vkGetBufferDeviceAddressKHR)vkGetDeviceProcAddr(core->device, "vkGetBufferDeviceAddress");
	if (!rt_funcs.GetBufferDeviceAddressKHR)
		rt_funcs.GetBufferDeviceAddressKHR = (PFN_vkGetBufferDeviceAddressKHR)vkGetDeviceProcAddr(core->device, "vkGetBufferDeviceAddressKHR");
}

// ============================================================================
// Device Address Queries
// ============================================================================

uint64_t rt_helpers_get_buffer_device_address(VkDevice device, VkBuffer buf)
{
	VkBufferDeviceAddressInfo addrInfo = {.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = buf};
	return rt_funcs.GetBufferDeviceAddressKHR(device, &addrInfo);
}

uint64_t rt_helpers_get_as_device_address(VkDevice device, VkAccelerationStructureKHR as)
{
	VkAccelerationStructureDeviceAddressInfoKHR addrInfo = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR, .accelerationStructure = as};
	return rt_funcs.GetAccelerationStructureDeviceAddressKHR(device, &addrInfo);
}

// ============================================================================
// Buffer Creation Helpers
// ============================================================================

void rt_helpers_create_buffer(VulkanCore *core, VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer *buf, VkDeviceMemory *mem)
{
	VK_CREATE_DEVICE_ADDRESS_BUFFER(core->device, core->physicalDevice, size, usage, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, buf, mem);
}

void rt_helpers_create_device_buffer(VulkanCore *core, VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer *buf, VkDeviceMemory *mem)
{
	VK_CREATE_DEVICE_ADDRESS_BUFFER(core->device, core->physicalDevice, size, usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, buf, mem);
}

// ============================================================================
// Staging Upload
// ============================================================================

void rt_helpers_staging_upload(VulkanCore *core, VkBuffer dst, const void *data, VkDeviceSize size)
{
	VkBuffer staging;
	VkDeviceMemory stagingMem;
	rt_helpers_create_buffer(core, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &staging, &stagingMem);

	void *mapped;
	VK_CHECK(vkMapMemory(core->device, stagingMem, 0, size, 0, &mapped), "Failed to map staging upload");
	memcpy(mapped, data, size);
	vkUnmapMemory(core->device, stagingMem);

	VkCommandPool cmdPool;
	VkCommandBuffer cmd;
	VK_ONE_SHOT_BEGIN(core->device, (uint32_t)core->graphicsQueueFamily, cmdPool, cmd);

	VkBufferCopy copyRegion = {.size = size};
	vkCmdCopyBuffer(cmd, staging, dst, 1, &copyRegion);

	pthread_mutex_lock(&core->graphicsQueueMutex);
	VK_ONE_SHOT_END(core->device, core->graphicsQueue, cmdPool, cmd);
	pthread_mutex_unlock(&core->graphicsQueueMutex);

	VK_DESTROY_BUFFER(core->device, staging, stagingMem);
}

// ============================================================================
// Acceleration Structure Destruction
// ============================================================================

void rt_helpers_destroy_as(VkDevice device, VkAccelerationStructureKHR as)
{
	if (as != VK_NULL_HANDLE)
		rt_funcs.DestroyAccelerationStructureKHR(device, as, NULL);
}

// ============================================================================
// Internal — accessors for RTBase / BarnesHutRT to use loaded function pointers
// ============================================================================

PFN_vkCreateAccelerationStructureKHR rt_helpers_get_CreateAccelerationStructureKHR(void)
{
	return rt_funcs.CreateAccelerationStructureKHR;
}
PFN_vkDestroyAccelerationStructureKHR rt_helpers_get_DestroyAccelerationStructureKHR(void)
{
	return rt_funcs.DestroyAccelerationStructureKHR;
}
PFN_vkGetAccelerationStructureBuildSizesKHR rt_helpers_get_GetAccelerationStructureBuildSizesKHR(void)
{
	return rt_funcs.GetAccelerationStructureBuildSizesKHR;
}
PFN_vkCmdBuildAccelerationStructuresKHR rt_helpers_get_CmdBuildAccelerationStructuresKHR(void)
{
	return rt_funcs.CmdBuildAccelerationStructuresKHR;
}
PFN_vkGetAccelerationStructureDeviceAddressKHR rt_helpers_get_GetAccelerationStructureDeviceAddressKHR(void)
{
	return rt_funcs.GetAccelerationStructureDeviceAddressKHR;
}
