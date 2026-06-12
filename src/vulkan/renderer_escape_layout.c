#include "vulkan/renderer_escape_layout.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vulkan/buffers.h"
#include "vulkan/commands.h"
#include "vulkan/utils.h"

// ============================================================================
// Extension function pointers (loaded via vkGetDeviceProcAddr)
// ============================================================================

static PFN_vkGetBufferDeviceAddressKHR pfnGetBufferDeviceAddressKHR;
static PFN_vkCreateAccelerationStructureKHR pfnCreateAccelerationStructureKHR;
static PFN_vkDestroyAccelerationStructureKHR pfnDestroyAccelerationStructureKHR;
static PFN_vkGetAccelerationStructureBuildSizesKHR pfnGetAccelerationStructureBuildSizesKHR;
static PFN_vkGetAccelerationStructureDeviceAddressKHR pfnGetAccelerationStructureDeviceAddressKHR;
static PFN_vkCmdBuildAccelerationStructuresKHR pfnCmdBuildAccelerationStructuresKHR;
static PFN_vkCreateRayTracingPipelinesKHR pfnCreateRayTracingPipelinesKHR;
static PFN_vkGetRayTracingShaderGroupHandlesKHR pfnGetRayTracingShaderGroupHandlesKHR;
static PFN_vkCmdTraceRaysKHR pfnCmdTraceRaysKHR;

static bool rt_functions_loaded = false;

static void load_rt_function_pointers(VkDevice device)
{
	if (rt_functions_loaded)
		return;
	pfnGetBufferDeviceAddressKHR = (PFN_vkGetBufferDeviceAddressKHR)vkGetDeviceProcAddr(device, "vkGetBufferDeviceAddressKHR");
	pfnCreateAccelerationStructureKHR = (PFN_vkCreateAccelerationStructureKHR)vkGetDeviceProcAddr(device, "vkCreateAccelerationStructureKHR");
	pfnDestroyAccelerationStructureKHR = (PFN_vkDestroyAccelerationStructureKHR)vkGetDeviceProcAddr(device, "vkDestroyAccelerationStructureKHR");
	pfnGetAccelerationStructureBuildSizesKHR = (PFN_vkGetAccelerationStructureBuildSizesKHR)vkGetDeviceProcAddr(device, "vkGetAccelerationStructureBuildSizesKHR");
	pfnGetAccelerationStructureDeviceAddressKHR = (PFN_vkGetAccelerationStructureDeviceAddressKHR)vkGetDeviceProcAddr(device, "vkGetAccelerationStructureDeviceAddressKHR");
	pfnCmdBuildAccelerationStructuresKHR = (PFN_vkCmdBuildAccelerationStructuresKHR)vkGetDeviceProcAddr(device, "vkCmdBuildAccelerationStructuresKHR");
	pfnCreateRayTracingPipelinesKHR = (PFN_vkCreateRayTracingPipelinesKHR)vkGetDeviceProcAddr(device, "vkCreateRayTracingPipelinesKHR");
	pfnGetRayTracingShaderGroupHandlesKHR = (PFN_vkGetRayTracingShaderGroupHandlesKHR)vkGetDeviceProcAddr(device, "vkGetRayTracingShaderGroupHandlesKHR");
	pfnCmdTraceRaysKHR = (PFN_vkCmdTraceRaysKHR)vkGetDeviceProcAddr(device, "vkCmdTraceRaysKHR");

	// Validate all pointers loaded
	int null_count = 0;
	if (!pfnGetBufferDeviceAddressKHR) {
		fprintf(stderr, "[Escape RT] ERROR: pfnGetBufferDeviceAddressKHR is NULL\n");
		null_count++;
	}
	if (!pfnCreateAccelerationStructureKHR) {
		fprintf(stderr, "[Escape RT] ERROR: pfnCreateAccelerationStructureKHR is NULL\n");
		null_count++;
	}
	if (!pfnDestroyAccelerationStructureKHR) {
		fprintf(stderr, "[Escape RT] ERROR: pfnDestroyAccelerationStructureKHR is NULL\n");
		null_count++;
	}
	if (!pfnGetAccelerationStructureBuildSizesKHR) {
		fprintf(stderr, "[Escape RT] ERROR: pfnGetAccelerationStructureBuildSizesKHR is NULL\n");
		null_count++;
	}
	if (!pfnGetAccelerationStructureDeviceAddressKHR) {
		fprintf(stderr, "[Escape RT] ERROR: pfnGetAccelerationStructureDeviceAddressKHR is NULL\n");
		null_count++;
	}
	if (!pfnCmdBuildAccelerationStructuresKHR) {
		fprintf(stderr, "[Escape RT] ERROR: pfnCmdBuildAccelerationStructuresKHR is NULL\n");
		null_count++;
	}
	if (!pfnCreateRayTracingPipelinesKHR) {
		fprintf(stderr, "[Escape RT] ERROR: pfnCreateRayTracingPipelinesKHR is NULL\n");
		null_count++;
	}
	if (!pfnGetRayTracingShaderGroupHandlesKHR) {
		fprintf(stderr, "[Escape RT] ERROR: pfnGetRayTracingShaderGroupHandlesKHR is NULL\n");
		null_count++;
	}
	if (!pfnCmdTraceRaysKHR) {
		fprintf(stderr, "[Escape RT] ERROR: pfnCmdTraceRaysKHR is NULL\n");
		null_count++;
	}

	rt_functions_loaded = true;
	fprintf(stderr, "[Escape RT] Loaded extension function pointers (%s)\n", null_count > 0 ? "HAS NULL PTRS" : "all valid");
}

