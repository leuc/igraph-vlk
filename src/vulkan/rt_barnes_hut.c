// =============================================================================
// BHRT — Barnes-Hut Ray-Traced Force Computation Engine
// =============================================================================
//
// Implements GPU-accelerated O(N log N) Barnes-Hut force computation using
// an octree built on the CPU, linearized to a triangle-mesh BLAS, and
// traversed via ray queries (rayQueryEXT) in a compute shader.
//
// Design:
//   1. CPU: Build octree from particle positions (barnes_hut_tree.h)
//   2. CPU: DFS-linearize octree → triangle per node + device-node array
//   3. GPU: Upload vertices/indices → build triangle-mesh BLAS
//   4. GPU: Build TLAS (single instance over BLAS)
//   5. GPU: Dispatch bh_force.comp — ray queries walk the octree via BVH
//
// Each iteration rebuilds the octree + BLAS/TLAS because positions change.
// =============================================================================

#include "vulkan/rt_barnes_hut.h"

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "graph/barnes_hut_tree.h"
#include "vulkan/buffers.h"
#include "vulkan/utils.h"

#define BHRT_WORKGROUP_SIZE 256

// ============================================================================
// RT Function Pointers (loaded once)
// ============================================================================

static struct
{
	PFN_vkCreateAccelerationStructureKHR CreateAccelerationStructureKHR;
	PFN_vkDestroyAccelerationStructureKHR DestroyAccelerationStructureKHR;
	PFN_vkGetAccelerationStructureBuildSizesKHR GetAccelerationStructureBuildSizesKHR;
	PFN_vkCmdBuildAccelerationStructuresKHR CmdBuildAccelerationStructuresKHR;
	PFN_vkGetAccelerationStructureDeviceAddressKHR GetAccelerationStructureDeviceAddressKHR;
	PFN_vkGetBufferDeviceAddressKHR GetBufferDeviceAddressKHR;
} bhrt_funcs;

static void bhrt_load_rt_functions(VulkanCore *core)
{
	if (bhrt_funcs.CreateAccelerationStructureKHR)
		return;
	bhrt_funcs.CreateAccelerationStructureKHR = (PFN_vkCreateAccelerationStructureKHR)vkGetDeviceProcAddr(core->device, "vkCreateAccelerationStructureKHR");
	bhrt_funcs.DestroyAccelerationStructureKHR = (PFN_vkDestroyAccelerationStructureKHR)vkGetDeviceProcAddr(core->device, "vkDestroyAccelerationStructureKHR");
	bhrt_funcs.GetAccelerationStructureBuildSizesKHR = (PFN_vkGetAccelerationStructureBuildSizesKHR)vkGetDeviceProcAddr(core->device, "vkGetAccelerationStructureBuildSizesKHR");
	bhrt_funcs.CmdBuildAccelerationStructuresKHR = (PFN_vkCmdBuildAccelerationStructuresKHR)vkGetDeviceProcAddr(core->device, "vkCmdBuildAccelerationStructuresKHR");
	bhrt_funcs.GetAccelerationStructureDeviceAddressKHR = (PFN_vkGetAccelerationStructureDeviceAddressKHR)vkGetDeviceProcAddr(core->device, "vkGetAccelerationStructureDeviceAddressKHR");
	bhrt_funcs.GetBufferDeviceAddressKHR = (PFN_vkGetBufferDeviceAddressKHR)vkGetDeviceProcAddr(core->device, "vkGetBufferDeviceAddress");
	if (!bhrt_funcs.GetBufferDeviceAddressKHR)
		bhrt_funcs.GetBufferDeviceAddressKHR = (PFN_vkGetBufferDeviceAddressKHR)vkGetDeviceProcAddr(core->device, "vkGetBufferDeviceAddressKHR");
}

// ============================================================================
// Opaque Struct (must be defined before helpers that use it)
// ============================================================================

struct BarnesHutRT
{
	VulkanCore *core;
	bool supported;

	// One-time resources
	VkDescriptorSetLayout desc_set_layout;
	VkPipelineLayout pipeline_layout;
	VkPipeline force_pipeline;

	// Per-session resources
	bool session_active;
	uint32_t particle_count;
	float theta;
	float G;

	// Positions + masses buffer (uploaded per build)
	VkBuffer pos_buf;
	VkDeviceMemory pos_mem;
	VkDeviceSize pos_capacity;

	// Force output buffer (for reading back or binding)
	VkBuffer force_buf;
	VkDeviceMemory force_mem;
	VkBuffer force_staging_buf;
	VkDeviceMemory force_staging_mem;

