// =============================================================================
// YHRT — Yu Hu Ray-Traced Force-Directed Graph Layout (GPU)
// =============================================================================
//
// Implements a GPU-accelerated force-directed graph layout using Vulkan compute
// shaders and ray-tracing acceleration structures. The algorithm follows the
// Yu Hu (YHu) model (igraph's layout_yhu_3d), where each iteration consists of
// four compute passes plus a TLAS rebuild:
//
//   1. REPULSION   (yh_repulsion.comp)  — all-pairs via BVH ray queries
//   2. ATTRACTION  (yh_attraction.comp) — edge-based spring forces
//   3. UPDATE      (yh_update.comp)     — position integration + Fnorm reduction
//   4. INSTANCES   (yh_update_instances.comp) — sync transforms for BVH rebuild
//   5. TLAS REBUILD (Vulkan API)        — update acceleration structure in-place
//
// Acceleration Structure Hierarchy (the key trick):
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Instead of using ray tracing for rendering, we repurpose the Vulkan
// acceleration structures (BLAS + TLAS) as a spatial index for the repulsion
// pass. Internally, both BLAS and TLAS are organized as BVHs (Bounding Volume
// Hierarchies). Each ray query traverses the TLAS BVH in O(log N + k) time
// where k is the number of nodes within radius R, reducing the total repulsion
// work from O(N²) brute-force to O(N · k_avg).
//
//   TLAS (Top-Level Acceleration Structure)
//   ========================================
//   Contains N instances, one per graph node. Each instance references the same
//   BLAS but with a unique 3x4 transform matrix that translates it to the
//   node's current 3D position. The BLAS itself is a single AABB of half-extent R
//   centered at the origin — so after instancing, each node occupies an
//   axis-aligned cube of side 2R in world space.
//
//   BLAS (Bottom-Level Acceleration Structure)
//   ==========================================
//   Contains a single AABB primitive: [-R, -R, -R] to [+R, +R, +R].
//   This is a cube (axis-aligned bounding box) shared by ALL instances.
//   The BLAS is built ONCE at session init and never rebuilt — only the TLAS
//   instance transforms change per iteration.
//
//   Why this works for force-directed layout:
//   The repulsion shader (yh_repulsion.comp) fires a ray from each node and
//   traverses the TLAS. The BVH traversal prunes nodes whose AABBs
//   don't intersect the ray, so only nodes within distance ~R are considered.
//   This reduces the O(N^2) all-pairs problem to ~O(N * avg_neighbors_in_R),
//   where R = 5*K (5x average edge length).
//
//   Per-iteration data flow:
//
//     ┌─────────────┐     ┌──────────────┐     ┌───────────────┐
//     │ NodeBuffer   │────>│ Repulsion    │────>│ ForceBuffer   │
//     │ [pos, deg]   │     │ (ray query   │     │ [force.xyz,0] │
//     └─────────────┘     │  via TLAS)   │     └───────┬───────┘
//                         └──────────────┘             │
//                                                      v
//     ┌─────────────┐     ┌──────────────┐     ┌───────────────┐
//     │ EdgeBuffer   │────>│ Attraction   │────>│ ForceBuffer   │
//     │ [from,to,w]  │     │ (spring)     │     │ (accumulated) │
//     └─────────────┘     └──────────────┘     └───────┬───────┘
//                                                      │
//                                                      v
//     ┌─────────────┐     ┌──────────────┐     ┌───────────────┐
//     │ NodeBuffer   │<───>│ Update       │<───>│ FnormBuffer   │
//     │ (positions   │     │ (integrate + │     │ (global sum)  │
//     │  updated)    │     │  reduce)     │     └───────────────┘
//     └──────┬──────┘     └──────────────┘
//            │
//            v
//     ┌──────────────┐     ┌──────────────┐     ┌──────────────┐
//     │ InstanceBuffer│────>│ Update       │────>│ TLAS Rebuild │
//     │ (transforms)  │     │ Instances    │     │ (in-place)   │
//     └──────────────┘     └──────────────┘     └──────┬───────┘
//                                                      │
//                                                      v
//                                                ┌──────────────┐
//                                                │ Next iter:   │
//                                                │ Repulsion    │
//                                                │ reads new TLAS│
//                                                └──────────────┘
//
// Descriptor Set Layout (shared across all 5 pipelines):
//   binding 0: STORAGE_BUFFER  — NodeBuffer   (vec4[pos.xyz, degree])
//   binding 1: STORAGE_BUFFER  — ForceBuffer   (vec4[force.xyz, 0])
//   binding 2: STORAGE_BUFFER  — EdgeBuffer    (EdgeData[from, to, weight])
//   binding 3: TLAS            — Acceleration structure for ray queries
//   binding 4: STORAGE_BUFFER  — FnormBuffer   (float/double accumulator)
//   binding 5: STORAGE_BUFFER  — InstanceBuffer (vec4[4] per node, TLAS transforms)
//
// Push Constants (28 bytes):
//   [0..3]   float KP          — K^(1-p), repulsion scaling
//   [4..7]   float CRK         — C^((2-p)/3) / K, attraction scaling
//   [8..11]  float p           — repulsion power-law exponent (default 1.0)
//   [12..15] float step_size   — adaptive step size (cooling schedule)
//   [16..19] uint  vertex_count — number of graph nodes
//   [20..23] uint  edge_count   — number of graph edges
//   [24..27] float R           — BVH search radius = 5 * K
// =============================================================================

#include "vulkan/rt_layout.h"

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vulkan/buffers.h"
#include "vulkan/renderer.h"
#include "vulkan/utils.h"

#define YHRT_WORKGROUP_SIZE 256
#define YHRT_FNORM_READBACK_INTERVAL 1

// YHu constants matching igraph's layout_yhu_3d implementation.
// IGRAPH_YHU_C controls attraction/repulsion balance; IGRAPH_YHU_COOL is the
// cooling factor applied when Fnorm increases (step *= COOL) or decreases
// slowly (step *= 0.99/COOL to allow slight growth).
#define IGRAPH_YHU_C 0.2
#define IGRAPH_YHU_COOL 0.90

// ============================================================================
// RT Function Pointer Loading
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

static void yhrt_load_rt_functions(Renderer *r)
{
	if (rt_funcs.CreateAccelerationStructureKHR)
		return;
	rt_funcs.CreateAccelerationStructureKHR = (PFN_vkCreateAccelerationStructureKHR)vkGetDeviceProcAddr(r->core.device, "vkCreateAccelerationStructureKHR");
	rt_funcs.DestroyAccelerationStructureKHR = (PFN_vkDestroyAccelerationStructureKHR)vkGetDeviceProcAddr(r->core.device, "vkDestroyAccelerationStructureKHR");
	rt_funcs.GetAccelerationStructureBuildSizesKHR = (PFN_vkGetAccelerationStructureBuildSizesKHR)vkGetDeviceProcAddr(r->core.device, "vkGetAccelerationStructureBuildSizesKHR");
	rt_funcs.CmdBuildAccelerationStructuresKHR = (PFN_vkCmdBuildAccelerationStructuresKHR)vkGetDeviceProcAddr(r->core.device, "vkCmdBuildAccelerationStructuresKHR");
	rt_funcs.GetAccelerationStructureDeviceAddressKHR = (PFN_vkGetAccelerationStructureDeviceAddressKHR)vkGetDeviceProcAddr(r->core.device, "vkGetAccelerationStructureDeviceAddressKHR");
	// vkGetBufferDeviceAddress: KHR promoted to core in 1.2; loaders only register core name
	rt_funcs.GetBufferDeviceAddressKHR = (PFN_vkGetBufferDeviceAddressKHR)vkGetDeviceProcAddr(r->core.device, "vkGetBufferDeviceAddress");
	if (!rt_funcs.GetBufferDeviceAddressKHR)
		rt_funcs.GetBufferDeviceAddressKHR = (PFN_vkGetBufferDeviceAddressKHR)vkGetDeviceProcAddr(r->core.device, "vkGetBufferDeviceAddressKHR");
}

// ============================================================================
// Support Check
// ============================================================================

bool yhrt_check_support(VkPhysicalDevice device)
{
	uint32_t devExtCount = 0;
	vkEnumerateDeviceExtensionProperties(device, NULL, &devExtCount, NULL);
	VkExtensionProperties *devExts = malloc(sizeof(VkExtensionProperties) * devExtCount);
	vkEnumerateDeviceExtensionProperties(device, NULL, &devExtCount, devExts);

	printf("[YHRT] Checking %u device extensions for RT support...\n", devExtCount);

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
			fprintf(stderr, "[YHRT] MISSING extension: %s\n", required[i]);
			supported = false;
		} else {
			printf("[YHRT]   found: %s\n", required[i]);
		}
	}
	free(devExts);
	return supported;
}