// ============================================================================
// Tetrahedron geometry (4 vertices, 4 triangles)
// ============================================================================

static const float TETRA_VERTICES[4][3] = {
	{0.0f, 1.0f, 0.0f},
	{0.0f, -0.333333f, 0.942809f},
	{-0.816497f, -0.333333f, -0.471405f},
	{0.816497f, -0.333333f, -0.471405f},
};
static const uint32_t TETRA_INDICES[12] = {0, 1, 2, 0, 2, 3, 0, 3, 1, 1, 3, 2};

// ============================================================================
// Helpers
// ============================================================================

typedef struct
{
	float position[4];
	float escape_vector[4];
	float freedom[4];
} NodePhysicsGPU;

static VkDeviceAddress get_buffer_device_address(VkDevice device, VkBuffer buffer)
{
	VkBufferDeviceAddressInfoKHR addrInfo = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
		.buffer = buffer,
	};
	return pfnGetBufferDeviceAddressKHR(device, &addrInfo);
}

// ============================================================================
// BLAS — Bottom-Level Acceleration Structure (single tetrahedron)
// ============================================================================

void renderer_escape_build_blas(Renderer *r)
{
	VkDevice dev = r->core.device;
	VkPhysicalDevice phys = r->core.physicalDevice;
	load_rt_function_pointers(dev);

	// Vertex buffer (local — destroyed after BLAS build, geometry data baked in)
	VkDeviceSize vertSize = sizeof(TETRA_VERTICES);
	VkBuffer vertBuf;
	VkDeviceMemory vertMem;
	create_buffer(dev, phys, vertSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &vertBuf, &vertMem);
	update_buffer(dev, vertMem, vertSize, TETRA_VERTICES);

	// Index buffer (local — destroyed after BLAS build)
	VkDeviceSize idxSize = sizeof(TETRA_INDICES);
	VkBuffer idxBuffer;
	VkDeviceMemory idxMemory;
	create_buffer(dev, phys, idxSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &idxBuffer, &idxMemory);
	update_buffer(dev, idxMemory, idxSize, TETRA_INDICES);

	// Get device addresses
	VkBufferDeviceAddressInfoKHR vertAddrInfo = {.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = vertBuf};
	VkDeviceAddress vertAddr = pfnGetBufferDeviceAddressKHR(dev, &vertAddrInfo);
	VkBufferDeviceAddressInfoKHR idxAddrInfo = {.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = idxBuffer};
	VkDeviceAddress idxAddr = pfnGetBufferDeviceAddressKHR(dev, &idxAddrInfo);

	// Geometry definition
	VkAccelerationStructureGeometryTrianglesDataKHR triData = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
		.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
		.vertexData.deviceAddress = vertAddr,
		.vertexStride = sizeof(float) * 3,
		.maxVertex = 3,
		.indexType = VK_INDEX_TYPE_UINT32,
		.indexData.deviceAddress = idxAddr,
	};

	VkAccelerationStructureGeometryKHR geometry = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
		.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
		.flags = 0,
		.geometry.triangles = triData,
	};

	// Build size query
	VkAccelerationStructureBuildGeometryInfoKHR buildInfo = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
		.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
		.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
		.geometryCount = 1,
		.pGeometries = &geometry,
	};

	uint32_t primCount = 4;
	VkAccelerationStructureBuildSizesInfoKHR sizeInfo = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
	};
	pfnGetAccelerationStructureBuildSizesKHR(dev, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &primCount, &sizeInfo);

	// AS storage buffer
	create_buffer(dev, phys, sizeInfo.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &r->escape_blas_buffer, &r->escape_blas_memory);

	VkAccelerationStructureCreateInfoKHR asCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
		.buffer = r->escape_blas_buffer,
		.size = sizeInfo.accelerationStructureSize,
		.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
	};
	VK_CHECK(pfnCreateAccelerationStructureKHR(dev, &asCreateInfo, NULL, &r->escape_blas), "Failed to create BLAS");

	// Scratch buffer
	create_buffer(dev, phys, sizeInfo.buildScratchSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &r->escape_tlas_scratch_buffer, &r->escape_tlas_scratch_memory);

	VkBufferDeviceAddressInfoKHR scratchAddrInfo = {.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = r->escape_tlas_scratch_buffer};
	VkDeviceAddress scratchAddr = pfnGetBufferDeviceAddressKHR(dev, &scratchAddrInfo);

	buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	buildInfo.dstAccelerationStructure = r->escape_blas;
	buildInfo.scratchData.deviceAddress = scratchAddr;

	VkAccelerationStructureBuildRangeInfoKHR rangeInfo = {.primitiveCount = 4};
	const VkAccelerationStructureBuildRangeInfoKHR *pRangeInfo = &rangeInfo;

	// Build BLAS
	VkCommandBuffer cmd;
	VkCommandBufferAllocateInfo cmdAllocInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = r->escape_cmd_pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1,
	};
	VK_CHECK(vkAllocateCommandBuffers(dev, &cmdAllocInfo, &cmd), "Failed to allocate BLAS command buffer");

	VkCommandBufferBeginInfo beginInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
	VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo), "Failed to begin BLAS command buffer");
	pfnCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, &pRangeInfo);
	vkEndCommandBuffer(cmd);

	VkSubmitInfo submitInfo = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
	VK_CHECK(vkQueueSubmit(r->core.graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE), "Failed to submit BLAS build");
	VK_CHECK(vkQueueWaitIdle(r->core.graphicsQueue), "Failed to wait for BLAS build");
	vkFreeCommandBuffers(dev, r->escape_cmd_pool, 1, &cmd);

	// Get BLAS device address
	VkAccelerationStructureDeviceAddressInfoKHR addrInfo = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
		.accelerationStructure = r->escape_blas,
	};
	r->escape_blas_device_address = pfnGetAccelerationStructureDeviceAddressKHR(dev, &addrInfo);

	// Clean up scratch + index + vertex buffers (no longer needed)
	VK_DESTROY_BUFFER(dev, r->escape_tlas_scratch_buffer, r->escape_tlas_scratch_memory);
	VK_DESTROY_BUFFER(dev, idxBuffer, idxMemory);
	VK_DESTROY_BUFFER(dev, vertBuf, vertMem);

	fprintf(stderr, "[Escape RT] BLAS built: handle=%lu\n", (unsigned long)r->escape_blas);
}

