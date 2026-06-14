// =============================================================================
// RTBase — Reusable Vulkan Ray-Tracing Infrastructure
// =============================================================================
//
// Provides BLAS/TLAS construction, instance buffer management, command buffer
// lifecycle, and buffer creation helpers used by any RT-accelerated layout
// algorithm (Yifan Hu, ForceAtlas2, etc.).
//
// The architecture uses one BLAS (single AABB) instanced N times via TLAS:
//
//   TLAS (Top-Level Acceleration Structure)
//   +-----------+-----------+-----+-----------+
//   | Instance0 | Instance1 | ... | InstanceN |
//   +-----------+-----------+-----+-----------+
//        |           |               |
//        v           v               v
//   +------+     +------+         +------+
//   | BLAS |     | BLAS |   ...   | BLAS |    Same BLAS, different 3x4 transforms
//   +------+     +------+         +------+
//      |            |                |
//      v            v                v
//   [-R,R]³      [-R,R]³          [-R,R]³   Single AABB primitive per instance
//
// =============================================================================

#include "vulkan/rt_base.h"

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vulkan/buffers.h"
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

static void rt_base_load_rt_functions(VulkanCore *core)
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
// Opaque Struct Definition
// ============================================================================

struct RTBase
{
	VulkanCore *core;
	bool supported;
	bool fp64_supported;
	bool session_active;
	uint32_t vcount;
	uint32_t ecount;
	float search_radius;

	// Buffers
	VkBuffer blas_buf;
	VkDeviceMemory blas_mem;
	VkBuffer tlas_buf;
	VkDeviceMemory tlas_mem;
	VkBuffer as_scratch_buf;
	VkDeviceMemory as_scratch_mem;
	VkBuffer instance_buf;
	VkDeviceMemory instance_mem;

	// Acceleration structures
	VkAccelerationStructureKHR blas;
	VkAccelerationStructureKHR tlas;

	// Command infrastructure
	VkCommandPool cmd_pool;
	VkCommandBuffer cmd_buf;

	// Staging for position readback
	VkBuffer node_staging_buf;
	VkDeviceMemory node_staging_mem;
};

// ============================================================================
// Internal Helpers
// ============================================================================

static uint64_t rt_base_get_buffer_address(RTBase *base, VkBuffer buf)
{
	VkBufferDeviceAddressInfo addrInfo = {.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = buf};
	return rt_funcs.GetBufferDeviceAddressKHR(base->core->device, &addrInfo);
}

static uint64_t rt_base_get_as_address(RTBase *base, VkAccelerationStructureKHR as)
{
	VkAccelerationStructureDeviceAddressInfoKHR addrInfo = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR, .accelerationStructure = as};
	return rt_funcs.GetAccelerationStructureDeviceAddressKHR(base->core->device, &addrInfo);
}

// ============================================================================
// Support Check
// ============================================================================

bool rt_base_check_support(VkPhysicalDevice device)
{
	uint32_t devExtCount = 0;
	vkEnumerateDeviceExtensionProperties(device, NULL, &devExtCount, NULL);
	VkExtensionProperties *devExts = malloc(sizeof(VkExtensionProperties) * devExtCount);
	vkEnumerateDeviceExtensionProperties(device, NULL, &devExtCount, devExts);

	printf("[RTBase] Checking %u device extensions for RT support...\n", devExtCount);

	const char *required[] = {
		VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, VK_KHR_RAY_QUERY_EXTENSION_NAME, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME, VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME, VK_KHR_SPIRV_1_4_EXTENSION_NAME, VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME,
	};

	bool supported = true;
	for (int i = 0; i < 6; i++) {
		bool found = false;
		for (uint32_t j = 0; j < devExtCount; j++) {
			if (strcmp(required[i], devExts[j].extensionName) == 0) {
				found = true;
				break;
			}
		}
		if (!found) {
			fprintf(stderr, "[RTBase] MISSING extension: %s\n", required[i]);
			supported = false;
		} else {
			printf("[RTBase]   found: %s\n", required[i]);
		}
	}
	free(devExts);
	return supported;
}