	// Octree mesh buffers (reallocated each build if needed)
	VkBuffer vertex_buf;
	VkDeviceMemory vertex_mem;
	VkDeviceSize vertex_capacity;
	VkBuffer index_buf;
	VkDeviceMemory index_mem;
	VkDeviceSize index_capacity;
	VkBuffer node_buf;
	VkDeviceMemory node_mem;
	VkDeviceSize node_capacity;
	uint32_t num_prims;

	// BLAS
	VkBuffer blas_buf;
	VkDeviceMemory blas_mem;
	VkAccelerationStructureKHR blas;

	// TLAS
	VkBuffer tlas_buf;
	VkDeviceMemory tlas_mem;
	VkAccelerationStructureKHR tlas;
	VkBuffer instance_buf;
	VkDeviceMemory instance_mem;

	// Scratch buffer for AS builds
	VkBuffer scratch_buf;
	VkDeviceMemory scratch_mem;
	VkDeviceSize scratch_capacity;

	// Descriptor pool + set
	VkDescriptorPool desc_pool;
	VkDescriptorSet desc_set;
};

// ============================================================================
// Internal Helpers
// ============================================================================

static uint64_t bhrt_get_buffer_address(BarnesHutRT *bh, VkBuffer buf)
{
	VkBufferDeviceAddressInfo addrInfo = {.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = buf};
	return bhrt_funcs.GetBufferDeviceAddressKHR(bh->core->device, &addrInfo);
}

static uint64_t bhrt_get_as_address(BarnesHutRT *bh, VkAccelerationStructureKHR as)
{
	VkAccelerationStructureDeviceAddressInfoKHR addrInfo = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR, .accelerationStructure = as};
	return bhrt_funcs.GetAccelerationStructureDeviceAddressKHR(bh->core->device, &addrInfo);
}

// ============================================================================
// Buffer helpers (local — don't depend on RTBase)
// ============================================================================

static void bhrt_create_buffer(BarnesHutRT *bh, VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer *buf, VkDeviceMemory *mem)
{
	VkBufferCreateInfo bufInfo = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size, .usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
	VK_CHECK(vkCreateBuffer(bh->core->device, &bufInfo, NULL, buf), "Failed to create BHRT buffer");

	VkMemoryRequirements memReqs;
	vkGetBufferMemoryRequirements(bh->core->device, *buf, &memReqs);

	VkPhysicalDeviceMemoryProperties memProps;
	vkGetPhysicalDeviceMemoryProperties(bh->core->physicalDevice, &memProps);

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
		exit_with_error("BHRT: failed to find suitable memory type");

	VkMemoryAllocateFlagsInfo allocFlags = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO, .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT};
	VkMemoryAllocateInfo allocInfo = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .pNext = &allocFlags, .allocationSize = memReqs.size, .memoryTypeIndex = memTypeIndex};
	VK_CHECK(vkAllocateMemory(bh->core->device, &allocInfo, NULL, mem), "Failed to allocate BHRT buffer memory");
	VK_CHECK(vkBindBufferMemory(bh->core->device, *buf, *mem, 0), "Failed to bind BHRT buffer memory");
}

static void bhrt_create_device_buffer(BarnesHutRT *bh, VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer *buf, VkDeviceMemory *mem)
{
	VkBufferCreateInfo bufInfo = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size, .usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
	VK_CHECK(vkCreateBuffer(bh->core->device, &bufInfo, NULL, buf), "Failed to create BHRT device buffer");

	VkMemoryRequirements memReqs;
	vkGetBufferMemoryRequirements(bh->core->device, *buf, &memReqs);

	VkPhysicalDeviceMemoryProperties memProps;
	vkGetPhysicalDeviceMemoryProperties(bh->core->physicalDevice, &memProps);

	uint32_t memTypeIndex = UINT32_MAX;
	for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
		if ((memReqs.memoryTypeBits & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
			memTypeIndex = i;
			break;
		}
	}
	if (memTypeIndex == UINT32_MAX)
		exit_with_error("BHRT: failed to find device-local memory type");

	VkMemoryAllocateFlagsInfo allocFlags = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO, .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT};
	VkMemoryAllocateInfo allocInfo = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .pNext = &allocFlags, .allocationSize = memReqs.size, .memoryTypeIndex = memTypeIndex};
	VK_CHECK(vkAllocateMemory(bh->core->device, &allocInfo, NULL, mem), "Failed to allocate BHRT device buffer memory");
	VK_CHECK(vkBindBufferMemory(bh->core->device, *buf, *mem, 0), "Failed to bind BHRT device buffer memory");
}