// ============================================================================
// TLAS — Top-Level Acceleration Structure (one instance per graph node)
// ============================================================================

void renderer_escape_build_tlas(Renderer *r, GraphData *data, uint32_t node_count)
{
	VkDevice dev = r->core.device;
	VkPhysicalDevice phys = r->core.physicalDevice;

	// Instance buffer (host-visible, filled per node)
	VkDeviceSize instSize = sizeof(VkAccelerationStructureInstanceKHR) * node_count;
	create_buffer(dev, phys, instSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->escape_tlas_instance_buffer, &r->escape_tlas_instance_memory);

	// Fill instances
	VkAccelerationStructureInstanceKHR *instances;
	vkMapMemory(dev, r->escape_tlas_instance_memory, 0, instSize, 0, (void **)&instances);

	for (uint32_t i = 0; i < node_count; i++) {
		memset(&instances[i], 0, sizeof(VkAccelerationStructureInstanceKHR));

		float px = data->nodes[i].position[0];
		float py = data->nodes[i].position[1];
		float pz = data->nodes[i].position[2];
		float scale = r->escape_bb_diag * 0.02f * log2f((float)data->nodes[i].degree + 2.0f) * 0.5f;

		VkTransformMatrixKHR transform = {{
			{scale, 0.0f, 0.0f, px},
			{0.0f, scale, 0.0f, py},
			{0.0f, 0.0f, scale, pz},
		}};
		instances[i].transform = transform;
		instances[i].instanceCustomIndex = i;
		instances[i].mask = 0xFF;
		instances[i].instanceShaderBindingTableRecordOffset = 0;
		instances[i].flags = 0;
		instances[i].accelerationStructureReference = r->escape_blas_device_address;
	}
	vkUnmapMemory(dev, r->escape_tlas_instance_memory);

	// Geometry definition
	VkBufferDeviceAddressInfoKHR instAddrInfo = {.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = r->escape_tlas_instance_buffer};
	VkDeviceAddress instAddr = pfnGetBufferDeviceAddressKHR(dev, &instAddrInfo);

	VkAccelerationStructureGeometryInstancesDataKHR instancesData = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
		.arrayOfPointers = VK_FALSE,
		.data.deviceAddress = instAddr,
	};

	VkAccelerationStructureGeometryKHR geometry = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
		.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
		.flags = 0,
		.geometry.instances = instancesData,
	};

	// Build size query
	VkAccelerationStructureBuildGeometryInfoKHR buildInfo = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
		.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
		.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR,
		.geometryCount = 1,
		.pGeometries = &geometry,
	};

	uint32_t primCount = node_count;
	VkAccelerationStructureBuildSizesInfoKHR sizeInfo = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
	};
	pfnGetAccelerationStructureBuildSizesKHR(dev, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &primCount, &sizeInfo);

	// AS storage buffer
	create_buffer(dev, phys, sizeInfo.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &r->escape_tlas_buffer, &r->escape_tlas_memory);

	VkAccelerationStructureCreateInfoKHR asCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
		.buffer = r->escape_tlas_buffer,
		.size = sizeInfo.accelerationStructureSize,
		.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
	};
	VK_CHECK(pfnCreateAccelerationStructureKHR(dev, &asCreateInfo, NULL, &r->escape_tlas), "Failed to create TLAS");

	// Scratch buffer
	create_buffer(dev, phys, sizeInfo.buildScratchSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &r->escape_tlas_scratch_buffer, &r->escape_tlas_scratch_memory);

	VkBufferDeviceAddressInfoKHR scratchAddrInfo = {.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = r->escape_tlas_scratch_buffer};
	VkDeviceAddress scratchAddr = pfnGetBufferDeviceAddressKHR(dev, &scratchAddrInfo);

	buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	buildInfo.dstAccelerationStructure = r->escape_tlas;
	buildInfo.scratchData.deviceAddress = scratchAddr;

	VkAccelerationStructureBuildRangeInfoKHR rangeInfo = {.primitiveCount = node_count};
	const VkAccelerationStructureBuildRangeInfoKHR *pRangeInfo = &rangeInfo;

	// Build TLAS (use escape_cmd_pool — worker-thread-safe)
	VkCommandBuffer cmd;
	VkCommandBufferAllocateInfo cmdAllocInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = r->escape_cmd_pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1,
	};
	VK_CHECK(vkAllocateCommandBuffers(dev, &cmdAllocInfo, &cmd), "Failed to allocate TLAS command buffer");

	VkCommandBufferBeginInfo beginInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
	VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo), "Failed to begin TLAS command buffer");
	pfnCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, &pRangeInfo);
	vkEndCommandBuffer(cmd);

	VkSubmitInfo submitInfo = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
	VK_CHECK(vkQueueSubmit(r->core.graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE), "Failed to submit TLAS build");
	VK_CHECK(vkQueueWaitIdle(r->core.graphicsQueue), "Failed to wait for TLAS build");
	vkFreeCommandBuffers(dev, r->escape_cmd_pool, 1, &cmd);

	// Clean up scratch (instance buffer kept for CPU updates)
	VK_DESTROY_BUFFER(dev, r->escape_tlas_scratch_buffer, r->escape_tlas_scratch_memory);

	fprintf(stderr, "[Escape RT] TLAS built: handle=%lu nodes=%u\n", (unsigned long)r->escape_tlas, node_count);
}