// ============================================================================
// Buffer Creation Helpers
// ============================================================================

void rt_base_create_buffer(RTBase *base, VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer *buf, VkDeviceMemory *mem)
{
	VkBufferCreateInfo bufInfo = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size, .usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
	VK_CHECK(vkCreateBuffer(base->core->device, &bufInfo, NULL, buf), "Failed to create RTBase buffer");

	VkMemoryRequirements memReqs;
	vkGetBufferMemoryRequirements(base->core->device, *buf, &memReqs);

	VkPhysicalDeviceMemoryProperties memProps;
	vkGetPhysicalDeviceMemoryProperties(base->core->physicalDevice, &memProps);

	uint32_t memTypeIndex = UINT32_MAX;
	for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
		if ((memReqs.memoryTypeBits & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) && (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
			memTypeIndex = i;
			break;
		}
	}
	if (memTypeIndex == UINT32_MAX) {
		for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
			if ((memReqs.memoryTypeBits & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
				memTypeIndex = i;
				break;
			}
		}
	}
	if (memTypeIndex == UINT32_MAX)
		exit_with_error("RTBase: failed to find suitable memory type");

	VkMemoryAllocateFlagsInfo allocFlags = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO, .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT};
	VkMemoryAllocateInfo allocInfo = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .pNext = &allocFlags, .allocationSize = memReqs.size, .memoryTypeIndex = memTypeIndex};
	VK_CHECK(vkAllocateMemory(base->core->device, &allocInfo, NULL, mem), "Failed to allocate RTBase buffer memory");
	VK_CHECK(vkBindBufferMemory(base->core->device, *buf, *mem, 0), "Failed to bind RTBase buffer memory");
}

void rt_base_create_device_buffer(RTBase *base, VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer *buf, VkDeviceMemory *mem)
{
	VkBufferCreateInfo bufInfo = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size, .usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
	VK_CHECK(vkCreateBuffer(base->core->device, &bufInfo, NULL, buf), "Failed to create RTBase device buffer");

	VkMemoryRequirements memReqs;
	vkGetBufferMemoryRequirements(base->core->device, *buf, &memReqs);

	VkPhysicalDeviceMemoryProperties memProps;
	vkGetPhysicalDeviceMemoryProperties(base->core->physicalDevice, &memProps);

	uint32_t memTypeIndex = UINT32_MAX;
	for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
		if ((memReqs.memoryTypeBits & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
			memTypeIndex = i;
			break;
		}
	}
	if (memTypeIndex == UINT32_MAX)
		exit_with_error("RTBase: failed to find device-local memory type");

	VkMemoryAllocateFlagsInfo allocFlags = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO, .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT};
	VkMemoryAllocateInfo allocInfo = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .pNext = &allocFlags, .allocationSize = memReqs.size, .memoryTypeIndex = memTypeIndex};
	VK_CHECK(vkAllocateMemory(base->core->device, &allocInfo, NULL, mem), "Failed to allocate RTBase device buffer memory");
	VK_CHECK(vkBindBufferMemory(base->core->device, *buf, *mem, 0), "Failed to bind RTBase device buffer memory");
}

// ============================================================================
// Staging Upload
// ============================================================================

