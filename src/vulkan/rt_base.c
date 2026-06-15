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

#include "vulkan/rt_helpers.h"
#include "vulkan/utils.h"

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
// Support Check
// ============================================================================

static bool rt_base_check_support(VkPhysicalDevice device)
{
	uint32_t devExtCount = 0;
	vkEnumerateDeviceExtensionProperties(device, NULL, &devExtCount, NULL);
	VkExtensionProperties *devExts = malloc(sizeof(VkExtensionProperties) * devExtCount);
	vkEnumerateDeviceExtensionProperties(device, NULL, &devExtCount, devExts);

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
		}
	}
	free(devExts);
	return supported;
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

	rt_helpers_destroy_as(base->core->device, base->blas);
	base->blas = VK_NULL_HANDLE;
	rt_helpers_destroy_as(base->core->device, base->tlas);
	base->tlas = VK_NULL_HANDLE;
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

	rt_helpers_load_functions(core);

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
// Internal — Per-Session Helpers
// ============================================================================

static void rt_base_create_worker_cmd(RTBase *base)
{
	VkCommandPoolCreateInfo poolInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = (uint32_t)base->core->graphicsQueueFamily};
	VK_CHECK(vkCreateCommandPool(base->core->device, &poolInfo, NULL, &base->cmd_pool), "Failed to create RTBase worker cmd pool");
	VkCommandBufferAllocateInfo cmdInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = base->cmd_pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
	VK_CHECK(vkAllocateCommandBuffers(base->core->device, &cmdInfo, &base->cmd_buf), "Failed to allocate RTBase worker cmd");
}

static VkDeviceSize rt_base_build_blas(RTBase *base, float search_radius)
{
	VkAccelerationStructureGeometryKHR blasGeometry = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR, .geometryType = VK_GEOMETRY_TYPE_AABBS_KHR, .flags = VK_GEOMETRY_OPAQUE_BIT_KHR};

	// Size the BLAS
	VkAabbPositionsKHR aabb = {.minX = -search_radius, .minY = -search_radius, .minZ = -search_radius, .maxX = search_radius, .maxY = search_radius, .maxZ = search_radius};

	VkBuffer aabbBuf = VK_NULL_HANDLE;
	VkDeviceMemory aabbMem = VK_NULL_HANDLE;
	rt_helpers_create_buffer(base->core, sizeof(VkAabbPositionsKHR), VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, &aabbBuf, &aabbMem);
	void *mapped;
	vkMapMemory(base->core->device, aabbMem, 0, sizeof(VkAabbPositionsKHR), 0, &mapped);
	memcpy(mapped, &aabb, sizeof(VkAabbPositionsKHR));
	vkUnmapMemory(base->core->device, aabbMem);

	blasGeometry.geometry.aabbs.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR;
	blasGeometry.geometry.aabbs.data.deviceAddress = rt_helpers_get_buffer_device_address(base->core->device, aabbBuf);
	blasGeometry.geometry.aabbs.stride = sizeof(VkAabbPositionsKHR);

	VkAccelerationStructureBuildGeometryInfoKHR blasBuildInfo = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR, .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR, .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR, .geometryCount = 1, .pGeometries = &blasGeometry};

	uint32_t maxPrims = 1;
	VkAccelerationStructureBuildSizesInfoKHR blasSizeInfo = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
	rt_helpers_get_GetAccelerationStructureBuildSizesKHR()(base->core->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &blasBuildInfo, &maxPrims, &blasSizeInfo);

	rt_helpers_create_device_buffer(base->core, blasSizeInfo.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, &base->blas_buf, &base->blas_mem);

	VkAccelerationStructureCreateInfoKHR blasCreateInfo = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR, .buffer = base->blas_buf, .offset = 0, .size = blasSizeInfo.accelerationStructureSize, .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR};
	VK_CHECK(rt_helpers_get_CreateAccelerationStructureKHR()(base->core->device, &blasCreateInfo, NULL, &base->blas), "Failed to create BLAS");

	// Size the TLAS (to get scratch size)
	VkAccelerationStructureGeometryKHR tlasGeometry = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR, .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR, .flags = VK_GEOMETRY_OPAQUE_BIT_KHR, .geometry.instances = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR}};
	{
		tlasGeometry.geometry.instances.data.deviceAddress = 0;
		VkAccelerationStructureBuildGeometryInfoKHR tlasBuildInfo = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR, .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR, .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR, .geometryCount = 1, .pGeometries = &tlasGeometry};
		VkAccelerationStructureBuildSizesInfoKHR tlasSizeInfo = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
		uint32_t maxInstances = base->vcount;
		rt_helpers_get_GetAccelerationStructureBuildSizesKHR()(base->core->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &tlasBuildInfo, &maxInstances, &tlasSizeInfo);

		// Shared scratch buffer
		VkDeviceSize scratchSize = blasSizeInfo.buildScratchSize > tlasSizeInfo.buildScratchSize ? blasSizeInfo.buildScratchSize : tlasSizeInfo.buildScratchSize;
		rt_helpers_create_device_buffer(base->core, scratchSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, &base->as_scratch_buf, &base->as_scratch_mem);
	}

	// Build the BLAS (one-time, synchronous)
	blasBuildInfo.dstAccelerationStructure = base->blas;
	blasBuildInfo.scratchData.deviceAddress = rt_helpers_get_buffer_device_address(base->core->device, base->as_scratch_buf);
	const VkAccelerationStructureBuildRangeInfoKHR blasRangeInfo = {.primitiveCount = 1};
	const VkAccelerationStructureBuildRangeInfoKHR *pBlasRange = &blasRangeInfo;

	VkCommandPool blasPool;
	VkCommandBuffer blasCmd;
	VK_ONE_SHOT_BEGIN(base->core->device, (uint32_t)base->core->graphicsQueueFamily, blasPool, blasCmd);

	rt_helpers_get_CmdBuildAccelerationStructuresKHR()(blasCmd, 1, &blasBuildInfo, &pBlasRange);

	pthread_mutex_lock(&base->core->graphicsQueueMutex);
	VK_ONE_SHOT_END(base->core->device, base->core->graphicsQueue, blasPool, blasCmd);
	pthread_mutex_unlock(&base->core->graphicsQueueMutex);

	VK_DESTROY_BUFFER(base->core->device, aabbBuf, aabbMem);

	return blasSizeInfo.buildScratchSize;
}