// ============================================================================
// RT Pipeline + SBT + Descriptor Sets
// ============================================================================

void renderer_escape_create_rt_pipeline(Renderer *r, VkBuffer physics_buffer)
{
	VkDevice dev = r->core.device;
	VkPhysicalDevice phys = r->core.physicalDevice;

	// --- Descriptor set layout ---
	VkDescriptorSetLayoutBinding bindings[] = {
		{0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, VK_SHADER_STAGE_RAYGEN_BIT_KHR, NULL}, {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_RAYGEN_BIT_KHR, NULL}, {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ANY_HIT_BIT_KHR, NULL}, {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ANY_HIT_BIT_KHR, NULL}, {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ANY_HIT_BIT_KHR, NULL},
	};
	VkDescriptorSetLayoutCreateInfo layoutInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 5,
		.pBindings = bindings,
	};
	VK_CHECK(vkCreateDescriptorSetLayout(dev, &layoutInfo, NULL, &r->escape_rt_desc_layout), "Failed to create RT desc set layout");

	// --- Pipeline layout with push constants ---
	VkPushConstantRange pushRange = {
		.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR,
		.offset = 0,
		.size = sizeof(uint32_t) + sizeof(float),
	};
	VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &r->escape_rt_desc_layout,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &pushRange,
	};
	VK_CHECK(vkCreatePipelineLayout(dev, &pipelineLayoutInfo, NULL, &r->escape_rt_pipeline_layout), "Failed to create RT pipeline layout");

	// --- Load shader modules ---
	VkShaderModule rgenModule = VK_NULL_HANDLE;
	VkShaderModule rchitModule = VK_NULL_HANDLE;
	VkShaderModule rmissModule = VK_NULL_HANDLE;
	VkShaderModule rahitModule = VK_NULL_HANDLE;
	VK_CHECK(create_shader_module(dev, ESCAPE_RGEN_SHADER_PATH, &rgenModule), "Failed to create rgen shader module");
	VK_CHECK(create_shader_module(dev, ESCAPE_RCHIT_SHADER_PATH, &rchitModule), "Failed to create rchit shader module");
	VK_CHECK(create_shader_module(dev, ESCAPE_RMISS_SHADER_PATH, &rmissModule), "Failed to create rmiss shader module");
	VK_CHECK(create_shader_module(dev, ESCAPE_RAHIT_SHADER_PATH, &rahitModule), "Failed to create rahit shader module");

	// --- Shader stages ---
	VkPipelineShaderStageCreateInfo stages[4] = {
		{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR, .module = rgenModule, .pName = "main"},
		{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_MISS_BIT_KHR, .module = rmissModule, .pName = "main"},
		{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, .module = rchitModule, .pName = "main"},
		{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_ANY_HIT_BIT_KHR, .module = rahitModule, .pName = "main"},
	};

	// --- Shader groups ---
	VkRayTracingShaderGroupCreateInfoKHR groups[3] = {
		{
			.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
			.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
			.generalShader = 0,
			.closestHitShader = VK_SHADER_UNUSED_KHR,
			.anyHitShader = VK_SHADER_UNUSED_KHR,
			.intersectionShader = VK_SHADER_UNUSED_KHR,
		},
		{
			.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
			.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
			.generalShader = 1,
			.closestHitShader = VK_SHADER_UNUSED_KHR,
			.anyHitShader = VK_SHADER_UNUSED_KHR,
			.intersectionShader = VK_SHADER_UNUSED_KHR,
		},
		{
			.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
			.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR,
			.generalShader = VK_SHADER_UNUSED_KHR,
			.closestHitShader = 2,
			.anyHitShader = 3,
			.intersectionShader = VK_SHADER_UNUSED_KHR,
		},
	};

	// --- Create RT pipeline ---
	VkRayTracingPipelineCreateInfoKHR rtPipelineInfo = {
		.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR,
		.stageCount = 4,
		.pStages = stages,
		.groupCount = 3,
		.pGroups = groups,
		.maxPipelineRayRecursionDepth = 1,
		.layout = r->escape_rt_pipeline_layout,
	};
	VK_CHECK(pfnCreateRayTracingPipelinesKHR(dev, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &rtPipelineInfo, NULL, &r->escape_rt_pipeline), "Failed to create RT pipeline");

	// Clean up shader modules
	vkDestroyShaderModule(dev, rgenModule, NULL);
	vkDestroyShaderModule(dev, rchitModule, NULL);
	vkDestroyShaderModule(dev, rmissModule, NULL);
	vkDestroyShaderModule(dev, rahitModule, NULL);

	// --- SBT (Shader Binding Table) ---
	// Query handle sizes
	VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtProps = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR,
	};
	VkPhysicalDeviceProperties2 props2 = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
		.pNext = &rtProps,
	};
	vkGetPhysicalDeviceProperties2(r->core.physicalDevice, &props2);

	r->escape_rt_handle_size = rtProps.shaderGroupHandleSize;
	r->escape_rt_handle_alignment = rtProps.shaderGroupHandleAlignment;
	r->escape_rt_base_alignment = rtProps.shaderGroupBaseAlignment;

	uint32_t handleSize = r->escape_rt_handle_size;
	uint32_t handleAlign = r->escape_rt_handle_alignment;
	uint32_t baseAlign = r->escape_rt_base_alignment;

	// Each region: aligned to baseAlignment
	uint32_t raygenRegionSize = VK_ALIGN_UP(handleSize, baseAlign);
	uint32_t missRegionSize = VK_ALIGN_UP(handleSize, baseAlign);
	uint32_t hitRegionSize = VK_ALIGN_UP(handleSize, baseAlign);

	uint32_t sbtSize = raygenRegionSize + missRegionSize + hitRegionSize;
	// Pad to baseAlignment boundary
	sbtSize = VK_ALIGN_UP(sbtSize, baseAlign);

	create_buffer(dev, phys, sbtSize, VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->escape_sbt_buffer, &r->escape_sbt_memory);

	// Get group handles
	uint8_t *groupHandles = (uint8_t *)malloc(handleSize * 3);
	VK_CHECK(pfnGetRayTracingShaderGroupHandlesKHR(dev, r->escape_rt_pipeline, 0, 3, handleSize * 3, groupHandles), "Failed to get RT shader group handles");

	// Copy handles into SBT
	uint8_t *sbtMapped;
	vkMapMemory(dev, r->escape_sbt_memory, 0, sbtSize, 0, (void **)&sbtMapped);
	memset(sbtMapped, 0, sbtSize);
	memcpy(sbtMapped + 0, groupHandles + handleSize * 0, handleSize);								  // raygen
	memcpy(sbtMapped + raygenRegionSize, groupHandles + handleSize * 1, handleSize);				  // miss
	memcpy(sbtMapped + raygenRegionSize + missRegionSize, groupHandles + handleSize * 2, handleSize); // hit
	vkUnmapMemory(dev, r->escape_sbt_memory);
	free(groupHandles);

	// SBT region device addresses
	VkBufferDeviceAddressInfoKHR sbtAddrInfo = {.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = r->escape_sbt_buffer};
	VkDeviceAddress sbtBase = pfnGetBufferDeviceAddressKHR(dev, &sbtAddrInfo);

	r->escape_sbt_raygen = (VkStridedDeviceAddressRegionKHR){
		.deviceAddress = sbtBase,
		.stride = VK_ALIGN_UP(handleSize, baseAlign),
		.size = VK_ALIGN_UP(handleSize, baseAlign),
	};
	r->escape_sbt_miss = (VkStridedDeviceAddressRegionKHR){
		.deviceAddress = sbtBase + raygenRegionSize,
		.stride = VK_ALIGN_UP(handleSize, handleAlign),
		.size = VK_ALIGN_UP(handleSize, handleAlign),
	};
	r->escape_sbt_hit = (VkStridedDeviceAddressRegionKHR){
		.deviceAddress = sbtBase + raygenRegionSize + missRegionSize,
		.stride = VK_ALIGN_UP(handleSize, handleAlign),
		.size = VK_ALIGN_UP(handleSize, handleAlign),
	};
	r->escape_sbt_callable = (VkStridedDeviceAddressRegionKHR){0};

	fprintf(stderr, "[Escape RT] SBT built: size=%u handleSize=%u raygen=%u miss=%u hit=%u\n", sbtSize, handleSize, raygenRegionSize, missRegionSize, hitRegionSize);

	// --- Descriptor pool ---
	VkDescriptorPoolSize poolSizes[] = {
		{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1},
		{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4},
	};
	VkDescriptorPoolCreateInfo poolInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = 1,
		.poolSizeCount = 2,
		.pPoolSizes = poolSizes,
	};
	VK_CHECK(vkCreateDescriptorPool(dev, &poolInfo, NULL, &r->escape_rt_descriptor_pool), "Failed to create RT descriptor pool");

	VkDescriptorSetAllocateInfo descAllocInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = r->escape_rt_descriptor_pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &r->escape_rt_desc_layout,
	};
	VK_CHECK(vkAllocateDescriptorSets(dev, &descAllocInfo, &r->escape_rt_desc_set), "Failed to allocate RT descriptor set");

	// --- Write descriptor set ---
	VkWriteDescriptorSetAccelerationStructureKHR asInfo = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
		.accelerationStructureCount = 1,
		.pAccelerationStructures = &r->escape_tlas,
	};
	VkDescriptorBufferInfo physBufInfo = {physics_buffer, 0, VK_WHOLE_SIZE};
	VkDescriptorBufferInfo adjBufInfo = {r->escape_adjacency_buffer, 0, VK_WHOLE_SIZE};
	VkDescriptorBufferInfo offsBufInfo = {r->escape_offsets_buffer, 0, VK_WHOLE_SIZE};
	VkDescriptorBufferInfo cntsBufInfo = {r->escape_counts_buffer, 0, VK_WHOLE_SIZE};
	VkWriteDescriptorSet writes[] = {
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.pNext = &asInfo,
			.dstSet = r->escape_rt_desc_set,
			.dstBinding = 0,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
		},
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = r->escape_rt_desc_set,
			.dstBinding = 1,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.pBufferInfo = &physBufInfo,
		},
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = r->escape_rt_desc_set,
			.dstBinding = 2,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.pBufferInfo = &adjBufInfo,
		},
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = r->escape_rt_desc_set,
			.dstBinding = 3,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.pBufferInfo = &offsBufInfo,
		},
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = r->escape_rt_desc_set,
			.dstBinding = 4,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.pBufferInfo = &cntsBufInfo,
		},
	};
	vkUpdateDescriptorSets(dev, 5, writes, 0, NULL);

	fprintf(stderr, "[Escape RT] RT pipeline + SBT + descriptors created\n");
}