static void bhrt_staging_upload(BarnesHutRT *bh, VkBuffer dst, const void *data, VkDeviceSize size)
{
	VkBuffer staging;
	VkDeviceMemory stagingMem;
	bhrt_create_buffer(bh, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &staging, &stagingMem);
	void *mapped;
	VK_CHECK(vkMapMemory(bh->core->device, stagingMem, 0, size, 0, &mapped), "Failed to map BHRT staging upload");
	memcpy(mapped, data, size);
	vkUnmapMemory(bh->core->device, stagingMem);

	VkCommandPoolCreateInfo poolInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, .queueFamilyIndex = (uint32_t)bh->core->graphicsQueueFamily};
	VkCommandPool cmdPool;
	VK_CHECK(vkCreateCommandPool(bh->core->device, &poolInfo, NULL, &cmdPool), "Failed to create BHRT staging upload pool");
	VkCommandBufferAllocateInfo cmdInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = cmdPool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
	VkCommandBuffer cmd;
	VK_CHECK(vkAllocateCommandBuffers(bh->core->device, &cmdInfo, &cmd), "Failed to allocate BHRT staging upload cmd");
	VK_CHECK(vkBeginCommandBuffer(cmd, &VK_CMD_BEGIN_INFO_ONETIME), "Failed to begin BHRT staging upload cmd");
	VkBufferCopy copyRegion = {.size = size};
	vkCmdCopyBuffer(cmd, staging, dst, 1, &copyRegion);
	VK_CHECK(vkEndCommandBuffer(cmd), "Failed to end BHRT staging upload cmd");
	VkSubmitInfo submitInfo = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
	pthread_mutex_lock(&bh->core->graphicsQueueMutex);
	VK_CHECK(vkQueueSubmit(bh->core->graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE), "Failed to submit BHRT staging upload");
	vkQueueWaitIdle(bh->core->graphicsQueue);
	pthread_mutex_unlock(&bh->core->graphicsQueueMutex);
	vkDestroyCommandPool(bh->core->device, cmdPool, NULL);
	VK_DESTROY_BUFFER(bh->core->device, staging, stagingMem);
}

static void bhrt_destroy_as_resources(BarnesHutRT *bh)
{
	VK_DESTROY_BUFFER(bh->core->device, bh->blas_buf, bh->blas_mem);
	VK_DESTROY_BUFFER(bh->core->device, bh->tlas_buf, bh->tlas_mem);
	VK_DESTROY_BUFFER(bh->core->device, bh->instance_buf, bh->instance_mem);
	VK_DESTROY_BUFFER(bh->core->device, bh->scratch_buf, bh->scratch_mem);
	bh->scratch_capacity = 0;

	if (bh->blas != VK_NULL_HANDLE) {
		bhrt_funcs.DestroyAccelerationStructureKHR(bh->core->device, bh->blas, NULL);
		bh->blas = VK_NULL_HANDLE;
	}
	if (bh->tlas != VK_NULL_HANDLE) {
		bhrt_funcs.DestroyAccelerationStructureKHR(bh->core->device, bh->tlas, NULL);
		bh->tlas = VK_NULL_HANDLE;
	}
}

// ============================================================================
// Public API — One-time Lifecycle
// ============================================================================

BarnesHutRT *bhrt_create(VulkanCore *core)
{
	BarnesHutRT *bh = (BarnesHutRT *)calloc(1, sizeof(BarnesHutRT));
	if (!bh)
		return NULL;
	bh->core = core;
	bh->supported = true;

	bhrt_load_rt_functions(core);

	// Descriptor Set Layout — matching bh_force.comp bindings
	//
	// Binding  Type                          Count  Stage
	// ------  ----------------------------  -----  ------
	//   0     STORAGE_BUFFER (NodeBuffer)     1    COMPUTE
	//   1     STORAGE_BUFFER (ForceBuffer)    1    COMPUTE
	//   2     STORAGE_BUFFER (PointBuffer)    1    COMPUTE
	//   3     STORAGE_BUFFER (BhNodeBuffer)   1    COMPUTE
	//   4     ACCELERATION_STRUCTURE (TLAS)   1    COMPUTE
	//
	// Push constants: 16 bytes = 2 uints + 2 floats
	VkDescriptorSetLayoutBinding bindings[5] = {
		{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}, {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}, {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}, {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}, {4, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
	};
	VkDescriptorSetLayoutCreateInfo layoutInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 5, .pBindings = bindings};
	VK_CHECK(vkCreateDescriptorSetLayout(core->device, &layoutInfo, NULL, &bh->desc_set_layout), "Failed to create BHRT descriptor set layout");

	VkPushConstantRange pcRange = {.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0, .size = 16};
	VkPipelineLayoutCreateInfo pipelineLayoutInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1, .pSetLayouts = &bh->desc_set_layout, .pushConstantRangeCount = 1, .pPushConstantRanges = &pcRange};
	VK_CHECK(vkCreatePipelineLayout(core->device, &pipelineLayoutInfo, NULL, &bh->pipeline_layout), "Failed to create BHRT pipeline layout");

	VkShaderModule forceModule = VK_NULL_HANDLE;
	VK_CHECK(create_shader_module(core->device, BHRT_FORCE_COMP_SHADER_PATH, &forceModule), "Failed to create BHRT force shader module");
	VkPipelineShaderStageCreateInfo forceStage = VK_SHADER_STAGE_COMP(forceModule);
	VkComputePipelineCreateInfo pipeInfo = {.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, .stage = forceStage, .layout = bh->pipeline_layout};
	VK_CHECK(vkCreateComputePipelines(core->device, VK_NULL_HANDLE, 1, &pipeInfo, NULL, &bh->force_pipeline), "Failed to create BHRT force pipeline");
	vkDestroyShaderModule(core->device, forceModule, NULL);

	printf("[BHRT] Initialized successfully\n");
	return bh;
}