// ============================================================================
// Helper: create buffer with device address
// ============================================================================

static void yhrt_create_buffer(Renderer *r, VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer *buf, VkDeviceMemory *mem)
{
	VkBufferCreateInfo bufInfo = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size, .usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
	VK_CHECK(vkCreateBuffer(r->core.device, &bufInfo, NULL, buf), "Failed to create YHRT buffer");

	VkMemoryRequirements memReqs;
	vkGetBufferMemoryRequirements(r->core.device, *buf, &memReqs);

	VkPhysicalDeviceMemoryProperties memProps;
	vkGetPhysicalDeviceMemoryProperties(r->core.physicalDevice, &memProps);

	uint32_t memTypeIndex = UINT32_MAX;
	for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
		if ((memReqs.memoryTypeBits & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) && (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
			memTypeIndex = i;
			break;
		}
	}
	if (memTypeIndex == UINT32_MAX) {
		// Try device-local
		for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
			if ((memReqs.memoryTypeBits & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
				memTypeIndex = i;
				break;
			}
		}
	}
	if (memTypeIndex == UINT32_MAX)
		exit_with_error("YHRT: failed to find suitable memory type");

	VkMemoryAllocateFlagsInfo allocFlags = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO, .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT};
	VkMemoryAllocateInfo allocInfo = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .pNext = &allocFlags, .allocationSize = memReqs.size, .memoryTypeIndex = memTypeIndex};
	VK_CHECK(vkAllocateMemory(r->core.device, &allocInfo, NULL, mem), "Failed to allocate YHRT buffer memory");
	VK_CHECK(vkBindBufferMemory(r->core.device, *buf, *mem, 0), "Failed to bind YHRT buffer memory");
}

static void yhrt_create_device_buffer(Renderer *r, VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer *buf, VkDeviceMemory *mem)
{
	VkBufferCreateInfo bufInfo = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size, .usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
	VK_CHECK(vkCreateBuffer(r->core.device, &bufInfo, NULL, buf), "Failed to create YHRT device buffer");

	VkMemoryRequirements memReqs;
	vkGetBufferMemoryRequirements(r->core.device, *buf, &memReqs);

	VkPhysicalDeviceMemoryProperties memProps;
	vkGetPhysicalDeviceMemoryProperties(r->core.physicalDevice, &memProps);

	uint32_t memTypeIndex = UINT32_MAX;
	for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
		if ((memReqs.memoryTypeBits & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
			memTypeIndex = i;
			break;
		}
	}
	if (memTypeIndex == UINT32_MAX)
		exit_with_error("YHRT: failed to find device-local memory type");

	VkMemoryAllocateFlagsInfo allocFlags = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO, .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT};
	VkMemoryAllocateInfo allocInfo = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .pNext = &allocFlags, .allocationSize = memReqs.size, .memoryTypeIndex = memTypeIndex};
	VK_CHECK(vkAllocateMemory(r->core.device, &allocInfo, NULL, mem), "Failed to allocate YHRT device buffer memory");
	VK_CHECK(vkBindBufferMemory(r->core.device, *buf, *mem, 0), "Failed to bind YHRT device buffer memory");
}

static uint64_t yhrt_get_buffer_address(Renderer *r, VkBuffer buf)
{
	VkBufferDeviceAddressInfo addrInfo = {.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = buf};
	return rt_funcs.GetBufferDeviceAddressKHR(r->core.device, &addrInfo);
}

static uint64_t yhrt_get_as_address(Renderer *r, VkAccelerationStructureKHR as)
{
	VkAccelerationStructureDeviceAddressInfoKHR addrInfo = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR, .accelerationStructure = as};
	return rt_funcs.GetAccelerationStructureDeviceAddressKHR(r->core.device, &addrInfo);
}

// ============================================================================
// One-Shot Helpers
// ============================================================================

#define YHRT_DISPATCH_COMPUTE(cmd, pipeline, layout, descSet, pc, count) \
	do { \
		vkCmdBindPipeline((cmd), VK_PIPELINE_BIND_POINT_COMPUTE, (pipeline)); \
		vkCmdBindDescriptorSets((cmd), VK_PIPELINE_BIND_POINT_COMPUTE, (layout), 0, 1, &(descSet), 0, NULL); \
		vkCmdPushConstants((cmd), (layout), VK_SHADER_STAGE_COMPUTE_BIT, 0, 28, &(pc)); \
		vkCmdDispatch((cmd), (count), 1, 1); \
	} while (0)

static void yhrt_staging_upload(Renderer *r, VkBuffer dst, const void *data, VkDeviceSize size, bool thread_safe)
{
	VkBuffer staging;
	VkDeviceMemory stagingMem;
	yhrt_create_buffer(r, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &staging, &stagingMem);
	void *mapped;
	VK_CHECK(vkMapMemory(r->core.device, stagingMem, 0, size, 0, &mapped), "Failed to map staging upload");
	memcpy(mapped, data, size);
	vkUnmapMemory(r->core.device, stagingMem);

	VkCommandPoolCreateInfo poolInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, .queueFamilyIndex = (uint32_t)r->core.graphicsQueueFamily};
	VkCommandPool cmdPool;
	VK_CHECK(vkCreateCommandPool(r->core.device, &poolInfo, NULL, &cmdPool), "Failed to create staging upload pool");
	VkCommandBufferAllocateInfo cmdInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = cmdPool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
	VkCommandBuffer cmd;
	VK_CHECK(vkAllocateCommandBuffers(r->core.device, &cmdInfo, &cmd), "Failed to allocate staging upload cmd");
	VK_CHECK(vkBeginCommandBuffer(cmd, &VK_CMD_BEGIN_INFO_ONETIME), "Failed to begin staging upload cmd");
	VkBufferCopy copyRegion = {.size = size};
	vkCmdCopyBuffer(cmd, staging, dst, 1, &copyRegion);
	VK_CHECK(vkEndCommandBuffer(cmd), "Failed to end staging upload cmd");
	VkSubmitInfo submitInfo = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
	if (thread_safe)
		pthread_mutex_lock(&r->core.graphicsQueueMutex);
	VK_CHECK(vkQueueSubmit(r->core.graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE), "Failed to submit staging upload");
	vkQueueWaitIdle(r->core.graphicsQueue);
	if (thread_safe)
		pthread_mutex_unlock(&r->core.graphicsQueueMutex);
	vkDestroyCommandPool(r->core.device, cmdPool, NULL);
	VK_DESTROY_BUFFER(r->core.device, staging, stagingMem);
}

static void yhrt_fill_device_buffer(Renderer *r, VkBuffer dst, VkDeviceSize size, bool thread_safe)
{
	VkCommandPoolCreateInfo poolInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, .queueFamilyIndex = (uint32_t)r->core.graphicsQueueFamily};
	VkCommandPool cmdPool;
	VK_CHECK(vkCreateCommandPool(r->core.device, &poolInfo, NULL, &cmdPool), "Failed to create fill buffer pool");
	VkCommandBufferAllocateInfo cmdInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = cmdPool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
	VkCommandBuffer cmd;
	VK_CHECK(vkAllocateCommandBuffers(r->core.device, &cmdInfo, &cmd), "Failed to allocate fill buffer cmd");
	VK_CHECK(vkBeginCommandBuffer(cmd, &VK_CMD_BEGIN_INFO_ONETIME), "Failed to begin fill buffer cmd");
	vkCmdFillBuffer(cmd, dst, 0, size, 0);
	VK_CHECK(vkEndCommandBuffer(cmd), "Failed to end fill buffer cmd");
	VkSubmitInfo submitInfo = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
	if (thread_safe)
		pthread_mutex_lock(&r->core.graphicsQueueMutex);
	VK_CHECK(vkQueueSubmit(r->core.graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE), "Failed to submit fill buffer");
	vkQueueWaitIdle(r->core.graphicsQueue);
	if (thread_safe)
		pthread_mutex_unlock(&r->core.graphicsQueueMutex);
	vkDestroyCommandPool(r->core.device, cmdPool, NULL);
}

// ============================================================================
// Session Cleanup
// ============================================================================