// ============================================================================
// Update RT descriptor set with new physics buffer handle (after buffer recreation)
// ============================================================================

void renderer_escape_update_rt_physics_buffer(Renderer *r, VkBuffer physics_buffer)
{
	VkDevice dev = r->core.device;
	VkDescriptorBufferInfo physBufInfo = {physics_buffer, 0, VK_WHOLE_SIZE};
	VkDescriptorBufferInfo adjBufInfo = {r->escape_adjacency_buffer, 0, VK_WHOLE_SIZE};
	VkDescriptorBufferInfo offsBufInfo = {r->escape_offsets_buffer, 0, VK_WHOLE_SIZE};
	VkDescriptorBufferInfo cntsBufInfo = {r->escape_counts_buffer, 0, VK_WHOLE_SIZE};
	VkWriteDescriptorSet writes[] = {
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = r->escape_rt_desc_set,
			.dstBinding = 1,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.pBufferInfo = &physBufInfo,
		},
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = r->escape_rt_desc_set,
			.dstBinding = 2,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.pBufferInfo = &adjBufInfo,
		},
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = r->escape_rt_desc_set,
			.dstBinding = 3,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.pBufferInfo = &offsBufInfo,
		},
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = r->escape_rt_desc_set,
			.dstBinding = 4,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.pBufferInfo = &cntsBufInfo,
		},
	};
	vkUpdateDescriptorSets(dev, 4, writes, 0, NULL);
}