void bhrt_destroy(BarnesHutRT *bh)
{
	if (!bh)
		return;

	bhrt_session_cleanup(bh);

	if (bh->force_pipeline != VK_NULL_HANDLE)
		vkDestroyPipeline(bh->core->device, bh->force_pipeline, NULL);
	if (bh->pipeline_layout != VK_NULL_HANDLE)
		vkDestroyPipelineLayout(bh->core->device, bh->pipeline_layout, NULL);
	if (bh->desc_set_layout != VK_NULL_HANDLE)
		vkDestroyDescriptorSetLayout(bh->core->device, bh->desc_set_layout, NULL);

	VK_DESTROY_BUFFER(bh->core->device, bh->pos_buf, bh->pos_mem);
	VK_DESTROY_BUFFER(bh->core->device, bh->force_buf, bh->force_mem);
	VK_DESTROY_BUFFER(bh->core->device, bh->force_staging_buf, bh->force_staging_mem);
	VK_DESTROY_BUFFER(bh->core->device, bh->vertex_buf, bh->vertex_mem);
	VK_DESTROY_BUFFER(bh->core->device, bh->index_buf, bh->index_mem);
	VK_DESTROY_BUFFER(bh->core->device, bh->node_buf, bh->node_mem);

	bhrt_destroy_as_resources(bh);

	free(bh);
	printf("[BHRT] Destroyed\n");
}

bool bhrt_is_supported(BarnesHutRT *bh)
{
	return bh && bh->supported;
}

// ============================================================================
// Public API — Session Lifecycle
// ============================================================================