void rt_base_staging_upload(RTBase *base, VkBuffer dst, const void *data, VkDeviceSize size, bool thread_safe)
{
	VkBuffer staging;
	VkDeviceMemory stagingMem;
	rt_base_create_buffer(base, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &staging, &stagingMem);
	void *mapped;
	VK_CHECK(vkMapMemory(base->core->device, stagingMem, 0, size, 0, &mapped), "Failed to map staging upload");
	memcpy(mapped, data, size);
	vkUnmapMemory(base->core->device, stagingMem);

	VkCommandPoolCreateInfo poolInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, .queueFamilyIndex = (uint32_t)base->core->graphicsQueueFamily};
	VkCommandPool cmdPool;
	VK_CHECK(vkCreateCommandPool(base->core->device, &poolInfo, NULL, &cmdPool), "Failed to create staging upload pool");
	VkCommandBufferAllocateInfo cmdInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = cmdPool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
	VkCommandBuffer cmd;
	VK_CHECK(vkAllocateCommandBuffers(base->core->device, &cmdInfo, &cmd), "Failed to allocate staging upload cmd");
	VK_CHECK(vkBeginCommandBuffer(cmd, &VK_CMD_BEGIN_INFO_ONETIME), "Failed to begin staging upload cmd");
	VkBufferCopy copyRegion = {.size = size};
	vkCmdCopyBuffer(cmd, staging, dst, 1, &copyRegion);
	VK_CHECK(vkEndCommandBuffer(cmd), "Failed to end staging upload cmd");
	VkSubmitInfo submitInfo = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
	VkFence fence;
	VkFenceCreateInfo fenceInfo = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
	VK_CHECK(vkCreateFence(base->core->device, &fenceInfo, NULL, &fence), "Failed to create staging upload fence");
	if (thread_safe)
		pthread_mutex_lock(&base->core->graphicsQueueMutex);
	VK_CHECK(vkQueueSubmit(base->core->graphicsQueue, 1, &submitInfo, fence), "Failed to submit staging upload");
	if (thread_safe)
		pthread_mutex_unlock(&base->core->graphicsQueueMutex);
	VK_CHECK(vkWaitForFences(base->core->device, 1, &fence, VK_TRUE, UINT64_MAX), "Failed to wait for staging upload fence");
	vkDestroyFence(base->core->device, fence, NULL);
	vkDestroyCommandPool(base->core->device, cmdPool, NULL);
	VK_DESTROY_BUFFER(base->core->device, staging, stagingMem);
}

// ============================================================================
// Session Cleanup (internal — base-owned resources only)
// ============================================================================

static void rt_base_cleanup_session_buffers(RTBase *base)
{
	VK_DESTROY_BUFFER(base->core->device, base->as_scratch_buf, base->as_scratch_mem);
	VK_DESTROY_BUFFER(base->core->device, base->blas_buf, base->blas_mem);
	VK_DESTROY_BUFFER(base->core->device, base->tlas_buf, base->tlas_mem);
	VK_DESTROY_BUFFER(base->core->device, base->instance_buf, base->instance_mem);

	if (base->blas != VK_NULL_HANDLE) {
		rt_funcs.DestroyAccelerationStructureKHR(base->core->device, base->blas, NULL);
		base->blas = VK_NULL_HANDLE;
	}
	if (base->tlas != VK_NULL_HANDLE) {
		rt_funcs.DestroyAccelerationStructureKHR(base->core->device, base->tlas, NULL);
		base->tlas = VK_NULL_HANDLE;
	}
	if (base->cmd_pool != VK_NULL_HANDLE) {
		vkDestroyCommandPool(base->core->device, base->cmd_pool, NULL);
		base->cmd_pool = VK_NULL_HANDLE;
		base->cmd_buf = VK_NULL_HANDLE;
	}
	VK_DESTROY_BUFFER(base->core->device, base->node_staging_buf, base->node_staging_mem);
}

// ============================================================================
// Public API — One-time
// ============================================================================

RTBase *rt_base_create(VulkanCore *core)
{
	RTBase *base = calloc(1, sizeof(RTBase));
	if (!base)
		return NULL;
	base->core = core;
	base->supported = rt_base_check_support(core->physicalDevice);
	base->session_active = false;

	rt_base_load_rt_functions(core);

	return base;
}

void rt_base_destroy(RTBase *base)
{
	if (!base)
		return;
	rt_base_cleanup_session_buffers(base);
	free(base);
}

// ============================================================================
// Public API — Per-Session
// ============================================================================