// ============================================================================
// CPU-side TLAS update — copy new positions into instance transforms, rebuild
// ============================================================================

void renderer_escape_update_tlas_cpu(Renderer *r, uint32_t node_count)
{
	VkDevice dev = r->core.device;

	// Update instance transform matrices from physics buffer
	VkAccelerationStructureInstanceKHR *instances;
	VkDeviceSize instSize = sizeof(VkAccelerationStructureInstanceKHR) * node_count;
	vkMapMemory(dev, r->escape_tlas_instance_memory, 0, instSize, 0, (void **)&instances);

	// Map physics buffer to read new positions
	NodePhysicsGPU *phys;
	vkMapMemory(dev, r->escape_physics_memory, 0, sizeof(NodePhysicsGPU) * node_count, 0, (void **)&phys);

	for (uint32_t i = 0; i < node_count; i++) {
		float px = phys[i].position[0];
		float py = phys[i].position[1];
		float pz = phys[i].position[2];
		float existingScale = instances[i].transform.matrix[0][0];
		instances[i].transform.matrix[0][3] = px;
		instances[i].transform.matrix[1][3] = py;
		instances[i].transform.matrix[2][3] = pz;
	}
	vkUnmapMemory(dev, r->escape_physics_memory);
	vkUnmapMemory(dev, r->escape_tlas_instance_memory);

	// Full TLAS rebuild (UPDATE_KHR is unreliable on Intel i915)
	pfnDestroyAccelerationStructureKHR(dev, r->escape_tlas, NULL);
	r->escape_tlas = VK_NULL_HANDLE;

	VkBufferDeviceAddressInfoKHR instAddrInfo = {.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = r->escape_tlas_instance_buffer};
	VkDeviceAddress instAddr = pfnGetBufferDeviceAddressKHR(dev, &instAddrInfo);

	VkAccelerationStructureGeometryInstancesDataKHR instancesData = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
		.arrayOfPointers = VK_FALSE,
		.data.deviceAddress = instAddr,
	};
	VkAccelerationStructureGeometryKHR geometry = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
		.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
		.flags = 0,
		.geometry.instances = instancesData,
	};

	VkAccelerationStructureBuildGeometryInfoKHR buildInfo = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
		.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
		.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
		.geometryCount = 1,
		.pGeometries = &geometry,
	};

	uint32_t primCount = node_count;
	VkAccelerationStructureBuildSizesInfoKHR sizeInfo = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
	};
	pfnGetAccelerationStructureBuildSizesKHR(dev, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &primCount, &sizeInfo);

	// Recreate TLAS buffer
	VK_DESTROY_BUFFER(dev, r->escape_tlas_buffer, r->escape_tlas_memory);
	create_buffer(dev, r->core.physicalDevice, sizeInfo.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &r->escape_tlas_buffer, &r->escape_tlas_memory);

	VkAccelerationStructureCreateInfoKHR asCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
		.buffer = r->escape_tlas_buffer,
		.size = sizeInfo.accelerationStructureSize,
		.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
	};
	VK_CHECK(pfnCreateAccelerationStructureKHR(dev, &asCreateInfo, NULL, &r->escape_tlas), "Failed to recreate TLAS");

	// Scratch buffer
	VkBuffer scratchBuf;
	VkDeviceMemory scratchMem;
	create_buffer(dev, r->core.physicalDevice, sizeInfo.buildScratchSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &scratchBuf, &scratchMem);

	VkBufferDeviceAddressInfoKHR scratchAddrInfo = {.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = scratchBuf};
	buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	buildInfo.dstAccelerationStructure = r->escape_tlas;
	buildInfo.scratchData.deviceAddress = pfnGetBufferDeviceAddressKHR(dev, &scratchAddrInfo);

	VkAccelerationStructureBuildRangeInfoKHR rangeInfo = {.primitiveCount = node_count};
	const VkAccelerationStructureBuildRangeInfoKHR *pRangeInfo = &rangeInfo;

	// Record + submit (use escape_cmd_pool — worker-thread-safe)
	VkCommandBuffer cmd;
	VkCommandBufferAllocateInfo cmdAllocInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = r->escape_cmd_pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1,
	};
	VK_CHECK(vkAllocateCommandBuffers(dev, &cmdAllocInfo, &cmd), "Failed to allocate TLAS update cmd buf");

	VkCommandBufferBeginInfo beginInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
	VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo), "Failed to begin TLAS update cmd buf");
	pfnCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, &pRangeInfo);
	vkEndCommandBuffer(cmd);

	VkSubmitInfo submitInfo = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
	VK_CHECK(vkQueueSubmit(r->core.graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE), "Failed to submit TLAS update");
	VK_CHECK(vkQueueWaitIdle(r->core.graphicsQueue), "Failed to wait for TLAS update");
	vkFreeCommandBuffers(dev, r->escape_cmd_pool, 1, &cmd);

	VK_DESTROY_BUFFER(dev, scratchBuf, scratchMem);

	// Update TLAS device address in descriptor set (new handle)
	VkWriteDescriptorSetAccelerationStructureKHR asInfo = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
		.accelerationStructureCount = 1,
		.pAccelerationStructures = &r->escape_tlas,
	};
	VkWriteDescriptorSet write = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.pNext = &asInfo,
		.dstSet = r->escape_rt_desc_set,
		.dstBinding = 0,
		.dstArrayElement = 0,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
	};
	vkUpdateDescriptorSets(dev, 1, &write, 0, NULL);
}