bool bhrt_session_init(BarnesHutRT *bh, const float *positions, const float *masses, uint32_t particle_count, float theta, float G)
{
	if (!bh || !bh->supported)
		return false;

	bhrt_session_cleanup(bh);

	bh->particle_count = particle_count;
	bh->theta = theta;
	bh->G = G;
	bh->num_prims = 0;

	// Positions + masses buffer (vec4 per particle)
	VkDeviceSize posSize = sizeof(float) * 4 * particle_count;
	bhrt_create_device_buffer(bh, posSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, &bh->pos_buf, &bh->pos_mem);
	bh->pos_capacity = posSize;

	// Upload initial positions
	{
		float *posData = (float *)malloc(posSize);
		for (uint32_t i = 0; i < particle_count; i++) {
			posData[i * 4 + 0] = positions[i * 3 + 0];
			posData[i * 4 + 1] = positions[i * 3 + 1];
			posData[i * 4 + 2] = (particle_count > 2) ? positions[i * 3 + 2] : 0.0f;
			posData[i * 4 + 3] = masses ? masses[i] : 1.0f;
		}
		bhrt_staging_upload(bh, bh->pos_buf, posData, posSize);
		free(posData);
	}

	// Force output buffer (vec4 per particle)
	VkDeviceSize forceSize = sizeof(float) * 4 * particle_count;
	bhrt_create_device_buffer(bh, forceSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &bh->force_buf, &bh->force_mem);

	// Force staging buffer (for CPU readback)
	bhrt_create_buffer(bh, forceSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &bh->force_staging_buf, &bh->force_staging_mem);

	// Create descriptor pool + set (shared across all dispatches)
	{
		VkDescriptorPoolSize poolSizes[] = {
			{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4},
			{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1},
		};
		VkDescriptorPoolCreateInfo dpInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 1, .poolSizeCount = 2, .pPoolSizes = poolSizes};
		VK_CHECK(vkCreateDescriptorPool(bh->core->device, &dpInfo, NULL, &bh->desc_pool), "Failed to create BHRT descriptor pool");
		VkDescriptorSetAllocateInfo setInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = bh->desc_pool, .descriptorSetCount = 1, .pSetLayouts = &bh->desc_set_layout};
		VK_CHECK(vkAllocateDescriptorSets(bh->core->device, &setInfo, &bh->desc_set), "Failed to allocate BHRT descriptor set");
	}

	// Write persistent descriptor bindings (0, 1, 2 — pos, force, points use same buf)
	{
		VkDescriptorBufferInfo posInfo = {bh->pos_buf, 0, VK_WHOLE_SIZE};
		VkDescriptorBufferInfo forceInfo = {bh->force_buf, 0, VK_WHOLE_SIZE};
		VkDescriptorBufferInfo pointInfo = {bh->pos_buf, 0, VK_WHOLE_SIZE}; // same buf as pos (w=mass)

		VkWriteDescriptorSet writes[3] = {
			VK_WRITE_DESC_BUFFER(bh->desc_set, 0, &posInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
			VK_WRITE_DESC_BUFFER(bh->desc_set, 1, &forceInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
			VK_WRITE_DESC_BUFFER(bh->desc_set, 2, &pointInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
		};
		vkUpdateDescriptorSets(bh->core->device, 3, writes, 0, NULL);
	}

	bh->session_active = true;
	printf("[BHRT] Session initialized: %u particles, theta=%.3f, G=%.3f\n", particle_count, theta, G);
	return true;
}

void bhrt_session_cleanup(BarnesHutRT *bh)
{
	if (!bh || !bh->session_active)
		return;

	bhrt_destroy_as_resources(bh);

	if (bh->desc_pool != VK_NULL_HANDLE) {
		vkDestroyDescriptorPool(bh->core->device, bh->desc_pool, NULL);
		bh->desc_pool = VK_NULL_HANDLE;
		bh->desc_set = VK_NULL_HANDLE;
	}

	VK_DESTROY_BUFFER(bh->core->device, bh->pos_buf, bh->pos_mem);
	bh->pos_capacity = 0;
	VK_DESTROY_BUFFER(bh->core->device, bh->force_buf, bh->force_mem);
	VK_DESTROY_BUFFER(bh->core->device, bh->force_staging_buf, bh->force_staging_mem);
	VK_DESTROY_BUFFER(bh->core->device, bh->vertex_buf, bh->vertex_mem);
	bh->vertex_capacity = 0;
	VK_DESTROY_BUFFER(bh->core->device, bh->index_buf, bh->index_mem);
	bh->index_capacity = 0;
	VK_DESTROY_BUFFER(bh->core->device, bh->node_buf, bh->node_mem);
	bh->node_capacity = 0;

	bh->session_active = false;
	bh->particle_count = 0;
	bh->num_prims = 0;
}

// ============================================================================
// Public API — Build Octree + BLAS/TLAS
// ============================================================================

void bhrt_build(BarnesHutRT *bh, const float *positions, VkCommandBuffer cmd)
{
	if (!bh || !bh->session_active || !cmd)
		return;

	uint32_t pc = bh->particle_count;

	// ---- Step 1: Build octree on CPU ----
	// Find bounding box to determine grid size
	float min_x = positions[0], max_x = positions[0];
	float min_y = positions[1], max_y = positions[1];
	float min_z = positions[2], max_z = positions[2];
	for (uint32_t i = 1; i < pc; i++) {
		float x = positions[i * 3 + 0], y = positions[i * 3 + 1], z = positions[i * 3 + 2];
		if (x < min_x)
			min_x = x;
		if (x > max_x)
			max_x = x;
		if (y < min_y)
			min_y = y;
		if (y > max_y)
			max_y = y;
		if (z < min_z)
			min_z = z;
		if (z > max_z)
			max_z = z;
	}
	float extent_x = max_x - min_x;
	float extent_y = max_y - min_y;
	float extent_z = max_z - min_z;
	float max_extent = extent_x;
	if (extent_y > max_extent)
		max_extent = extent_y;
	if (extent_z > max_extent)
		max_extent = extent_z;
	if (max_extent < 1e-6f)
		max_extent = 1.0f;
	float grid_size = max_extent * 2.0f;

	// Create points array with Z-order sort
	bh_point_t *points = (bh_point_t *)malloc(sizeof(bh_point_t) * pc);
	for (uint32_t i = 0; i < pc; i++) {
		points[i].pos.x = positions[i * 3 + 0];
		points[i].pos.y = positions[i * 3 + 1];
		points[i].pos.z = positions[i * 3 + 2];
		points[i].mass = 1.0f;
		points[i].id = (int)i;
	}

	bh_points_sort_zorder(points, (int)pc);

	bh_tree_t *tree = bh_tree_create(grid_size, bh->theta);
	bh_tree_build(tree, points, (int)pc);
	bh_tree_compute_com(tree);

	// ---- Step 2: DFS linearization ----
	bh_dfs_output_t dfs = bh_tree_to_dfs_array(tree);
	bh_tree_install_auto_ropes(tree, dfs.device_nodes, dfs.num_nodes);

	bh->num_prims = (uint32_t)dfs.num_nodes;

	// ---- Step 3: Upload octree mesh to GPU ----
	VkDeviceSize vertSize = sizeof(float) * 9 * dfs.num_nodes;
	VkDeviceSize idxSize = sizeof(uint32_t) * 3 * dfs.num_nodes;
	VkDeviceSize nodeSize = sizeof(bh_device_node_t) * dfs.num_nodes;

	// Vertex buffer
	if (vertSize > bh->vertex_capacity) {
		VK_DESTROY_BUFFER(bh->core->device, bh->vertex_buf, bh->vertex_mem);
		bhrt_create_device_buffer(bh, vertSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, &bh->vertex_buf, &bh->vertex_mem);
		bh->vertex_capacity = vertSize;
	}
	bhrt_staging_upload(bh, bh->vertex_buf, dfs.vertices, vertSize);

	// Index buffer
	if (idxSize > bh->index_capacity) {
		VK_DESTROY_BUFFER(bh->core->device, bh->index_buf, bh->index_mem);
		bhrt_create_device_buffer(bh, idxSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, &bh->index_buf, &bh->index_mem);
		bh->index_capacity = idxSize;
	}
	bhrt_staging_upload(bh, bh->index_buf, dfs.indices, idxSize);

	// Node buffer (BhNode[])
	if (nodeSize > bh->node_capacity) {
		VK_DESTROY_BUFFER(bh->core->device, bh->node_buf, bh->node_mem);
		bhrt_create_device_buffer(bh, nodeSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, &bh->node_buf, &bh->node_mem);
		bh->node_capacity = nodeSize;
	}
	bhrt_staging_upload(bh, bh->node_buf, dfs.device_nodes, nodeSize);

	bh_dfs_output_free(&dfs);
	bh_tree_destroy(tree);
	free(points);

	// ---- Step 4: Destroy old BLAS/TLAS ----
	bhrt_destroy_as_resources(bh);

	// ---- Step 5: Build triangle-mesh BLAS ----
	uint64_t vertAddr = bhrt_get_buffer_address(bh, bh->vertex_buf);
	uint64_t idxAddr = bhrt_get_buffer_address(bh, bh->index_buf);

	VkAccelerationStructureGeometryKHR blasGeometry = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
		.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
		.flags = VK_GEOMETRY_OPAQUE_BIT_KHR,
		.geometry.triangles =
			{
				.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
				.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
				.vertexData.deviceAddress = vertAddr,
				.vertexStride = sizeof(float) * 3,
				.maxVertex = (uint32_t)(dfs.num_nodes * 3 - 1),
				.indexType = VK_INDEX_TYPE_UINT32,
				.indexData.deviceAddress = idxAddr,
			},
	};

	VkAccelerationStructureBuildGeometryInfoKHR blasBuildInfo = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
		.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
		.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
		.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
		.geometryCount = 1,
		.pGeometries = &blasGeometry,
	};

	uint32_t maxPrims = bh->num_prims;
	VkAccelerationStructureBuildSizesInfoKHR blasSizeInfo = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
	bhrt_funcs.GetAccelerationStructureBuildSizesKHR(bh->core->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &blasBuildInfo, &maxPrims, &blasSizeInfo);

	bhrt_create_device_buffer(bh, blasSizeInfo.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, &bh->blas_buf, &bh->blas_mem);

	VkAccelerationStructureCreateInfoKHR blasCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
		.buffer = bh->blas_buf,
		.offset = 0,
		.size = blasSizeInfo.accelerationStructureSize,
		.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
	};
	VK_CHECK(bhrt_funcs.CreateAccelerationStructureKHR(bh->core->device, &blasCreateInfo, NULL, &bh->blas), "Failed to create BHRT BLAS");

	// ---- Step 6: Build TLAS (single instance) ----
	uint64_t blasAddr = bhrt_get_as_address(bh, bh->blas);
	VkAccelerationStructureInstanceKHR instance;
	memset(&instance, 0, sizeof(instance));
	instance.instanceCustomIndex = 0;
	instance.mask = 0xFF;
	instance.instanceShaderBindingTableRecordOffset = 0;
	instance.flags = VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;
	instance.accelerationStructureReference = blasAddr;
	// Identity transform
	float *m = (float *)instance.transform.matrix[0];
	m[0] = 1.0f;
	m[1] = 0.0f;
	m[2] = 0.0f;
	m[3] = 0.0f;
	m[4] = 0.0f;
	m[5] = 1.0f;
	m[6] = 0.0f;
	m[7] = 0.0f;
	m[8] = 0.0f;
	m[9] = 0.0f;
	m[10] = 1.0f;
	m[11] = 0.0f;

	VkDeviceSize instSize = sizeof(VkAccelerationStructureInstanceKHR);
	bhrt_create_device_buffer(bh, instSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, &bh->instance_buf, &bh->instance_mem);
	bhrt_staging_upload(bh, bh->instance_buf, &instance, instSize);

	VkAccelerationStructureGeometryKHR tlasGeometry = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
		.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
		.flags = VK_GEOMETRY_OPAQUE_BIT_KHR,
		.geometry.instances =
			{
				.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
				.data.deviceAddress = bhrt_get_buffer_address(bh, bh->instance_buf),
			},
	};

	VkAccelerationStructureBuildGeometryInfoKHR tlasBuildInfo = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
		.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
		.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
		.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
		.geometryCount = 1,
		.pGeometries = &tlasGeometry,
	};

	uint32_t maxInstances = 1;
	VkAccelerationStructureBuildSizesInfoKHR tlasSizeInfo = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
	bhrt_funcs.GetAccelerationStructureBuildSizesKHR(bh->core->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &tlasBuildInfo, &maxInstances, &tlasSizeInfo);

	bhrt_create_device_buffer(bh, tlasSizeInfo.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, &bh->tlas_buf, &bh->tlas_mem);

	VkAccelerationStructureCreateInfoKHR tlasCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
		.buffer = bh->tlas_buf,
		.offset = 0,
		.size = tlasSizeInfo.accelerationStructureSize,
		.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
	};
	VK_CHECK(bhrt_funcs.CreateAccelerationStructureKHR(bh->core->device, &tlasCreateInfo, NULL, &bh->tlas), "Failed to create BHRT TLAS");

	// ---- Step 7: Create/use scratch buffer (max size) ----
	VkDeviceSize scratchSize = blasSizeInfo.buildScratchSize > tlasSizeInfo.buildScratchSize ? blasSizeInfo.buildScratchSize : tlasSizeInfo.buildScratchSize;
	if (scratchSize > bh->scratch_capacity) {
		VK_DESTROY_BUFFER(bh->core->device, bh->scratch_buf, bh->scratch_mem);
		bhrt_create_device_buffer(bh, scratchSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, &bh->scratch_buf, &bh->scratch_mem);
		bh->scratch_capacity = scratchSize;
	}
	uint64_t scratchAddr = bhrt_get_buffer_address(bh, bh->scratch_buf);

	// ---- Step 8: Update positions on GPU ----
	// Upload updated positions to pos_buf (w=mass stays from session init)
	{
		float *posData = (float *)malloc(sizeof(float) * 4 * pc);
		for (uint32_t i = 0; i < pc; i++) {
			posData[i * 4 + 0] = positions[i * 3 + 0];
			posData[i * 4 + 1] = positions[i * 3 + 1];
			posData[i * 4 + 2] = positions[i * 3 + 2];
			posData[i * 4 + 3] = 1.0f;
		}
		// Use a command to upload to device-local
		VkBuffer staging;
		VkDeviceMemory stagingMem;
		bhrt_create_buffer(bh, sizeof(float) * 4 * pc, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &staging, &stagingMem);
		void *mapped;
		vkMapMemory(bh->core->device, stagingMem, 0, sizeof(float) * 4 * pc, 0, &mapped);
		memcpy(mapped, posData, sizeof(float) * 4 * pc);
		vkUnmapMemory(bh->core->device, stagingMem);
		VkBufferCopy copyRegion = {.size = sizeof(float) * 4 * pc};
		vkCmdCopyBuffer(cmd, staging, bh->pos_buf, 1, &copyRegion);
		VK_DESTROY_BUFFER(bh->core->device, staging, stagingMem);
		free(posData);
	}

	VkMemoryBarrier posBarrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR};
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1, &posBarrier, 0, NULL, 0, NULL);

	// ---- Step 9: Record BLAS build ----
	blasBuildInfo.dstAccelerationStructure = bh->blas;
	blasBuildInfo.scratchData.deviceAddress = scratchAddr;
	VkAccelerationStructureBuildRangeInfoKHR blasRangeInfo = {.primitiveCount = bh->num_prims, .primitiveOffset = 0, .firstVertex = 0, .transformOffset = 0};
	const VkAccelerationStructureBuildRangeInfoKHR *pBlasRange = &blasRangeInfo;
	bhrt_funcs.CmdBuildAccelerationStructuresKHR(cmd, 1, &blasBuildInfo, &pBlasRange);

	// BLAS barrier
	VkMemoryBarrier blasBarrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR, .dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR};
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1, &blasBarrier, 0, NULL, 0, NULL);

	// ---- Step 10: Record TLAS build ----
	tlasBuildInfo.dstAccelerationStructure = bh->tlas;
	tlasBuildInfo.scratchData.deviceAddress = scratchAddr;
	{
		// Re-fetch instance addr (buffer may have changed after upload)
		tlasGeometry.geometry.instances.data.deviceAddress = bhrt_get_buffer_address(bh, bh->instance_buf);
		tlasBuildInfo.pGeometries = &tlasGeometry;
		VkAccelerationStructureBuildRangeInfoKHR tlasRangeInfo = {.primitiveCount = 1, .primitiveOffset = 0, .firstVertex = 0, .transformOffset = 0};
		const VkAccelerationStructureBuildRangeInfoKHR *pTlasRange = &tlasRangeInfo;
		bhrt_funcs.CmdBuildAccelerationStructuresKHR(cmd, 1, &tlasBuildInfo, &pTlasRange);
	}

	// ---- Step 11: Update TLAS descriptor binding ----
	VkAccelerationStructureKHR tlas = bh->tlas;
	VkWriteDescriptorSetAccelerationStructureKHR asDescInfo = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
		.accelerationStructureCount = 1,
		.pAccelerationStructures = &tlas,
	};
	VkWriteDescriptorSet tlasWrite = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.pNext = &asDescInfo,
		.dstSet = bh->desc_set,
		.dstBinding = 4,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
	};

	// Update node buffer descriptor (binding 3) in case it was recreated
	VkDescriptorBufferInfo nodeInfo = {bh->node_buf, 0, VK_WHOLE_SIZE};
	VkWriteDescriptorSet nodeWrite = VK_WRITE_DESC_BUFFER(bh->desc_set, 3, &nodeInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

	VkWriteDescriptorSet writes[2] = {tlasWrite, nodeWrite};
	vkUpdateDescriptorSets(bh->core->device, 2, writes, 0, NULL);
}