static void yhrt_cleanup_session_buffers(Renderer *r)
{
	VK_DESTROY_BUFFER(r->core.device, r->yhrt_node_buf, r->yhrt_node_mem);
	VK_DESTROY_BUFFER(r->core.device, r->yhrt_force_buf, r->yhrt_force_mem);
	VK_DESTROY_BUFFER(r->core.device, r->yhrt_edge_buf, r->yhrt_edge_mem);
	VK_DESTROY_BUFFER(r->core.device, r->yhrt_fnorm_buf, r->yhrt_fnorm_mem);
	VK_DESTROY_BUFFER(r->core.device, r->yhrt_staging_buf, r->yhrt_staging_mem);
	VK_DESTROY_BUFFER(r->core.device, r->yhrt_as_scratch_buf, r->yhrt_as_scratch_mem);
	VK_DESTROY_BUFFER(r->core.device, r->yhrt_blas_buf, r->yhrt_blas_mem);
	VK_DESTROY_BUFFER(r->core.device, r->yhrt_tlas_buf, r->yhrt_tlas_mem);
	VK_DESTROY_BUFFER(r->core.device, r->yhrt_instance_buf, r->yhrt_instance_mem);

	if (r->yhrt_blas != VK_NULL_HANDLE) {
		rt_funcs.DestroyAccelerationStructureKHR(r->core.device, r->yhrt_blas, NULL);
		r->yhrt_blas = VK_NULL_HANDLE;
	}
	if (r->yhrt_tlas != VK_NULL_HANDLE) {
		rt_funcs.DestroyAccelerationStructureKHR(r->core.device, r->yhrt_tlas, NULL);
		r->yhrt_tlas = VK_NULL_HANDLE;
	}
	if (r->yhrt_desc_pool != VK_NULL_HANDLE) {
		vkDestroyDescriptorPool(r->core.device, r->yhrt_desc_pool, NULL);
		r->yhrt_desc_pool = VK_NULL_HANDLE;
	}
	if (r->yhrt_dispatch_fence != VK_NULL_HANDLE) {
		vkDestroyFence(r->core.device, r->yhrt_dispatch_fence, NULL);
		r->yhrt_dispatch_fence = VK_NULL_HANDLE;
	}
	if (r->yhrt_cmd_pool != VK_NULL_HANDLE) {
		vkDestroyCommandPool(r->core.device, r->yhrt_cmd_pool, NULL);
		r->yhrt_cmd_pool = VK_NULL_HANDLE;
		r->yhrt_cmd_buf = VK_NULL_HANDLE;
	}
	VK_DESTROY_BUFFER(r->core.device, r->yhrt_node_staging_buf, r->yhrt_node_staging_mem);
}

// ============================================================================
// Pipeline Initialization (called once)
// ============================================================================

void yhrt_init_pipelines(Renderer *r)
{
	r->yhrt_supported = yhrt_check_support(r->core.physicalDevice);
	r->yhrt_active = false;
	r->yhrt_desc_set_layout = VK_NULL_HANDLE;
	r->yhrt_pipeline_layout = VK_NULL_HANDLE;
	r->yhrt_repulsion_pipeline = VK_NULL_HANDLE;
	r->yhrt_attraction_pipeline = VK_NULL_HANDLE;
	r->yhrt_update_pipeline = VK_NULL_HANDLE;
	r->yhrt_update_fp64_pipeline = VK_NULL_HANDLE;
	r->yhrt_update_instances_pipeline = VK_NULL_HANDLE;
	r->yhrt_desc_pool = VK_NULL_HANDLE;

	if (!r->yhrt_supported) {
		printf("[YHRT] Ray tracing not supported, layout disabled\n");
		return;
	}

	yhrt_load_rt_functions(r);

	// =====================================================================
	// Descriptor Set Layout — shared by all 5 compute pipelines
	// =====================================================================
	//
	// Binding  Type                          Count  Stage
	// ------  ----------------------------  -----  ------
	//   0     STORAGE_BUFFER (NodeBuffer)     1    COMPUTE
	//   1     STORAGE_BUFFER (ForceBuffer)    1    COMPUTE
	//   2     STORAGE_BUFFER (EdgeBuffer)     1    COMPUTE
	//   3     ACCELERATION_STRUCTURE (TLAS)   1    COMPUTE
	//   4     STORAGE_BUFFER (FnormBuffer)    1    COMPUTE
	//   5     STORAGE_BUFFER (InstanceBuffer) 1    COMPUTE
	//
	// Push constants: 28 bytes = 5 floats + 2 uints + 1 float
	//   [KP, CRK, p, step_size, vertex_count, edge_count, R]
	VkDescriptorSetLayoutBinding bindings[6] = {
		{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}, {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}, {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}, {3, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}, {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}, {5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
	};
	VkDescriptorSetLayoutCreateInfo layoutInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 6, .pBindings = bindings};
	VK_CHECK(vkCreateDescriptorSetLayout(r->core.device, &layoutInfo, NULL, &r->yhrt_desc_set_layout), "Failed to create YHRT descriptor set layout");

	VkPushConstantRange pcRange = {.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0, .size = 28};
	VkPipelineLayoutCreateInfo pipelineLayoutInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1, .pSetLayouts = &r->yhrt_desc_set_layout, .pushConstantRangeCount = 1, .pPushConstantRanges = &pcRange};
	VK_CHECK(vkCreatePipelineLayout(r->core.device, &pipelineLayoutInfo, NULL, &r->yhrt_pipeline_layout), "Failed to create YHRT pipeline layout");

	VkShaderModule repModule = VK_NULL_HANDLE;
	VK_CHECK(create_shader_module(r->core.device, YHRT_REPULSION_COMP_SHADER_PATH, &repModule), "Failed to create YHRT repulsion shader module");
	VkPipelineShaderStageCreateInfo repStage = VK_SHADER_STAGE_COMP(repModule);
	VkComputePipelineCreateInfo repPipeInfo = {.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, .stage = repStage, .layout = r->yhrt_pipeline_layout};
	VK_CHECK(vkCreateComputePipelines(r->core.device, VK_NULL_HANDLE, 1, &repPipeInfo, NULL, &r->yhrt_repulsion_pipeline), "Failed to create YHRT repulsion pipeline");
	vkDestroyShaderModule(r->core.device, repModule, NULL);

	VkShaderModule attModule = VK_NULL_HANDLE;
	VK_CHECK(create_shader_module(r->core.device, YHRT_ATTRACTION_COMP_SHADER_PATH, &attModule), "Failed to create YHRT attraction shader module");
	VkPipelineShaderStageCreateInfo attStage = VK_SHADER_STAGE_COMP(attModule);
	VkComputePipelineCreateInfo attPipeInfo = {.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, .stage = attStage, .layout = r->yhrt_pipeline_layout};
	VK_CHECK(vkCreateComputePipelines(r->core.device, VK_NULL_HANDLE, 1, &attPipeInfo, NULL, &r->yhrt_attraction_pipeline), "Failed to create YHRT attraction pipeline");
	vkDestroyShaderModule(r->core.device, attModule, NULL);

	VkShaderModule updModule = VK_NULL_HANDLE;
	VK_CHECK(create_shader_module(r->core.device, YHRT_UPDATE_COMP_SHADER_PATH, &updModule), "Failed to create YHRT update shader module");
	VkPipelineShaderStageCreateInfo updStage = VK_SHADER_STAGE_COMP(updModule);
	VkComputePipelineCreateInfo updPipeInfo = {.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, .stage = updStage, .layout = r->yhrt_pipeline_layout};
	VK_CHECK(vkCreateComputePipelines(r->core.device, VK_NULL_HANDLE, 1, &updPipeInfo, NULL, &r->yhrt_update_pipeline), "Failed to create YHRT update pipeline");
	vkDestroyShaderModule(r->core.device, updModule, NULL);

	if (r->core.fp64_atomics_supported) {
		VkShaderModule updFp64Module = VK_NULL_HANDLE;
		if (create_shader_module(r->core.device, YHRT_UPDATE_FP64_COMP_SHADER_PATH, &updFp64Module) == VK_SUCCESS) {
			VkPipelineShaderStageCreateInfo updFp64Stage = VK_SHADER_STAGE_COMP(updFp64Module);
			VkComputePipelineCreateInfo updFp64PipeInfo = {.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, .stage = updFp64Stage, .layout = r->yhrt_pipeline_layout};
			VK_CHECK(vkCreateComputePipelines(r->core.device, VK_NULL_HANDLE, 1, &updFp64PipeInfo, NULL, &r->yhrt_update_fp64_pipeline), "Failed to create YHRT FP64 update pipeline");
			vkDestroyShaderModule(r->core.device, updFp64Module, NULL);
		} else {
			r->yhrt_update_fp64_pipeline = VK_NULL_HANDLE;
			fprintf(stderr, "[YHRT] Warning: FP64 shader compilation failed\n");
		}
	} else {
		r->yhrt_update_fp64_pipeline = VK_NULL_HANDLE;
	}

	VkShaderModule uiModule = VK_NULL_HANDLE;
	VK_CHECK(create_shader_module(r->core.device, YHRT_UPDATE_INSTANCES_COMP_SHADER_PATH, &uiModule), "Failed to create YHRT update instances shader module");
	VkPipelineShaderStageCreateInfo uiStage = VK_SHADER_STAGE_COMP(uiModule);
	VkComputePipelineCreateInfo uiPipeInfo = {.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, .stage = uiStage, .layout = r->yhrt_pipeline_layout};
	VK_CHECK(vkCreateComputePipelines(r->core.device, VK_NULL_HANDLE, 1, &uiPipeInfo, NULL, &r->yhrt_update_instances_pipeline), "Failed to create YHRT update instances pipeline");
	vkDestroyShaderModule(r->core.device, uiModule, NULL);

	printf("[YHRT] Pipelines initialized successfully\n");
}