// ============================================================================
// Record RT dispatch into command buffer
// ============================================================================

void renderer_escape_record_rt_pass(VkCommandBuffer cmd, Renderer *r, uint32_t node_count, uint32_t frame_index)
{
	if (!pfnCmdTraceRaysKHR) {
		fprintf(stderr, "[Escape RT] ERROR: pfnCmdTraceRaysKHR is NULL, skipping RT pass\n");
		return;
	}

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, r->escape_rt_pipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, r->escape_rt_pipeline_layout, 0, 1, &r->escape_rt_desc_set, 0, NULL);

	struct
	{
		uint32_t frame_index;
		float ray_max_distance;
	} pc;
	pc.frame_index = frame_index;
	pc.ray_max_distance = r->escape_bb_diag * 2.0f;
	vkCmdPushConstants(cmd, r->escape_rt_pipeline_layout, VK_SHADER_STAGE_RAYGEN_BIT_KHR, 0, sizeof(pc), &pc);

	if (frame_index == 0) {
		fprintf(stderr, "[Escape RT] Dispatch: nodes=%u ray_max=%.1f\n", node_count, pc.ray_max_distance);
		fprintf(stderr, "[Escape RT]   pipeline=%lu desc_set=%lu\n", (unsigned long)r->escape_rt_pipeline, (unsigned long)r->escape_rt_desc_set);
		fprintf(stderr, "[Escape RT]   SBT raygen addr=%lu stride=%lu size=%lu\n", (unsigned long)r->escape_sbt_raygen.deviceAddress, (unsigned long)r->escape_sbt_raygen.stride, (unsigned long)r->escape_sbt_raygen.size);
		fprintf(stderr, "[Escape RT]   SBT miss   addr=%lu stride=%lu size=%lu\n", (unsigned long)r->escape_sbt_miss.deviceAddress, (unsigned long)r->escape_sbt_miss.stride, (unsigned long)r->escape_sbt_miss.size);
		fprintf(stderr, "[Escape RT]   SBT hit    addr=%lu stride=%lu size=%lu\n", (unsigned long)r->escape_sbt_hit.deviceAddress, (unsigned long)r->escape_sbt_hit.stride, (unsigned long)r->escape_sbt_hit.size);
		fprintf(stderr, "[Escape RT]   physics_buf=%lu TLAS=%lu\n", (unsigned long)r->escape_physics_buffer, (unsigned long)r->escape_tlas);
	}

	pfnCmdTraceRaysKHR(cmd, &r->escape_sbt_raygen, &r->escape_sbt_miss, &r->escape_sbt_hit, &r->escape_sbt_callable, node_count, 1, 1);
}