bool rt_base_session_init(RTBase *base, igraph_t *graph, igraph_matrix_t *init_positions, float search_radius)
{
	if (!base->supported) {
		fprintf(stderr, "[RTBase] Cannot start session: ray tracing not supported\n");
		return false;
	}

	uint32_t vcount = (uint32_t)igraph_vcount(graph);
	uint32_t ecount = (uint32_t)igraph_ecount(graph);
	base->vcount = vcount;
	base->ecount = ecount;
	base->search_radius = search_radius;
	base->fp64_supported = base->core->fp64_atomics_supported;
	base->session_active = true;

	// ---- Create worker-thread command pool + buffer ----
	{
		VkCommandPoolCreateInfo poolInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = (uint32_t)base->core->graphicsQueueFamily};
		VK_CHECK(vkCreateCommandPool(base->core->device, &poolInfo, NULL, &base->cmd_pool), "Failed to create RTBase worker cmd pool");
		VkCommandBufferAllocateInfo cmdInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = base->cmd_pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
		VK_CHECK(vkAllocateCommandBuffers(base->core->device, &cmdInfo, &base->cmd_buf), "Failed to allocate RTBase worker cmd");
	}

	// ---- Staging buffer for periodic position readback ----
	VkDeviceSize nodeSize = sizeof(vec4) * vcount;
	rt_base_create_buffer(base, nodeSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &base->node_staging_buf, &base->node_staging_mem);

	// =====================================================================
	// BLAS + TLAS Build
	// =====================================================================

	VkDeviceSize blasScratchSize = 0;
	VkDeviceSize tlasScratchSize = 0;
	VkBuffer aabbBuf = VK_NULL_HANDLE;
	VkDeviceMemory aabbMem = VK_NULL_HANDLE;

	VkAccelerationStructureGeometryKHR blasGeometry = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR, .geometryType = VK_GEOMETRY_TYPE_AABBS_KHR, .flags = VK_GEOMETRY_OPAQUE_BIT_KHR};
	VkAccelerationStructureBuildGeometryInfoKHR blasBuildInfo = {0};

	VkAccelerationStructureGeometryKHR tlasGeometry = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR, .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR, .flags = VK_GEOMETRY_OPAQUE_BIT_KHR, .geometry.instances = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR}};

	VkAccelerationStructureBuildRangeInfoKHR blasRangeInfo = {.primitiveCount = 1};
	VkAccelerationStructureBuildRangeInfoKHR tlasRangeInfo = {.primitiveCount = vcount};

	// Step 1: Size the BLAS
	{
		VkAabbPositionsKHR aabb = {.minX = -search_radius, .minY = -search_radius, .minZ = -search_radius, .maxX = search_radius, .maxY = search_radius, .maxZ = search_radius};

		rt_base_create_buffer(base, sizeof(VkAabbPositionsKHR), VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, &aabbBuf, &aabbMem);
		void *mapped;
		vkMapMemory(base->core->device, aabbMem, 0, sizeof(VkAabbPositionsKHR), 0, &mapped);
		memcpy(mapped, &aabb, sizeof(VkAabbPositionsKHR));
		vkUnmapMemory(base->core->device, aabbMem);

		blasGeometry.geometry.aabbs.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR;
		blasGeometry.geometry.aabbs.data.deviceAddress = rt_base_get_buffer_address(base, aabbBuf);
		blasGeometry.geometry.aabbs.stride = sizeof(VkAabbPositionsKHR);

		blasBuildInfo = (VkAccelerationStructureBuildGeometryInfoKHR){.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR, .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR, .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR, .geometryCount = 1, .pGeometries = &blasGeometry};

		uint32_t maxPrims = 1;
		VkAccelerationStructureBuildSizesInfoKHR blasSizeInfo = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
		rt_funcs.GetAccelerationStructureBuildSizesKHR(base->core->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &blasBuildInfo, &maxPrims, &blasSizeInfo);

		rt_base_create_device_buffer(base, blasSizeInfo.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, &base->blas_buf, &base->blas_mem);
		blasScratchSize = blasSizeInfo.buildScratchSize;

		VkAccelerationStructureCreateInfoKHR blasCreateInfo = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR, .buffer = base->blas_buf, .offset = 0, .size = blasSizeInfo.accelerationStructureSize, .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR};
		VK_CHECK(rt_funcs.CreateAccelerationStructureKHR(base->core->device, &blasCreateInfo, NULL, &base->blas), "Failed to create BLAS");
	}

	// Step 2: Size the TLAS
	{
		tlasGeometry.geometry.instances.data.deviceAddress = 0;
		VkAccelerationStructureBuildGeometryInfoKHR tlasBuildInfo = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR, .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR, .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR, .geometryCount = 1, .pGeometries = &tlasGeometry};
		uint32_t maxInstances = vcount;
		VkAccelerationStructureBuildSizesInfoKHR tlasSizeInfo = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
		rt_funcs.GetAccelerationStructureBuildSizesKHR(base->core->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &tlasBuildInfo, &maxInstances, &tlasSizeInfo);

		tlasScratchSize = tlasSizeInfo.buildScratchSize;
	}

	// Shared scratch buffer
	VkDeviceSize scratchSize = blasScratchSize > tlasScratchSize ? blasScratchSize : tlasScratchSize;
	rt_base_create_device_buffer(base, scratchSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, &base->as_scratch_buf, &base->as_scratch_mem);

	// Step 3: Build the BLAS (one-time, synchronous)
	{
		blasBuildInfo.dstAccelerationStructure = base->blas;
		blasBuildInfo.scratchData.deviceAddress = rt_base_get_buffer_address(base, base->as_scratch_buf);
		const VkAccelerationStructureBuildRangeInfoKHR *pBlasRange = &blasRangeInfo;

		VkCommandPoolCreateInfo poolInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, .queueFamilyIndex = (uint32_t)base->core->graphicsQueueFamily};
		VkCommandPool blasPool;
		vkCreateCommandPool(base->core->device, &poolInfo, NULL, &blasPool);
		VkCommandBufferAllocateInfo cmdInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = blasPool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
		VkCommandBuffer blasCmd;
		vkAllocateCommandBuffers(base->core->device, &cmdInfo, &blasCmd);
		vkBeginCommandBuffer(blasCmd, &VK_CMD_BEGIN_INFO_ONETIME);
		rt_funcs.CmdBuildAccelerationStructuresKHR(blasCmd, 1, &blasBuildInfo, &pBlasRange);
		vkEndCommandBuffer(blasCmd);
		VkSubmitInfo submitInfo = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &blasCmd};
		VkFence fence;
		VkFenceCreateInfo fenceInfo = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
		VK_CHECK(vkCreateFence(base->core->device, &fenceInfo, NULL, &fence), "Failed to create BLAS fence");
		pthread_mutex_lock(&base->core->graphicsQueueMutex);
		vkQueueSubmit(base->core->graphicsQueue, 1, &submitInfo, fence);
		pthread_mutex_unlock(&base->core->graphicsQueueMutex);
		VK_CHECK(vkWaitForFences(base->core->device, 1, &fence, VK_TRUE, UINT64_MAX), "Failed to wait for BLAS fence");
		vkDestroyFence(base->core->device, fence, NULL);
		vkDestroyCommandPool(base->core->device, blasPool, NULL);

		VK_DESTROY_BUFFER(base->core->device, aabbBuf, aabbMem);
	}

	// Step 4: Create TLAS instances and build the TLAS
	{
		VkDeviceSize instSize = sizeof(VkAccelerationStructureInstanceKHR) * vcount;
		rt_base_create_device_buffer(base, instSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &base->instance_buf, &base->instance_mem);

		uint32_t ncols = (uint32_t)igraph_matrix_ncol(init_positions);

		VkAccelerationStructureInstanceKHR *instances = calloc(vcount, sizeof(VkAccelerationStructureInstanceKHR));
		uint64_t blasAddr = rt_base_get_as_address(base, base->blas);
		for (uint32_t i = 0; i < vcount; i++) {
			memset(&instances[i], 0, sizeof(VkAccelerationStructureInstanceKHR));
			instances[i].instanceCustomIndex = i;
			instances[i].mask = 0xFF;
			instances[i].instanceShaderBindingTableRecordOffset = 0;
			instances[i].flags = VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;
			instances[i].accelerationStructureReference = blasAddr;

			float *m = (float *)instances[i].transform.matrix[0];
			m[0] = 1.0f;
			m[1] = 0.0f;
			m[2] = 0.0f;
			m[3] = (float)MATRIX(*init_positions, i, 0);
			m[4] = 0.0f;
			m[5] = 1.0f;
			m[6] = 0.0f;
			m[7] = (float)MATRIX(*init_positions, i, 1);
			m[8] = 0.0f;
			m[9] = 0.0f;
			m[10] = 1.0f;
			m[11] = (ncols > 2) ? (float)MATRIX(*init_positions, i, 2) : 0.0f;
		}
		rt_base_staging_upload(base, base->instance_buf, instances, instSize, true);
		free(instances);

		uint64_t instAddr = rt_base_get_buffer_address(base, base->instance_buf);
		tlasGeometry.geometry.instances.data.deviceAddress = instAddr;

		VkAccelerationStructureBuildSizesInfoKHR tlasSizeInfo = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
		{
			VkAccelerationStructureBuildGeometryInfoKHR tmpInfo = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR, .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR, .geometryCount = 1, .pGeometries = &tlasGeometry};
			uint32_t maxInstances = vcount;
			rt_funcs.GetAccelerationStructureBuildSizesKHR(base->core->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &tmpInfo, &maxInstances, &tlasSizeInfo);
		}

		rt_base_create_device_buffer(base, tlasSizeInfo.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, &base->tlas_buf, &base->tlas_mem);
		VkAccelerationStructureCreateInfoKHR tlasCreate = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR, .buffer = base->tlas_buf, .size = tlasSizeInfo.accelerationStructureSize, .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR};
		VK_CHECK(rt_funcs.CreateAccelerationStructureKHR(base->core->device, &tlasCreate, NULL, &base->tlas), "Failed to create TLAS");

		VkAccelerationStructureBuildGeometryInfoKHR tlasBuildInfo = {
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
			.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
			.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR,
			.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
			.dstAccelerationStructure = base->tlas,
			.geometryCount = 1,
			.pGeometries = &tlasGeometry,
			.scratchData.deviceAddress = rt_base_get_buffer_address(base, base->as_scratch_buf),
		};
		const VkAccelerationStructureBuildRangeInfoKHR *pTlasRange = &tlasRangeInfo;

		VkCommandPoolCreateInfo poolInfo2 = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, .queueFamilyIndex = (uint32_t)base->core->graphicsQueueFamily};
		VkCommandPool tlasPool;
		vkCreateCommandPool(base->core->device, &poolInfo2, NULL, &tlasPool);
		VkCommandBufferAllocateInfo cmdInfo2 = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = tlasPool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
		VkCommandBuffer tlasCmd;
		vkAllocateCommandBuffers(base->core->device, &cmdInfo2, &tlasCmd);
		vkBeginCommandBuffer(tlasCmd, &VK_CMD_BEGIN_INFO_ONETIME);
		rt_funcs.CmdBuildAccelerationStructuresKHR(tlasCmd, 1, &tlasBuildInfo, &pTlasRange);
		vkEndCommandBuffer(tlasCmd);
		VkSubmitInfo submitInfo2 = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &tlasCmd};
		VkFence fence;
		VkFenceCreateInfo fenceInfo = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
		VK_CHECK(vkCreateFence(base->core->device, &fenceInfo, NULL, &fence), "Failed to create TLAS fence");
		pthread_mutex_lock(&base->core->graphicsQueueMutex);
		vkQueueSubmit(base->core->graphicsQueue, 1, &submitInfo2, fence);
		pthread_mutex_unlock(&base->core->graphicsQueueMutex);
		VK_CHECK(vkWaitForFences(base->core->device, 1, &fence, VK_TRUE, UINT64_MAX), "Failed to wait for TLAS fence");
		vkDestroyFence(base->core->device, fence, NULL);
		vkDestroyCommandPool(base->core->device, tlasPool, NULL);
	}

	return true;
}