// ============================================================================
// Worker-Thread API: Blocking GPU iterations with igraph_progress
// ============================================================================

bool yhrt_worker_init(Renderer *r, igraph_t *graph, igraph_matrix_t *init_positions, igraph_int_t maxiter)
{
	if (!r->yhrt_supported) {
		fprintf(stderr, "[YHRT] Cannot start: ray tracing not supported\n");
		return false;
	}

	yhrt_load_rt_functions(r);
	yhrt_cleanup_session_buffers(r);

	r->yhrt_fp64_supported = r->core.fp64_atomics_supported && (r->yhrt_update_fp64_pipeline != VK_NULL_HANDLE);

	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_integer_t ecount = igraph_ecount(graph);
	r->yhrt_vcount = (uint32_t)vcount;
	r->yhrt_ecount = (uint32_t)ecount;
	r->yhrt_maxiter = (int)maxiter;
	r->yhrt_current_iter = 0;
	r->yhrt_step = 0.1f;
	r->yhrt_Fnorm0 = INFINITY;
	r->yhrt_tolerance = 0.001f;
	r->yhrt_p = 1.0f;
	r->yhrt_fnorm_readback_pending = false;

	// Progressive insertion: start with one workgroup worth of nodes, ramp up each iteration
	uint32_t batch = r->core.deviceProperties.limits.maxComputeWorkGroupInvocations;
	r->yhrt_active_vcount = (batch < r->yhrt_vcount) ? batch : r->yhrt_vcount;

	// Compute K from initial positions (matches igraph's compute_average_edge_length_3d)
	float total_len = 0.0f;
	for (igraph_integer_t e = 0; e < ecount; e++) {
		igraph_integer_t from = IGRAPH_FROM(graph, e);
		igraph_integer_t to = IGRAPH_TO(graph, e);
		float dx = (float)(MATRIX(*init_positions, from, 0) - MATRIX(*init_positions, to, 0));
		float dy = (float)(MATRIX(*init_positions, from, 1) - MATRIX(*init_positions, to, 1));
		float dz = (igraph_matrix_ncol(init_positions) > 2) ? (float)(MATRIX(*init_positions, from, 2) - MATRIX(*init_positions, to, 2)) : 0.0f;
		total_len += sqrtf(dx * dx + dy * dy + dz * dz);
	}
	r->yhrt_K = (ecount > 0) ? (total_len / ecount) : 1.0f;
	if (r->yhrt_K < 1e-6f)
		r->yhrt_K = 1.0f;
	r->yhrt_KP = powf(r->yhrt_K, 1.0f - r->yhrt_p);
	r->yhrt_CRK = powf(IGRAPH_YHU_C, (2.0f - r->yhrt_p) / 3.0f) / r->yhrt_K;
	r->yhrt_R = 5.0f * r->yhrt_K;

	printf("[YHRT] Starting: vcount=%u ecount=%u K=%.4f R=%.4f KP=%.4f CRK=%.4f\n", r->yhrt_vcount, r->yhrt_ecount, r->yhrt_K, r->yhrt_R, r->yhrt_KP, r->yhrt_CRK);

	// ---- Upload NodeBuffer ----
	VkDeviceSize nodeSize = sizeof(vec4) * vcount;
	yhrt_create_device_buffer(r, nodeSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, &r->yhrt_node_buf, &r->yhrt_node_mem);

	float *nodeData = malloc(sizeof(float) * 4 * vcount);
	for (igraph_integer_t i = 0; i < vcount; i++) {
		nodeData[i * 4 + 0] = (float)MATRIX(*init_positions, i, 0);
		nodeData[i * 4 + 1] = (float)MATRIX(*init_positions, i, 1);
		nodeData[i * 4 + 2] = (igraph_matrix_ncol(init_positions) > 2) ? (float)MATRIX(*init_positions, i, 2) : 0.0f;
		igraph_int_t deg;
		igraph_degree_1(graph, &deg, i, IGRAPH_ALL, IGRAPH_LOOPS);
		nodeData[i * 4 + 3] = (float)(deg > 0 ? deg : 1);
	}
	yhrt_staging_upload(r, r->yhrt_node_buf, nodeData, nodeSize, true);
	free(nodeData);

	// ---- Create worker-thread command pool + buffer ----
	{
		VkCommandPoolCreateInfo poolInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = (uint32_t)r->core.graphicsQueueFamily};
		VK_CHECK(vkCreateCommandPool(r->core.device, &poolInfo, NULL, &r->yhrt_cmd_pool), "Failed to create YHRT worker cmd pool");
		VkCommandBufferAllocateInfo cmdInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = r->yhrt_cmd_pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
		VK_CHECK(vkAllocateCommandBuffers(r->core.device, &cmdInfo, &r->yhrt_cmd_buf), "Failed to allocate YHRT worker cmd");
	}

	// ---- Staging buffer for periodic position readback ----
	yhrt_create_buffer(r, nodeSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &r->yhrt_node_staging_buf, &r->yhrt_node_staging_mem);

	// ---- Fnorm + dispatch fence ----
	yhrt_create_device_buffer(r, sizeof(double), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, &r->yhrt_fnorm_buf, &r->yhrt_fnorm_mem);
	yhrt_create_buffer(r, sizeof(double), VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, &r->yhrt_staging_buf, &r->yhrt_staging_mem);
	{
		VkFenceCreateInfo fenceInfo = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT};
		VK_CHECK(vkCreateFence(r->core.device, &fenceInfo, NULL, &r->yhrt_dispatch_fence), "Failed to create YHRT dispatch fence");
	}

	// ---- Force buffer ----
	yhrt_create_device_buffer(r, sizeof(vec4) * vcount, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &r->yhrt_force_buf, &r->yhrt_force_mem);

	// ---- Edge buffer ----
	{
		struct EdgeData
		{
			uint32_t from;
			uint32_t to;
			float weight;
		};
		VkDeviceSize edgeSize = sizeof(struct EdgeData) * ecount;
		yhrt_create_device_buffer(r, edgeSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, &r->yhrt_edge_buf, &r->yhrt_edge_mem);

		struct EdgeData *edgeData = malloc(edgeSize);
		for (igraph_integer_t e = 0; e < ecount; e++) {
			edgeData[e].from = (uint32_t)IGRAPH_FROM(graph, e);
			edgeData[e].to = (uint32_t)IGRAPH_TO(graph, e);
			edgeData[e].weight = 1.0f;
		}
		yhrt_staging_upload(r, r->yhrt_edge_buf, edgeData, edgeSize, true);
		free(edgeData);
	}

	// =====================================================================
	// BLAS + TLAS Build — Spatial acceleration structure for repulsion queries
	// =====================================================================
	//
	// Architecture overview (all nodes share ONE BLAS, instanced N times):
	//
	//   TLAS (Top-Level Acceleration Structure)
	//   +-----------+-----------+-----+-----------+
	//   | Instance0 | Instance1 | ... | InstanceN |   N instances total
	//   +-----------+-----------+-----+-----------+
	//        |           |               |
	//        v           v               v
	//   +------+     +------+         +------+
	//   | BLAS |     | BLAS |   ...   | BLAS |       SAME BLAS, different
	//   +------+     +------+         +------+       3x4 transform matrices
	//      |            |                |
	//      v            v                v
	//   [-R,R]³      [-R,R]³          [-R,R]³       Single AABB primitive
	//   (origin)     (at pos_1)       (at pos_N)    per instance, translated
	//
	//   BLAS detail:
	//   ┌─────────────────────────────────────────────┐
	//   │  VkAabbPositionsKHR  (single primitive)     │
	//   │                                             │
	//   │   minX = -R    minY = -R    minZ = -R       │
	//   │   maxX = +R    maxY = +R    maxZ = +R       │
	//   │                                             │
	//   │   R = 5 * K  (K = average edge length)      │
	//   │                                             │
	//   │   This defines a cube centered at the origin.│
	//   │   Each TLAS instance translates it to the   │
	//   │   node's world position via its transform.   │
	//   └─────────────────────────────────────────────┘
	//
	//   Instance transform (3x4 column-major matrix):
	//   ┌                  ┐   ┌        ┐
	//   │ 1  0  0  pos.x   │   │ node i │
	//   │ 0  1  0  pos.y   │ = | pos    │
	//   │ 0  0  1  pos.z   │   │        │
	//   └                  ┘   └        ┘
	//   (identity rotation + translation only — no scaling needed)
	//
	//   Ray query in yh_repulsion.comp:
	//     origin = node[i].pos
	//     dir    = (1,0,0)  — arbitrary, only AABB intersection matters
	//     tmax   = R         — only consider nearby nodes
	//     → BVH traversal prunes nodes whose AABBs don't intersect the ray
	//     → Returns instanceCustomIndex = node index for force computation
	//
	// The BLAS is built ONCE and shared by all instances. Only the TLAS
	// instance transforms are updated per iteration (via yh_update_instances
	// comp + CmdBuildAccelerationStructuresKHR in UPDATE mode).
	// =====================================================================

	VkDeviceSize blasScratchSize = 0;
	VkDeviceSize tlasScratchSize = 0;
	VkBuffer aabbBuf = VK_NULL_HANDLE;
	VkDeviceMemory aabbMem = VK_NULL_HANDLE;

	// BLAS geometry: single AABB primitive (the bounding cube for one node)
	VkAccelerationStructureGeometryKHR blasGeometry = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR, .geometryType = VK_GEOMETRY_TYPE_AABBS_KHR, .flags = VK_GEOMETRY_OPAQUE_BIT_KHR};
	VkAccelerationStructureBuildGeometryInfoKHR blasBuildInfo = {0};

	// TLAS geometry: N instances referencing the same BLAS
	VkAccelerationStructureGeometryKHR tlasGeometry = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR, .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR, .flags = VK_GEOMETRY_OPAQUE_BIT_KHR, .geometry.instances = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR}};

	// BLAS: 1 primitive (the single AABB). TLAS: vcount primitives (one per node)
	VkAccelerationStructureBuildRangeInfoKHR blasRangeInfo = {.primitiveCount = 1};
	VkAccelerationStructureBuildRangeInfoKHR tlasRangeInfo = {.primitiveCount = vcount};

	// =================================================================
	// Step 1: Size the BLAS — single AABB of radius R centered at origin
	// =================================================================
	{
		// The AABB defines a cube [-R,R]³ at the origin. Each TLAS instance
		// translates this cube to its node's position via the instance transform.
		VkAabbPositionsKHR aabb = {.minX = -r->yhrt_R, .minY = -r->yhrt_R, .minZ = -r->yhrt_R, .maxX = r->yhrt_R, .maxY = r->yhrt_R, .maxZ = r->yhrt_R};

		// Upload the AABB to a GPU buffer (must have SHADER_DEVICE_ADDRESS_BIT
		// for the BLAS build input, and ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY)
		yhrt_create_buffer(r, sizeof(VkAabbPositionsKHR), VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, &aabbBuf, &aabbMem);
		void *mapped;
		vkMapMemory(r->core.device, aabbMem, 0, sizeof(VkAabbPositionsKHR), 0, &mapped);
		memcpy(mapped, &aabb, sizeof(VkAabbPositionsKHR));
		vkUnmapMemory(r->core.device, aabbMem);

		blasGeometry.geometry.aabbs.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR;
		blasGeometry.geometry.aabbs.data.deviceAddress = yhrt_get_buffer_address(r, aabbBuf);
		blasGeometry.geometry.aabbs.stride = sizeof(VkAabbPositionsKHR);

		blasBuildInfo = (VkAccelerationStructureBuildGeometryInfoKHR){.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR, .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR, .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR, .geometryCount = 1, .pGeometries = &blasGeometry};

		// Query how much memory the BLAS needs (storage + scratch)
		uint32_t maxPrims = 1;
		VkAccelerationStructureBuildSizesInfoKHR blasSizeInfo = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
		rt_funcs.GetAccelerationStructureBuildSizesKHR(r->core.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &blasBuildInfo, &maxPrims, &blasSizeInfo);

		// Allocate the BLAS storage buffer and create the BLAS handle
		yhrt_create_device_buffer(r, blasSizeInfo.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, &r->yhrt_blas_buf, &r->yhrt_blas_mem);
		blasScratchSize = blasSizeInfo.buildScratchSize;

		VkAccelerationStructureCreateInfoKHR blasCreateInfo = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR, .buffer = r->yhrt_blas_buf, .offset = 0, .size = blasSizeInfo.accelerationStructureSize, .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR};
		VK_CHECK(rt_funcs.CreateAccelerationStructureKHR(r->core.device, &blasCreateInfo, NULL, &r->yhrt_blas), "Failed to create BLAS");
	}

	// =================================================================
	// Step 2: Size the TLAS — will contain vcount instances of the BLAS
	// =================================================================
	{
		// Device address is set to 0 here for sizing only; the real address
		// is provided later when we build after uploading instance data.
		tlasGeometry.geometry.instances.data.deviceAddress = 0;
		VkAccelerationStructureBuildGeometryInfoKHR tlasBuildInfo = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR, .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR, .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR, .geometryCount = 1, .pGeometries = &tlasGeometry};
		uint32_t maxInstances = vcount;
		VkAccelerationStructureBuildSizesInfoKHR tlasSizeInfo = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
		rt_funcs.GetAccelerationStructureBuildSizesKHR(r->core.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &tlasBuildInfo, &maxInstances, &tlasSizeInfo);

		tlasScratchSize = tlasSizeInfo.buildScratchSize;
	}

	// Shared scratch buffer: allocate once with the larger of BLAS/TLAS requirements.
	// BLAS scratch is used only during init; TLAS scratch is reused for per-iteration
	// updates. They never overlap in time.
	VkDeviceSize scratchSize = blasScratchSize > tlasScratchSize ? blasScratchSize : tlasScratchSize;
	yhrt_create_device_buffer(r, scratchSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, &r->yhrt_as_scratch_buf, &r->yhrt_as_scratch_mem);

	// =================================================================
	// Step 3: Build the BLAS (one-time, synchronous)
	// =================================================================
	// The BLAS is immutable after this — it's just the AABB template.
	// All per-node positioning happens via TLAS instance transforms.
	{
		blasBuildInfo.dstAccelerationStructure = r->yhrt_blas;
		blasBuildInfo.scratchData.deviceAddress = yhrt_get_buffer_address(r, r->yhrt_as_scratch_buf);
		const VkAccelerationStructureBuildRangeInfoKHR *pBlasRange = &blasRangeInfo;

		VkCommandPoolCreateInfo poolInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, .queueFamilyIndex = (uint32_t)r->core.graphicsQueueFamily};
		VkCommandPool blasPool;
		vkCreateCommandPool(r->core.device, &poolInfo, NULL, &blasPool);
		VkCommandBufferAllocateInfo cmdInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = blasPool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
		VkCommandBuffer blasCmd;
		vkAllocateCommandBuffers(r->core.device, &cmdInfo, &blasCmd);
		vkBeginCommandBuffer(blasCmd, &VK_CMD_BEGIN_INFO_ONETIME);
		rt_funcs.CmdBuildAccelerationStructuresKHR(blasCmd, 1, &blasBuildInfo, &pBlasRange);
		vkEndCommandBuffer(blasCmd);
		VkSubmitInfo submitInfo = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &blasCmd};
		pthread_mutex_lock(&r->core.graphicsQueueMutex);
		vkQueueSubmit(r->core.graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
		vkQueueWaitIdle(r->core.graphicsQueue);
		pthread_mutex_unlock(&r->core.graphicsQueueMutex);
		vkDestroyCommandPool(r->core.device, blasPool, NULL);

		// The AABB input buffer is no longer needed after the BLAS is built
		VK_DESTROY_BUFFER(r->core.device, aabbBuf, aabbMem);
	}

	// =================================================================
	// Step 4: Create TLAS instances and build the TLAS
	// =================================================================
	// Each VkAccelerationStructureInstanceKHR positions the shared BLAS at
	// a node's location via a 3x4 column-major transform matrix.
	//
	// Instance layout in memory (instance_data[] buffer):
	//
	//   Instance 0:  [m00, m10, m20, m30,  m01, m11, m21, m31,  m02, m12, m22, m32]
	//   Instance 1:  [m00, m10, m20, m30,  m01, m11, m21, m31,  m02, m12, m22, m32]
	//   ...
	//   Instance N:  [m00, m10, m20, m30,  m01, m11, m21, m31,  m02, m12, m22, m32]
	//
	//   Where each matrix is:
	//     col0: [1, 0, 0]     col1: [0, 1, 0]     col2: [0, 0, 1]     col3: [pos.x, pos.y, pos.z]
	//
	// After yh_update_instances.comp writes new positions, the TLAS is rebuilt
	// in UPDATE mode (in-place, no full restructure) in yhrt_worker_step().
	{
		VkDeviceSize instSize = sizeof(VkAccelerationStructureInstanceKHR) * vcount;
		yhrt_create_device_buffer(r, instSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &r->yhrt_instance_buf, &r->yhrt_instance_mem);

		VkAccelerationStructureInstanceKHR *instances = calloc(vcount, sizeof(VkAccelerationStructureInstanceKHR));
		uint64_t blasAddr = yhrt_get_as_address(r, r->yhrt_blas);
		for (uint32_t i = 0; i < vcount; i++) {
			memset(&instances[i], 0, sizeof(VkAccelerationStructureInstanceKHR));
			instances[i].instanceCustomIndex = i; // Returned by rayQueryGetIntersectionInstanceIdEXT in shader
			instances[i].mask = 0xFF;			  // All bits set: always intersect
			instances[i].instanceShaderBindingTableRecordOffset = 0;
			instances[i].flags = VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR; // Skip any-hit (not used)
			instances[i].accelerationStructureReference = blasAddr;			// All instances point to same BLAS

			// 3x4 column-major transform: identity rotation + translation to node i's position.
			// The shader writes this same layout via instance_data[i*4+0..2].
			float *m = (float *)instances[i].transform.matrix[0];
			m[0] = 1.0f;	  // col0.x (scale X)
			m[1] = 0.0f;	  // col0.y
			m[2] = 0.0f;	  // col0.z
			m[3] = (float)i;  // col3.x (pos.x, initially just index for placeholder)
			m[4] = 0.0f;	  // col1.x
			m[5] = 1.0f;	  // col1.y (scale Y)
			m[6] = 0.0f;	  // col1.z
			m[7] = (float)i;  // col3.y (pos.y)
			m[8] = 0.0f;	  // col2.x
			m[9] = 0.0f;	  // col2.y
			m[10] = 1.0f;	  // col2.z (scale Z)
			m[11] = (float)i; // col3.z (pos.z)
		}
		yhrt_staging_upload(r, r->yhrt_instance_buf, instances, instSize, true);
		free(instances);

		// Now that instance data is uploaded, provide the real device address to the TLAS geometry
		uint64_t instAddr = yhrt_get_buffer_address(r, r->yhrt_instance_buf);
		tlasGeometry.geometry.instances.data.deviceAddress = instAddr;

		// Re-query TLAS size with the real instance address (required by spec)
		VkAccelerationStructureBuildSizesInfoKHR tlasSizeInfo = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
		{
			VkAccelerationStructureBuildGeometryInfoKHR tmpInfo = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR, .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR, .geometryCount = 1, .pGeometries = &tlasGeometry};
			uint32_t maxInstances = r->yhrt_vcount;
			rt_funcs.GetAccelerationStructureBuildSizesKHR(r->core.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &tmpInfo, &maxInstances, &tlasSizeInfo);
		}

		// Allocate TLAS storage and create the handle.
		// ALLOW_UPDATE_BIT is critical: per-iteration updates use MODE_UPDATE_KHR
		// which patches the BVH in-place (much faster than full rebuild).
		yhrt_create_device_buffer(r, tlasSizeInfo.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, &r->yhrt_tlas_buf, &r->yhrt_tlas_mem);
		VkAccelerationStructureCreateInfoKHR tlasCreate = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR, .buffer = r->yhrt_tlas_buf, .size = tlasSizeInfo.accelerationStructureSize, .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR};
		VK_CHECK(rt_funcs.CreateAccelerationStructureKHR(r->core.device, &tlasCreate, NULL, &r->yhrt_tlas), "Failed to create TLAS");

		// Initial build (full build, not update — subsequent iterations use UPDATE mode)
		VkAccelerationStructureBuildGeometryInfoKHR tlasBuildInfo = {
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
			.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
			.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR,
			.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
			.dstAccelerationStructure = r->yhrt_tlas,
			.geometryCount = 1,
			.pGeometries = &tlasGeometry,
			.scratchData.deviceAddress = yhrt_get_buffer_address(r, r->yhrt_as_scratch_buf),
		};
		const VkAccelerationStructureBuildRangeInfoKHR *pTlasRange = &tlasRangeInfo;

		VkCommandPoolCreateInfo poolInfo2 = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, .queueFamilyIndex = (uint32_t)r->core.graphicsQueueFamily};
		VkCommandPool tlasPool;
		vkCreateCommandPool(r->core.device, &poolInfo2, NULL, &tlasPool);
		VkCommandBufferAllocateInfo cmdInfo2 = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = tlasPool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
		VkCommandBuffer tlasCmd;
		vkAllocateCommandBuffers(r->core.device, &cmdInfo2, &tlasCmd);
		vkBeginCommandBuffer(tlasCmd, &VK_CMD_BEGIN_INFO_ONETIME);
		rt_funcs.CmdBuildAccelerationStructuresKHR(tlasCmd, 1, &tlasBuildInfo, &pTlasRange);
		vkEndCommandBuffer(tlasCmd);
		VkSubmitInfo submitInfo2 = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &tlasCmd};
		pthread_mutex_lock(&r->core.graphicsQueueMutex);
		vkQueueSubmit(r->core.graphicsQueue, 1, &submitInfo2, VK_NULL_HANDLE);
		vkQueueWaitIdle(r->core.graphicsQueue);
		pthread_mutex_unlock(&r->core.graphicsQueueMutex);
		vkDestroyCommandPool(r->core.device, tlasPool, NULL);
	}

	// =================================================================
	// Step 5: Create descriptor pool and bind all resources
	// =================================================================
	//
	// Descriptor set binding map (matches shader bindings):
	//
	//   Binding | Type                    | Buffer / Resource       | Used By
	//   --------|------------------------|------------------------|------------------
	//     0     | STORAGE_BUFFER          | NodeBuffer (vec4[])     | All shaders
	//     1     | STORAGE_BUFFER          | ForceBuffer (vec4[])    | All shaders
	//     2     | STORAGE_BUFFER          | EdgeBuffer (EdgeData[]) | Attraction only
	//     3     | ACCELERATION_STRUCTURE  | TLAS                    | Repulsion only
	//     4     | STORAGE_BUFFER          | FnormBuffer (double)    | Update only
	//     5     | STORAGE_BUFFER          | InstanceBuffer (vec4[]) | UpdateInstances only
	//
	// Note: binding 3 (TLAS) uses a separate write type (VkWriteDescriptorSet
	// with pNext = VkWriteDescriptorSetAccelerationStructureKHR) because
	// acceleration structures cannot be written via VkDescriptorBufferInfo.
	{
		VkDescriptorPoolSize poolSizes[] = {
			{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 5},				// bindings 0,1,2,4,5
			{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1}, // binding 3
		};
		VkDescriptorPoolCreateInfo dpInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 1, .poolSizeCount = 2, .pPoolSizes = poolSizes};
		VK_CHECK(vkCreateDescriptorPool(r->core.device, &dpInfo, NULL, &r->yhrt_desc_pool), "Failed to create YHRT descriptor pool");
		VkDescriptorSetAllocateInfo setInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = r->yhrt_desc_pool, .descriptorSetCount = 1, .pSetLayouts = &r->yhrt_desc_set_layout};
		VK_CHECK(vkAllocateDescriptorSets(r->core.device, &setInfo, &r->yhrt_desc_set), "Failed to allocate YHRT descriptor set");

		// Buffer descriptors (bindings 0,1,2,4,5)
		VkDescriptorBufferInfo nodeInfo = {r->yhrt_node_buf, 0, VK_WHOLE_SIZE};
		VkDescriptorBufferInfo forceInfo = {r->yhrt_force_buf, 0, VK_WHOLE_SIZE};
		VkDescriptorBufferInfo edgeInfo = {r->yhrt_edge_buf, 0, VK_WHOLE_SIZE};
		VkDescriptorBufferInfo fnormInfo = {r->yhrt_fnorm_buf, 0, VK_WHOLE_SIZE};
		VkDescriptorBufferInfo instInfo = {r->yhrt_instance_buf, 0, VK_WHOLE_SIZE};

		VkWriteDescriptorSet writes[5] = {
			VK_WRITE_DESC_BUFFER(r->yhrt_desc_set, 0, &nodeInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),  // NodeBuffer
			VK_WRITE_DESC_BUFFER(r->yhrt_desc_set, 1, &forceInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER), // ForceBuffer
			VK_WRITE_DESC_BUFFER(r->yhrt_desc_set, 2, &edgeInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),  // EdgeBuffer
			VK_WRITE_DESC_BUFFER(r->yhrt_desc_set, 4, &fnormInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER), // FnormBuffer
			VK_WRITE_DESC_BUFFER(r->yhrt_desc_set, 5, &instInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),  // InstanceBuffer
		};
		vkUpdateDescriptorSets(r->core.device, 5, writes, 0, NULL);

		// TLAS descriptor (binding 3) — written via the acceleration structure path
		VkWriteDescriptorSetAccelerationStructureKHR asDescInfo = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
			.accelerationStructureCount = 1,
			.pAccelerationStructures = &r->yhrt_tlas,
		};
		VkWriteDescriptorSet tlasWrite = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.pNext = &asDescInfo,
			.dstSet = r->yhrt_desc_set,
			.dstBinding = 3,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
		};
		vkUpdateDescriptorSets(r->core.device, 1, &tlasWrite, 0, NULL);
	}

	r->yhrt_active = true;
	printf("[YHRT] Worker session started, %d iterations planned\n", r->yhrt_maxiter);
	return true;
}