// ============================================================================
// Public API — Record Compute Dispatch
// ============================================================================

void bhrt_record_dispatch(BarnesHutRT *bh, VkCommandBuffer cmd)
{
	if (!bh || !bh->session_active || !cmd || bh->num_prims == 0)
		return;

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, bh->force_pipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, bh->pipeline_layout, 0, 1, &bh->desc_set, 0, NULL);

	struct
	{
		uint32_t particle_count;
		uint32_t num_prims;
		float threshold;
		float G;
	} pc = {bh->particle_count, bh->num_prims, bh->theta, bh->G};

	vkCmdPushConstants(cmd, bh->pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 16, &pc);

	uint32_t groupCount = (bh->particle_count + BHRT_WORKGROUP_SIZE - 1) / BHRT_WORKGROUP_SIZE;
	vkCmdDispatch(cmd, groupCount, 1, 1);
}

// ============================================================================
// Public API — Accessors
// ============================================================================

VkBuffer bhrt_force_buffer(BarnesHutRT *bh)
{
	return bh ? bh->force_buf : VK_NULL_HANDLE;
}

void bhrt_readback_forces(BarnesHutRT *bh, float *out_forces)
{
	if (!bh || !bh->session_active || !out_forces)
		return;

	VkDeviceSize forceSize = sizeof(float) * 4 * bh->particle_count;

	// Use a one-time command buffer to copy force -> staging
	VkCommandPoolCreateInfo poolInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, .queueFamilyIndex = (uint32_t)bh->core->graphicsQueueFamily};
	VkCommandPool cmdPool;
	VK_CHECK(vkCreateCommandPool(bh->core->device, &poolInfo, NULL, &cmdPool), "Failed to create BHRT readback pool");
	VkCommandBufferAllocateInfo cmdInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = cmdPool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
	VkCommandBuffer cmd;
	VK_CHECK(vkAllocateCommandBuffers(bh->core->device, &cmdInfo, &cmd), "Failed to allocate BHRT readback cmd");
	VK_CHECK(vkBeginCommandBuffer(cmd, &VK_CMD_BEGIN_INFO_ONETIME), "Failed to begin BHRT readback cmd");

	VkMemoryBarrier forceBarrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT};
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &forceBarrier, 0, NULL, 0, NULL);

	VkBufferCopy copyRegion = {.size = forceSize};
	vkCmdCopyBuffer(cmd, bh->force_buf, bh->force_staging_buf, 1, &copyRegion);

	VK_CHECK(vkEndCommandBuffer(cmd), "Failed to end BHRT readback cmd");

	VkSubmitInfo submitInfo = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
	pthread_mutex_lock(&bh->core->graphicsQueueMutex);
	VK_CHECK(vkQueueSubmit(bh->core->graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE), "Failed to submit BHRT readback");
	vkQueueWaitIdle(bh->core->graphicsQueue);
	pthread_mutex_unlock(&bh->core->graphicsQueueMutex);

	vkDestroyCommandPool(bh->core->device, cmdPool, NULL);

	void *mapped;
	VK_CHECK(vkMapMemory(bh->core->device, bh->force_staging_mem, 0, forceSize, 0, &mapped), "Failed to map BHRT readback");
	memcpy(out_forces, mapped, forceSize);
	vkUnmapMemory(bh->core->device, bh->force_staging_mem);
}