void rt_base_session_cleanup(RTBase *base)
{
	if (!base || !base->session_active)
		return;
	rt_base_cleanup_session_buffers(base);
	base->session_active = false;
}

// ============================================================================
// Public API — Accessors
// ============================================================================

VkBuffer rt_base_instance_buf(RTBase *base)
{
	return base->instance_buf;
}
bool rt_base_is_supported(RTBase *base)
{
	return base->supported;
}
VkAccelerationStructureKHR rt_base_tlas(RTBase *base)
{
	return base->tlas;
}
VkBuffer rt_base_node_staging_buf(RTBase *base)
{
	return base->node_staging_buf;
}
uint32_t rt_base_vcount(RTBase *base)
{
	return base->vcount;
}
uint32_t rt_base_ecount(RTBase *base)
{
	return base->ecount;
}
bool rt_base_fp64_supported(RTBase *base)
{
	return base->fp64_supported;
}

// ============================================================================
// Public API — Command Buffer Lifecycle
// ============================================================================

VkCommandBuffer rt_base_begin_commands(RTBase *base)
{
	VkCommandBuffer cmd = base->cmd_buf;
	VK_CHECK(vkResetCommandBuffer(cmd, 0), "Failed to reset RTBase worker cmd");
	VkCommandBufferBeginInfo beginInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
	VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo), "Failed to begin RTBase worker cmd");
	return cmd;
}