bool yhrt_worker_step(Renderer *r)
{
	if (!r->yhrt_active || !r->yhrt_supported)
		return false;
	if (r->yhrt_current_iter >= r->yhrt_maxiter)
		return false;

	// Wait for previous GPU work — fence stays SIGNALED after this
	VK_CHECK(vkWaitForFences(r->core.device, 1, &r->yhrt_dispatch_fence, VK_TRUE, UINT64_MAX), "Failed to wait for YHRT dispatch fence");

	// Progressive insertion: add a batch of nodes every 5 iterations
	// Save previous active count for Fnorm scaling (forces were computed with it)
	uint32_t prev_active_vcount = r->yhrt_active_vcount;
	{
		uint32_t batch = r->core.deviceProperties.limits.maxComputeWorkGroupInvocations;
		uint32_t new_active = (r->yhrt_current_iter / 5 + 1) * batch;
		if (new_active > r->yhrt_vcount)
			new_active = r->yhrt_vcount;

		if (new_active != prev_active_vcount) {
			printf("[YHRT] progressive: %u -> %u nodes (batch=%u)\n", prev_active_vcount, new_active, batch);
			r->yhrt_Fnorm0 = INFINITY;
			r->yhrt_step = 0.1f;
		}
		r->yhrt_active_vcount = new_active;
	}

	// Read back Fnorm from previous iteration (valid because fence was just waited on)
	if (r->yhrt_current_iter > 0) {
		float fnorm = 0.0f;
		void *mapped;
		if (vkMapMemory(r->core.device, r->yhrt_staging_mem, 0, sizeof(double), 0, &mapped) == VK_SUCCESS) {
			if (r->yhrt_fp64_supported)
				fnorm = (float)(*(double *)mapped);
			else
				// Scale by prev_active_vcount — that's what the shader used for reduction
				fnorm = (*(float *)mapped) * (float)prev_active_vcount;
			vkUnmapMemory(r->core.device, r->yhrt_staging_mem);
		}

		if ((r->yhrt_current_iter - 1) % 50 == 0)
			printf("[YHRT] iter=%d, step=%g, Fnorm=%g, Fnorm0=%g, active=%u/%u, repulsive_exp=%g, natlen=%g\n", r->yhrt_current_iter - 1, r->yhrt_step, fnorm, r->yhrt_Fnorm0, r->yhrt_active_vcount, r->yhrt_vcount, r->yhrt_p, r->yhrt_K);

		if (fnorm < r->yhrt_Fnorm0) {
			if (fnorm > 0.95f * r->yhrt_Fnorm0) {
			} else {
				r->yhrt_step *= 0.99f / IGRAPH_YHU_COOL;
			}
		} else {
			r->yhrt_step *= IGRAPH_YHU_COOL;
		}
		r->yhrt_Fnorm0 = fnorm;

		// Check tolerance — only real convergence when all nodes are active
		if (r->yhrt_step < r->yhrt_tolerance) {
			if (r->yhrt_active_vcount >= r->yhrt_vcount)
				return false;
			// Still inserting nodes — ignore premature convergence
			r->yhrt_step = r->yhrt_tolerance;
		}
	}

	// Only reset fence when we're about to submit new work
	VK_CHECK(vkResetFences(r->core.device, 1, &r->yhrt_dispatch_fence), "Failed to reset YHRT dispatch fence");

	// =====================================================================
	// Per-Iteration Compute Pipeline
	// =====================================================================
	//
	// Each iteration executes 4 compute dispatches + 1 TLAS update, with
	// pipeline barriers between them to ensure correct data dependencies:
	//
	//   ┌─────────────────────────────────────────────────────────────────┐
	//   │  Fnorm Reset (vkCmdFillBuffer → zeros the accumulator)        │
	//   │  TRANSFER → COMPUTE barrier                                   │
	//   └───────────────────────────────┬───────────────────────────────┘
	//                                   v
	//   ┌─────────────────────────────────────────────────────────────────┐
	//   │  Dispatch 1: REPULSION  (vertex_count / 256 workgroups)       │
	//   │  - Each node fires ray query against TLAS                     │
	//   │  - Reads NodeBuffer (binding 0), writes ForceBuffer (binding 1)│
	//   │  - Uses TLAS (binding 3) for spatial acceleration             │
	//   │  COMPUTE → COMPUTE barrier (force buffer dependency)           │
	//   └───────────────────────────────┬───────────────────────────────┘
	//                                   v
	//   ┌─────────────────────────────────────────────────────────────────┐
	//   │  Dispatch 2: ATTRACTION  (edge_count / 256 workgroups)        │
	//   │  - Each edge computes spring force on both endpoints           │
	//   │  - Reads NodeBuffer + EdgeBuffer (bindings 0,2)               │
	//   │  - Atomic adds into ForceBuffer (binding 1)                   │
	//   │  COMPUTE → COMPUTE barrier (force buffer dependency)           │
	//   └───────────────────────────────┬───────────────────────────────┘
	//                                   v
	//   ┌─────────────────────────────────────────────────────────────────┐
	//   │  Dispatch 3: UPDATE  (vertex_count / 256 workgroups)          │
	//   │  - Integrates positions: pos += step * normalize(force)        │
	//   │  - Reduces force magnitudes → FnormBuffer (binding 4)          │
	//   │  - Zeros ForceBuffer for next iteration                       │
	//   │  COMPUTE → COMPUTE barrier (instance buffer dependency)        │
	//   └───────────────────────────────┬───────────────────────────────┘
	//                                   v
	//   ┌─────────────────────────────────────────────────────────────────┐
	//   │  Dispatch 4: UPDATE INSTANCES  (vertex_count / 256 workgroups)│
	//   │  - Copies node positions → InstanceBuffer transforms           │
	//   │  - Reads NodeBuffer (binding 0), writes InstanceBuffer (b.5)  │
	//   │  COMPUTE → ACCELERATION_STRUCTURE_BUILD barrier                │
	//   └───────────────────────────────┬───────────────────────────────┘
	//                                   v
	//   ┌─────────────────────────────────────────────────────────────────┐
	//   │  TLAS Update (vkCmdBuildAccelerationStructuresKHR)            │
	//   │  - MODE_UPDATE_KHR: patches BVH in-place using new transforms │
	//   │  - src = dst = same TLAS (in-place update, not full rebuild)   │
	//   │  ACCELERATION_STRUCTURE_BUILD → COMPUTE barrier                │
	//   └───────────────────────────────┬───────────────────────────────┘
	//                                   v
	//   ┌─────────────────────────────────────────────────────────────────┐
	//   │  Fnorm Readback Copy  (fnorm_buf → staging_buf)               │
	//   │  + Optional position readback every 5 iterations              │
	//   │  CPU reads staging next iteration → adjusts step_size          │
	//   └─────────────────────────────────────────────────────────────────┘
	//
	// The TLAS is now updated, so the next iteration's repulsion pass will
	// query node positions at their new locations.
	// =====================================================================

	VkCommandBuffer cmd = r->yhrt_cmd_buf;
	VK_CHECK(vkResetCommandBuffer(cmd, 0), "Failed to reset YHRT worker cmd");
	VkCommandBufferBeginInfo beginInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
	VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo), "Failed to begin YHRT worker cmd");

	// Fnorm reset
	vkCmdFillBuffer(cmd, r->yhrt_fnorm_buf, 0, sizeof(double), 0);
	VK_PIPELINE_BARRIER(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

	struct
	{
		float KP, CRK, p, step_size;
		uint32_t vertex_count, edge_count;
		float R;
	} pc = {r->yhrt_KP, r->yhrt_CRK, r->yhrt_p, r->yhrt_step, r->yhrt_active_vcount, r->yhrt_ecount, r->yhrt_R};

	// Repulsion — only active nodes
	YHRT_DISPATCH_COMPUTE(cmd, r->yhrt_repulsion_pipeline, r->yhrt_pipeline_layout, r->yhrt_desc_set, pc, (r->yhrt_active_vcount + YHRT_WORKGROUP_SIZE - 1) / YHRT_WORKGROUP_SIZE);

	VK_PIPELINE_BARRIER(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

	// Attraction — all edges (shader skips edges where pc.vertex_count bounds active nodes)
	YHRT_DISPATCH_COMPUTE(cmd, r->yhrt_attraction_pipeline, r->yhrt_pipeline_layout, r->yhrt_desc_set, pc, (r->yhrt_ecount + YHRT_WORKGROUP_SIZE - 1) / YHRT_WORKGROUP_SIZE);

	VK_PIPELINE_BARRIER(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

	// Update — only active nodes
	VkPipeline updatePipeline = r->yhrt_fp64_supported ? r->yhrt_update_fp64_pipeline : r->yhrt_update_pipeline;
	YHRT_DISPATCH_COMPUTE(cmd, updatePipeline, r->yhrt_pipeline_layout, r->yhrt_desc_set, pc, (r->yhrt_active_vcount + YHRT_WORKGROUP_SIZE - 1) / YHRT_WORKGROUP_SIZE);

	VK_PIPELINE_BARRIER(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

	// Instance update — only active nodes
	YHRT_DISPATCH_COMPUTE(cmd, r->yhrt_update_instances_pipeline, r->yhrt_pipeline_layout, r->yhrt_desc_set, pc, (r->yhrt_active_vcount + YHRT_WORKGROUP_SIZE - 1) / YHRT_WORKGROUP_SIZE);

	// Instance -> TLAS barrier
	VK_PIPELINE_BARRIER(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_SHADER_READ_BIT);

	// =====================================================================
	// TLAS Update (in-place BVH patch, not full rebuild)
	// =====================================================================
	// After yh_update_instances.comp writes the new transforms into the
	// instance buffer, we rebuild the TLAS in UPDATE mode. This patches
	// the existing BVH nodes to reflect the new instance positions without
	// discarding the previous structure — significantly faster than a full
	// rebuild, and sufficient because only translations change (no topology).
	//
	// Key difference from the initial build:
	//   mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR
	//   srcAccelerationStructure = dstAccelerationStructure = same TLAS
	{
		VkAccelerationStructureGeometryKHR tlasGeometry = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR, .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR, .flags = VK_GEOMETRY_OPAQUE_BIT_KHR, .geometry.instances = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR, .data.deviceAddress = yhrt_get_buffer_address(r, r->yhrt_instance_buf)}};
		VkAccelerationStructureBuildGeometryInfoKHR tlasBuildInfo = {
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
			.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
			.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR,
			.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR,
			.srcAccelerationStructure = r->yhrt_tlas, // Patch in-place from current state
			.dstAccelerationStructure = r->yhrt_tlas, // Write back to same TLAS
			.geometryCount = 1,
			.pGeometries = &tlasGeometry,
			.scratchData.deviceAddress = yhrt_get_buffer_address(r, r->yhrt_as_scratch_buf),
		};
		VkAccelerationStructureBuildRangeInfoKHR tlasRangeInfo = {.primitiveCount = r->yhrt_active_vcount};
		const VkAccelerationStructureBuildRangeInfoKHR *pTlasRange = &tlasRangeInfo;
		rt_funcs.CmdBuildAccelerationStructuresKHR(cmd, 1, &tlasBuildInfo, &pTlasRange);
	}

	// TLAS -> compute barrier
	VK_PIPELINE_BARRIER(cmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR, VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT);

	// Fnorm readback copy
	VK_PIPELINE_BARRIER(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
	VkBufferCopy fnormCopy = {.size = sizeof(double)};
	vkCmdCopyBuffer(cmd, r->yhrt_fnorm_buf, r->yhrt_staging_buf, 1, &fnormCopy);
	r->yhrt_fnorm_readback_pending = true;

	// Periodic position readback (every 5 iterations)
	if ((r->yhrt_current_iter + 1) % 5 == 0 || r->yhrt_current_iter + 1 >= r->yhrt_maxiter) {
		VK_PIPELINE_BARRIER(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
		VkBufferCopy posCopy = {.size = sizeof(vec4) * r->yhrt_active_vcount};
		vkCmdCopyBuffer(cmd, r->yhrt_node_buf, r->yhrt_node_staging_buf, 1, &posCopy);
	}

	VK_CHECK(vkEndCommandBuffer(cmd), "Failed to end YHRT worker cmd");

	VkSubmitInfo submitInfo = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
	pthread_mutex_lock(&r->core.graphicsQueueMutex);
	VK_CHECK(vkQueueSubmit(r->core.graphicsQueue, 1, &submitInfo, r->yhrt_dispatch_fence), "Failed to submit YHRT worker");
	pthread_mutex_unlock(&r->core.graphicsQueueMutex);

	r->yhrt_current_iter++;
	return true;
}

bool yhrt_worker_readback(Renderer *r, igraph_matrix_t *out_positions)
{
	if (!out_positions)
		return false;

	VK_CHECK(vkWaitForFences(r->core.device, 1, &r->yhrt_dispatch_fence, VK_TRUE, UINT64_MAX), "Failed to wait for YHRT readback fence");
	// NOTE: do NOT reset the fence here — the next yhrt_worker_step call resets it

	VkDeviceSize nodeSize = sizeof(vec4) * r->yhrt_vcount;
	void *mapped;
	VK_CHECK(vkMapMemory(r->core.device, r->yhrt_node_staging_mem, 0, nodeSize, 0, &mapped), "Failed to map YHRT readback");

	float *positions = (float *)mapped;
	igraph_integer_t vcount = (igraph_integer_t)r->yhrt_vcount;
	igraph_integer_t ncols = igraph_matrix_ncol(out_positions);

	for (igraph_integer_t i = 0; i < vcount; i++) {
		MATRIX(*out_positions, i, 0) = positions[i * 4 + 0];
		MATRIX(*out_positions, i, 1) = positions[i * 4 + 1];
		if (ncols > 2)
			MATRIX(*out_positions, i, 2) = positions[i * 4 + 2];
	}
	vkUnmapMemory(r->core.device, r->yhrt_node_staging_mem);
	return true;
}

void yhrt_worker_cleanup(Renderer *r)
{
	pthread_mutex_lock(&r->core.graphicsQueueMutex);
	vkQueueWaitIdle(r->core.graphicsQueue);
	pthread_mutex_unlock(&r->core.graphicsQueueMutex);
	yhrt_cleanup_session_buffers(r);
	r->yhrt_active = false;
	printf("[YHRT] Worker session completed after %d iterations, final step=%.6f\n", r->yhrt_current_iter, r->yhrt_step);
}

// ============================================================================
// Full Cleanup (called at renderer shutdown)
// ============================================================================

void yhrt_destroy(Renderer *r)
{
	yhrt_cleanup_session_buffers(r);

	if (r->yhrt_update_pipeline != VK_NULL_HANDLE)
		vkDestroyPipeline(r->core.device, r->yhrt_update_pipeline, NULL);
	if (r->yhrt_update_fp64_pipeline != VK_NULL_HANDLE)
		vkDestroyPipeline(r->core.device, r->yhrt_update_fp64_pipeline, NULL);
	if (r->yhrt_update_instances_pipeline != VK_NULL_HANDLE)
		vkDestroyPipeline(r->core.device, r->yhrt_update_instances_pipeline, NULL);
	if (r->yhrt_attraction_pipeline != VK_NULL_HANDLE)
		vkDestroyPipeline(r->core.device, r->yhrt_attraction_pipeline, NULL);
	if (r->yhrt_repulsion_pipeline != VK_NULL_HANDLE)
		vkDestroyPipeline(r->core.device, r->yhrt_repulsion_pipeline, NULL);
	if (r->yhrt_pipeline_layout != VK_NULL_HANDLE)
		vkDestroyPipelineLayout(r->core.device, r->yhrt_pipeline_layout, NULL);
	if (r->yhrt_desc_set_layout != VK_NULL_HANDLE)
		vkDestroyDescriptorSetLayout(r->core.device, r->yhrt_desc_set_layout, NULL);

	r->yhrt_repulsion_pipeline = VK_NULL_HANDLE;
	r->yhrt_attraction_pipeline = VK_NULL_HANDLE;
	r->yhrt_update_pipeline = VK_NULL_HANDLE;
	r->yhrt_update_instances_pipeline = VK_NULL_HANDLE;
	r->yhrt_pipeline_layout = VK_NULL_HANDLE;
	r->yhrt_desc_set_layout = VK_NULL_HANDLE;
	r->yhrt_active = false;
}