// ============================================================================
// Cleanup all RT resources
// ============================================================================

void renderer_escape_cleanup_rt(Renderer *r)
{
	VkDevice dev = r->core.device;

	VK_DESTROY_BUFFER(dev, r->escape_sbt_buffer, r->escape_sbt_memory);

	if (r->escape_rt_pipeline != VK_NULL_HANDLE)
		vkDestroyPipeline(dev, r->escape_rt_pipeline, NULL);
	if (r->escape_rt_pipeline_layout != VK_NULL_HANDLE)
		vkDestroyPipelineLayout(dev, r->escape_rt_pipeline_layout, NULL);
	if (r->escape_rt_desc_layout != VK_NULL_HANDLE)
		vkDestroyDescriptorSetLayout(dev, r->escape_rt_desc_layout, NULL);
	if (r->escape_rt_descriptor_pool != VK_NULL_HANDLE)
		vkDestroyDescriptorPool(dev, r->escape_rt_descriptor_pool, NULL);

	if (r->escape_tlas != VK_NULL_HANDLE)
		pfnDestroyAccelerationStructureKHR(dev, r->escape_tlas, NULL);
	VK_DESTROY_BUFFER(dev, r->escape_tlas_buffer, r->escape_tlas_memory);
	VK_DESTROY_BUFFER(dev, r->escape_tlas_instance_buffer, r->escape_tlas_instance_memory);

	if (r->escape_blas != VK_NULL_HANDLE)
		pfnDestroyAccelerationStructureKHR(dev, r->escape_blas, NULL);
	VK_DESTROY_BUFFER(dev, r->escape_blas_buffer, r->escape_blas_memory);
}