static bool rt_base_build_tlas(RTBase *base, igraph_matrix_t *init_positions)
{
	uint32_t vcount = base->vcount;

	VkAccelerationStructureGeometryKHR tlasGeometry = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR, .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR, .flags = VK_GEOMETRY_OPAQUE_BIT_KHR, .geometry.instances = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR}};

	// Create instance buffer
	VkDeviceSize instSize = sizeof(VkAccelerationStructureInstanceKHR) * vcount;
	rt_helpers_create_device_buffer(base->core, instSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &base->instance_buf, &base->instance_mem);

	uint32_t ncols = (uint32_t)igraph_matrix_ncol(init_positions);

	VkAccelerationStructureInstanceKHR *instances = calloc(vcount, sizeof(VkAccelerationStructureInstanceKHR));
	uint64_t blasAddr = rt_helpers_get_as_device_address(base->core->device, base->blas);
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
	rt_helpers_staging_upload(base->core, base->instance_buf, instances, instSize);
	free(instances);

	// Size and create TLAS
	uint64_t instAddr = rt_helpers_get_buffer_device_address(base->core->device, base->instance_buf);
	tlasGeometry.geometry.instances.data.deviceAddress = instAddr;

	VkAccelerationStructureBuildSizesInfoKHR tlasSizeInfo = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
	{
		VkAccelerationStructureBuildGeometryInfoKHR tmpInfo = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR, .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR, .geometryCount = 1, .pGeometries = &tlasGeometry};
		uint32_t maxInstances = vcount;
		rt_helpers_get_GetAccelerationStructureBuildSizesKHR()(base->core->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &tmpInfo, &maxInstances, &tlasSizeInfo);
	}

	rt_helpers_create_device_buffer(base->core, tlasSizeInfo.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, &base->tlas_buf, &base->tlas_mem);
	VkAccelerationStructureCreateInfoKHR tlasCreate = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR, .buffer = base->tlas_buf, .size = tlasSizeInfo.accelerationStructureSize, .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR};
	VK_CHECK(rt_helpers_get_CreateAccelerationStructureKHR()(base->core->device, &tlasCreate, NULL, &base->tlas), "Failed to create TLAS");

	// Build the TLAS
	VkAccelerationStructureBuildRangeInfoKHR tlasRangeInfo = {.primitiveCount = vcount};
	const VkAccelerationStructureBuildRangeInfoKHR *pTlasRange = &tlasRangeInfo;
	VkAccelerationStructureBuildGeometryInfoKHR tlasBuildInfo = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
		.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
		.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR,
		.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
		.dstAccelerationStructure = base->tlas,
		.geometryCount = 1,
		.pGeometries = &tlasGeometry,
		.scratchData.deviceAddress = rt_helpers_get_buffer_device_address(base->core->device, base->as_scratch_buf),
	};

	VkCommandPool tlasPool;
	VkCommandBuffer tlasCmd;
	VK_ONE_SHOT_BEGIN(base->core->device, (uint32_t)base->core->graphicsQueueFamily, tlasPool, tlasCmd);

	rt_helpers_get_CmdBuildAccelerationStructuresKHR()(tlasCmd, 1, &tlasBuildInfo, &pTlasRange);

	pthread_mutex_lock(&base->core->graphicsQueueMutex);
	VK_ONE_SHOT_END(base->core->device, base->core->graphicsQueue, tlasPool, tlasCmd);
	pthread_mutex_unlock(&base->core->graphicsQueueMutex);

	return true;
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

	base->vcount = (uint32_t)igraph_vcount(graph);
	base->ecount = (uint32_t)igraph_ecount(graph);
	base->search_radius = search_radius;
	base->fp64_supported = base->core->fp64_atomics_supported;
	base->session_active = true;

	rt_base_create_worker_cmd(base);

	// Staging buffer for periodic position readback
	rt_helpers_create_buffer(base->core, sizeof(vec4) * base->vcount, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &base->node_staging_buf, &base->node_staging_mem);

	// BLAS + TLAS build
	rt_base_build_blas(base, search_radius);
	rt_base_build_tlas(base, init_positions);

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