bool rt_base_submit_commands(RTBase *base, VkFence fence)
{
	VkCommandBuffer cmd = base->cmd_buf;
	VK_CHECK(vkEndCommandBuffer(cmd), "Failed to end RTBase worker cmd");
	VkSubmitInfo submitInfo = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
	pthread_mutex_lock(&base->core->graphicsQueueMutex);
	VK_CHECK(vkQueueSubmit(base->core->graphicsQueue, 1, &submitInfo, fence), "Failed to submit RTBase worker");
	pthread_mutex_unlock(&base->core->graphicsQueueMutex);
	return true;
}

void rt_base_record_tlas_update(RTBase *base, VkCommandBuffer cmd, uint32_t active_vcount)
{
	VkAccelerationStructureGeometryKHR tlasGeometry = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR, .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR, .flags = VK_GEOMETRY_OPAQUE_BIT_KHR, .geometry.instances = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR, .data.deviceAddress = rt_base_get_buffer_address(base, base->instance_buf)}};
	VkAccelerationStructureBuildGeometryInfoKHR tlasBuildInfo = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
		.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
		.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR,
		.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR,
		.srcAccelerationStructure = base->tlas,
		.dstAccelerationStructure = base->tlas,
		.geometryCount = 1,
		.pGeometries = &tlasGeometry,
		.scratchData.deviceAddress = rt_base_get_buffer_address(base, base->as_scratch_buf),
	};
	VkAccelerationStructureBuildRangeInfoKHR tlasRangeInfo = {.primitiveCount = active_vcount};
	const VkAccelerationStructureBuildRangeInfoKHR *pTlasRange = &tlasRangeInfo;
	rt_funcs.CmdBuildAccelerationStructuresKHR(cmd, 1, &tlasBuildInfo, &pTlasRange);
}

// ============================================================================
// Public API — Position Readback
// ============================================================================

bool rt_base_readback_positions(RTBase *base, uint32_t vcount, igraph_matrix_t *out)
{
	if (!out)
		return false;

	VkDeviceSize nodeSize = sizeof(vec4) * vcount;
	void *mapped;
	VK_CHECK(vkMapMemory(base->core->device, base->node_staging_mem, 0, nodeSize, 0, &mapped), "Failed to map RTBase readback");

	float *positions = (float *)mapped;
	igraph_integer_t vc = (igraph_integer_t)vcount;
	igraph_integer_t ncols = igraph_matrix_ncol(out);

	for (igraph_integer_t i = 0; i < vc; i++) {
		MATRIX(*out, i, 0) = positions[i * 4 + 0];
		MATRIX(*out, i, 1) = positions[i * 4 + 1];
		if (ncols > 2)
			MATRIX(*out, i, 2) = positions[i * 4 + 2];
	}
	vkUnmapMemory(base->core->device, base->node_staging_mem);
	return true;
}