#include "vulkan/rt_layout.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vulkan/buffers.h"
#include "vulkan/renderer.h"
#include "vulkan/utils.h"

#define YHRT_WORKGROUP_SIZE 256
#define YHRT_FNORM_READBACK_INTERVAL 1
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
	r->yhrt_update_instances_pipeline = VK_NULL_HANDLE;

	if (!r->yhrt_supported) {
		printf("[YHRT] Ray tracing not supported, layout disabled\n");
		return;
	}

	yhrt_load_rt_functions(r);

	// Descriptor set layout: 6 bindings
	VkDescriptorSetLayoutBinding bindings[6] = {
		{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},			  // node
		{1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},			  // force
		{2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},			  // edge
		{3, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}, // tlas
		{4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},			  // fnorm
		{5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},			  // instance
	};
	VkDescriptorSetLayoutCreateInfo layoutInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 6, .pBindings = bindings};
	VK_CHECK(vkCreateDescriptorSetLayout(r->core.device, &layoutInfo, NULL, &r->yhrt_desc_set_layout), "Failed to create YHRT descriptor set layout");

	// Pipeline layout with push constants (28 bytes: 6 floats + 2 uint32s + 1 float)
	VkPushConstantRange pcRange = {.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0, .size = 28};
	VkPipelineLayoutCreateInfo pipelineLayoutInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1, .pSetLayouts = &r->yhrt_desc_set_layout, .pushConstantRangeCount = 1, .pPushConstantRanges = &pcRange};
	VK_CHECK(vkCreatePipelineLayout(r->core.device, &pipelineLayoutInfo, NULL, &r->yhrt_pipeline_layout), "Failed to create YHRT pipeline layout");

	// Create 3 compute pipelines
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

	VkShaderModule uiModule = VK_NULL_HANDLE;
	VK_CHECK(create_shader_module(r->core.device, YHRT_UPDATE_INSTANCES_COMP_SHADER_PATH, &uiModule), "Failed to create YHRT update instances shader module");
	VkPipelineShaderStageCreateInfo uiStage = VK_SHADER_STAGE_COMP(uiModule);
	VkComputePipelineCreateInfo uiPipeInfo = {.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, .stage = uiStage, .layout = r->yhrt_pipeline_layout};
	VK_CHECK(vkCreateComputePipelines(r->core.device, VK_NULL_HANDLE, 1, &uiPipeInfo, NULL, &r->yhrt_update_instances_pipeline), "Failed to create YHRT update instances pipeline");
	vkDestroyShaderModule(r->core.device, uiModule, NULL);

	printf("[YHRT] Pipelines initialized successfully\n");
}

// ============================================================================
// Cleanup Helper
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
}

// ============================================================================
// Start a New Layout Session
// ============================================================================

void yhrt_start(Renderer *r, igraph_t *graph, igraph_matrix_t *init_positions, igraph_int_t maxiter)
{
	if (!r->yhrt_supported) {
		fprintf(stderr, "[YHRT] Cannot start: ray tracing not supported\n");
		return;
	}

	// Clean up any previous session
	yhrt_cleanup_session_buffers(r);

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

	// Compute average edge length K
	float total_len = 0.0f;
	for (igraph_integer_t e = 0; e < ecount; e++) {
		igraph_integer_t from = IGRAPH_FROM(graph, e);
		igraph_integer_t to = IGRAPH_TO(graph, e);
		float dx = (float)(MATRIX(*init_positions, from, 0) - MATRIX(*init_positions, to, 0));
		float dy = (float)(MATRIX(*init_positions, from, 1) - MATRIX(*init_positions, to, 1));
		float dz = (ecount > 0 && igraph_matrix_ncol(init_positions) > 2) ? (float)(MATRIX(*init_positions, from, 2) - MATRIX(*init_positions, to, 2)) : 0.0f;
		total_len += sqrtf(dx * dx + dy * dy + dz * dz);
	}
	r->yhrt_K = (ecount > 0) ? (total_len / ecount) : 1.0f;
	if (r->yhrt_K < 1e-6f)
		r->yhrt_K = 1.0f;

	r->yhrt_KP = powf(r->yhrt_K, 1.0f - r->yhrt_p);
	r->yhrt_CRK = powf(IGRAPH_YHU_C, (2.0f - r->yhrt_p) / 3.0f) / r->yhrt_K;
	r->yhrt_R = 2.0f * r->yhrt_K;

	printf("[YHRT] Starting: vcount=%u ecount=%u K=%.4f R=%.4f KP=%.4f CRK=%.4f\n", r->yhrt_vcount, r->yhrt_ecount, r->yhrt_K, r->yhrt_R, r->yhrt_KP, r->yhrt_CRK);

	// ---- Upload NodeBuffer (vec4: xyz=pos, w=degree) ----
	VkDeviceSize nodeSize = sizeof(vec4) * vcount;
	yhrt_create_device_buffer(r, nodeSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &r->yhrt_node_buf, &r->yhrt_node_mem);

	// Staging buffer for initial upload
	VkBuffer nodeStaging;
	VkDeviceMemory nodeStagingMem;
	yhrt_create_buffer(r, nodeSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &nodeStaging, &nodeStagingMem);

	float *nodeData = malloc(sizeof(float) * 4 * vcount);
	for (igraph_integer_t i = 0; i < vcount; i++) {
		nodeData[i * 4 + 0] = (float)MATRIX(*init_positions, i, 0);
		nodeData[i * 4 + 1] = (float)MATRIX(*init_positions, i, 1);
		nodeData[i * 4 + 2] = (igraph_matrix_ncol(init_positions) > 2) ? (float)MATRIX(*init_positions, i, 2) : 0.0f;
		igraph_int_t deg;
		igraph_degree_1(graph, &deg, i, IGRAPH_ALL, IGRAPH_LOOPS);
		nodeData[i * 4 + 3] = (float)(deg > 0 ? deg : 1);
	}

	// Upload via staging
	{
		void *mapped;
		VK_CHECK(vkMapMemory(r->core.device, nodeStagingMem, 0, nodeSize, 0, &mapped), "Failed to map node staging");
		memcpy(mapped, nodeData, nodeSize);
		vkUnmapMemory(r->core.device, nodeStagingMem);

		VkCommandPoolCreateInfo poolInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, .queueFamilyIndex = (uint32_t)r->core.graphicsQueueFamily};
		VkCommandPool cmdPool;
		VK_CHECK(vkCreateCommandPool(r->core.device, &poolInfo, NULL, &cmdPool), "Failed to create YHRT cmd pool");
		VkCommandBufferAllocateInfo cmdInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = cmdPool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
		VkCommandBuffer cmd;
		VK_CHECK(vkAllocateCommandBuffers(r->core.device, &cmdInfo, &cmd), "Failed to allocate YHRT cmd");
		VK_CHECK(vkBeginCommandBuffer(cmd, &VK_CMD_BEGIN_INFO_ONETIME), "Failed to begin YHRT cmd");
		VkBufferCopy copyRegion = {.size = nodeSize};
		vkCmdCopyBuffer(cmd, nodeStaging, r->yhrt_node_buf, 1, &copyRegion);
		VK_CHECK(vkEndCommandBuffer(cmd), "Failed to end YHRT cmd");
		VkSubmitInfo submitInfo = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
		VK_CHECK(vkQueueSubmit(r->core.graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE), "Failed to submit YHRT copy");
		vkQueueWaitIdle(r->core.graphicsQueue);
		vkDestroyCommandPool(r->core.device, cmdPool, NULL);
	}
	VK_DESTROY_BUFFER(r->core.device, nodeStaging, nodeStagingMem);
	free(nodeData);

	// ---- ForceBuffer (zeroed) ----
	VkDeviceSize forceSize = sizeof(vec4) * vcount;
	yhrt_create_device_buffer(r, forceSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &r->yhrt_force_buf, &r->yhrt_force_mem);
	{
		VkBuffer zeroSrc;
		VkDeviceMemory zeroSrcMem;
		yhrt_create_buffer(r, forceSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &zeroSrc, &zeroSrcMem);

		VkCommandPoolCreateInfo poolInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, .queueFamilyIndex = (uint32_t)r->core.graphicsQueueFamily};
		VkCommandPool cmdPool;
		VK_CHECK(vkCreateCommandPool(r->core.device, &poolInfo, NULL, &cmdPool), "Failed to create YHRT zero cmd pool");
		VkCommandBufferAllocateInfo cmdInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = cmdPool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
		VkCommandBuffer cmd;
		VK_CHECK(vkAllocateCommandBuffers(r->core.device, &cmdInfo, &cmd), "Failed to allocate YHRT zero cmd");
		VK_CHECK(vkBeginCommandBuffer(cmd, &VK_CMD_BEGIN_INFO_ONETIME), "Failed to begin YHRT zero cmd");
		VkBufferCopy copyRegion = {.size = forceSize};
		vkCmdCopyBuffer(cmd, zeroSrc, r->yhrt_force_buf, 1, &copyRegion);
		VK_CHECK(vkEndCommandBuffer(cmd), "Failed to end YHRT zero cmd");
		VkSubmitInfo submitInfo = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
		VK_CHECK(vkQueueSubmit(r->core.graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE), "Failed to submit YHRT zero copy");
		vkQueueWaitIdle(r->core.graphicsQueue);
		vkDestroyCommandPool(r->core.device, cmdPool, NULL);
		VK_DESTROY_BUFFER(r->core.device, zeroSrc, zeroSrcMem);
	}

	// ---- EdgeBuffer (struct { uint from; uint to; float weight; } per edge) ----
	VkDeviceSize edgeStructSize = sizeof(uint32_t) * 2 * ecount;
	VkDeviceSize edgeWeightSize = sizeof(float) * ecount;
	VkDeviceSize edgeTotalSize = edgeStructSize + edgeWeightSize;
	yhrt_create_device_buffer(r, edgeTotalSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &r->yhrt_edge_buf, &r->yhrt_edge_mem);

	{
		VkBuffer edgeStaging;
		VkDeviceMemory edgeStagingMem;
		yhrt_create_buffer(r, edgeTotalSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &edgeStaging, &edgeStagingMem);

		// Pack as {uint from, uint to, float weight} per edge
		uint32_t *edgeData = malloc(edgeTotalSize);
		for (igraph_integer_t e = 0; e < ecount; e++) {
			edgeData[e * 3 + 0] = (uint32_t)IGRAPH_FROM(graph, e);
			edgeData[e * 3 + 1] = (uint32_t)IGRAPH_TO(graph, e);
			*(float *)&edgeData[e * 3 + 2] = 1.0f;
		}

		void *mapped;
		VK_CHECK(vkMapMemory(r->core.device, edgeStagingMem, 0, edgeTotalSize, 0, &mapped), "Failed to map edge staging");
		memcpy(mapped, edgeData, edgeTotalSize);
		vkUnmapMemory(r->core.device, edgeStagingMem);

		VkCommandPoolCreateInfo poolInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, .queueFamilyIndex = (uint32_t)r->core.graphicsQueueFamily};
		VkCommandPool cmdPool;
		VK_CHECK(vkCreateCommandPool(r->core.device, &poolInfo, NULL, &cmdPool), "Failed to create YHRT edge cmd pool");
		VkCommandBufferAllocateInfo cmdInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = cmdPool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
		VkCommandBuffer cmd;
		VK_CHECK(vkAllocateCommandBuffers(r->core.device, &cmdInfo, &cmd), "Failed to allocate YHRT edge cmd");
		VK_CHECK(vkBeginCommandBuffer(cmd, &VK_CMD_BEGIN_INFO_ONETIME), "Failed to begin YHRT edge cmd");
		VkBufferCopy copyRegion = {.size = edgeTotalSize};
		vkCmdCopyBuffer(cmd, edgeStaging, r->yhrt_edge_buf, 1, &copyRegion);
		VK_CHECK(vkEndCommandBuffer(cmd), "Failed to end YHRT edge cmd");
		VkSubmitInfo submitInfo = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
		VK_CHECK(vkQueueSubmit(r->core.graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE), "Failed to submit YHRT edge copy");
		vkQueueWaitIdle(r->core.graphicsQueue);
		vkDestroyCommandPool(r->core.device, cmdPool, NULL);
		VK_DESTROY_BUFFER(r->core.device, edgeStaging, edgeStagingMem);
		free(edgeData);
	}

	// ---- FnormBuffer (single float, zeroed) ----
	yhrt_create_device_buffer(r, sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, &r->yhrt_fnorm_buf, &r->yhrt_fnorm_mem);
	// ---- Fnorm staging (host visible for readback) ----
	yhrt_create_buffer(r, sizeof(float), VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, &r->yhrt_staging_buf, &r->yhrt_staging_mem);
	// Zero fnorm
	{
		float zero = 0.0f;
		void *mapped;
		VK_CHECK(vkMapMemory(r->core.device, r->yhrt_staging_mem, 0, sizeof(float), 0, &mapped), "Failed to map fnorm staging");
		memcpy(mapped, &zero, sizeof(float));
		vkUnmapMemory(r->core.device, r->yhrt_staging_mem);

		VkCommandPoolCreateInfo poolInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, .queueFamilyIndex = (uint32_t)r->core.graphicsQueueFamily};
		VkCommandPool cmdPool;
		VK_CHECK(vkCreateCommandPool(r->core.device, &poolInfo, NULL, &cmdPool), "Failed to create YHRT fnorm cmd pool");
		VkCommandBufferAllocateInfo cmdInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = cmdPool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
		VkCommandBuffer cmd;
		VK_CHECK(vkAllocateCommandBuffers(r->core.device, &cmdInfo, &cmd), "Failed to allocate YHRT fnorm cmd");
		VK_CHECK(vkBeginCommandBuffer(cmd, &VK_CMD_BEGIN_INFO_ONETIME), "Failed to begin YHRT fnorm cmd");
		VkBufferCopy copyRegion = {.size = sizeof(float)};
		vkCmdCopyBuffer(cmd, r->yhrt_staging_buf, r->yhrt_fnorm_buf, 1, &copyRegion);
		VK_CHECK(vkEndCommandBuffer(cmd), "Failed to end YHRT fnorm cmd");
		VkSubmitInfo submitInfo = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
		VK_CHECK(vkQueueSubmit(r->core.graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE), "Failed to submit YHRT fnorm zero");
		vkQueueWaitIdle(r->core.graphicsQueue);
		vkDestroyCommandPool(r->core.device, cmdPool, NULL);
	}

	// ---- Dispatch fence (serializes GPU execution for safe staging readback) ----
	{
		VkFenceCreateInfo fenceInfo = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT};
		VK_CHECK(vkCreateFence(r->core.device, &fenceInfo, NULL, &r->yhrt_dispatch_fence), "Failed to create YHRT dispatch fence");
	}

	// ---- Build BLAS (single AABB geometry) ----
	{
		float R = r->yhrt_R;
		VkAabbPositionsKHR aabb = {.minX = -R, .minY = -R, .minZ = -R, .maxX = R, .maxY = R, .maxZ = R};

		// Upload AABB data
		VkBuffer aabbBuf;
		VkDeviceMemory aabbMem;
		yhrt_create_buffer(r, sizeof(VkAabbPositionsKHR), VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, &aabbBuf, &aabbMem);
		void *mapped;
		VK_CHECK(vkMapMemory(r->core.device, aabbMem, 0, sizeof(VkAabbPositionsKHR), 0, &mapped), "Failed to map AABB data");
		memcpy(mapped, &aabb, sizeof(aabb));
		vkUnmapMemory(r->core.device, aabbMem);

		uint64_t aabbAddr = yhrt_get_buffer_address(r, aabbBuf);
		VkAccelerationStructureGeometryKHR blasGeometry = {
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
			.geometryType = VK_GEOMETRY_TYPE_AABBS_KHR,
			.flags = VK_GEOMETRY_OPAQUE_BIT_KHR,
			.geometry.aabbs = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR, .data.deviceAddress = aabbAddr, .stride = sizeof(VkAabbPositionsKHR)},
		};
		VkAccelerationStructureBuildGeometryInfoKHR buildInfo = {
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
			.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
			.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR,
			.geometryCount = 1,
			.pGeometries = &blasGeometry,
		};
		uint32_t primitiveCount = 1;
		VkAccelerationStructureBuildSizesInfoKHR sizeInfo = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
		rt_funcs.GetAccelerationStructureBuildSizesKHR(r->core.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &primitiveCount, &sizeInfo);

		yhrt_create_device_buffer(r, sizeInfo.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, &r->yhrt_blas_buf, &r->yhrt_blas_mem);
		yhrt_create_device_buffer(r, sizeInfo.buildScratchSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, &r->yhrt_as_scratch_buf, &r->yhrt_as_scratch_mem);

		VkAccelerationStructureCreateInfoKHR asCreateInfo = {
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
			.buffer = r->yhrt_blas_buf,
			.size = sizeInfo.accelerationStructureSize,
			.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
		};
		VK_CHECK(rt_funcs.CreateAccelerationStructureKHR(r->core.device, &asCreateInfo, NULL, &r->yhrt_blas), "Failed to create BLAS");

		buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
		buildInfo.dstAccelerationStructure = r->yhrt_blas;
		buildInfo.scratchData.deviceAddress = yhrt_get_buffer_address(r, r->yhrt_as_scratch_buf);

		VkAccelerationStructureBuildRangeInfoKHR rangeInfo = {.primitiveCount = 1};
		const VkAccelerationStructureBuildRangeInfoKHR *pRangeInfo = &rangeInfo;

		VkCommandPoolCreateInfo poolInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, .queueFamilyIndex = (uint32_t)r->core.graphicsQueueFamily};
		VkCommandPool cmdPool;
		VK_CHECK(vkCreateCommandPool(r->core.device, &poolInfo, NULL, &cmdPool), "Failed to create BLAS cmd pool");
		VkCommandBufferAllocateInfo cmdInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = cmdPool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
		VkCommandBuffer blasCmd;
		VK_CHECK(vkAllocateCommandBuffers(r->core.device, &cmdInfo, &blasCmd), "Failed to allocate BLAS cmd");
		VK_CHECK(vkBeginCommandBuffer(blasCmd, &VK_CMD_BEGIN_INFO_ONETIME), "Failed to begin BLAS cmd");

		VkMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = 0, .dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR};
		vkCmdPipelineBarrier(blasCmd, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1, &barrier, 0, NULL, 0, NULL);

		rt_funcs.CmdBuildAccelerationStructuresKHR(blasCmd, 1, &buildInfo, &pRangeInfo);

		barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
		barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
		vkCmdPipelineBarrier(blasCmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, NULL, 0, NULL);

		VK_CHECK(vkEndCommandBuffer(blasCmd), "Failed to end BLAS cmd");
		VkSubmitInfo submitInfo = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &blasCmd};
		VK_CHECK(vkQueueSubmit(r->core.graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE), "Failed to submit BLAS build");
		vkQueueWaitIdle(r->core.graphicsQueue);
		vkDestroyCommandPool(r->core.device, cmdPool, NULL);
		VK_DESTROY_BUFFER(r->core.device, aabbBuf, aabbMem);
	}

	// ---- Build TLAS (N instances of BLAS) ----
	{
		// Create instance buffer
		VkDeviceSize instanceSize = sizeof(VkAccelerationStructureInstanceKHR) * vcount;
		yhrt_create_device_buffer(r, instanceSize, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &r->yhrt_instance_buf, &r->yhrt_instance_mem);

		// Upload initial instances via staging
		VkBuffer instStaging;
		VkDeviceMemory instStagingMem;
		yhrt_create_buffer(r, instanceSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &instStaging, &instStagingMem);

		VkAccelerationStructureInstanceKHR *instances = malloc(instanceSize);
		uint64_t blasAddr = yhrt_get_as_address(r, r->yhrt_blas);

		for (igraph_integer_t i = 0; i < vcount; i++) {
			float x = (float)MATRIX(*init_positions, i, 0);
			float y = (float)MATRIX(*init_positions, i, 1);
			float z = (igraph_matrix_ncol(init_positions) > 2) ? (float)MATRIX(*init_positions, i, 2) : 0.0f;

			memset(&instances[i], 0, sizeof(VkAccelerationStructureInstanceKHR));
			instances[i].transform.matrix[0][0] = 1.0f;
			instances[i].transform.matrix[1][1] = 1.0f;
			instances[i].transform.matrix[2][2] = 1.0f;
			instances[i].transform.matrix[0][3] = x;
			instances[i].transform.matrix[1][3] = y;
			instances[i].transform.matrix[2][3] = z;
			instances[i].instanceCustomIndex = (uint32_t)i;
			instances[i].mask = 0xFF;
			instances[i].instanceShaderBindingTableRecordOffset = 0;
			instances[i].flags = VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;
			instances[i].accelerationStructureReference = blasAddr;
		}

		void *mapped;
		VK_CHECK(vkMapMemory(r->core.device, instStagingMem, 0, instanceSize, 0, &mapped), "Failed to map instance staging");
		memcpy(mapped, instances, instanceSize);
		vkUnmapMemory(r->core.device, instStagingMem);

		VkCommandPoolCreateInfo poolInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, .queueFamilyIndex = (uint32_t)r->core.graphicsQueueFamily};
		VkCommandPool cmdPool;
		VK_CHECK(vkCreateCommandPool(r->core.device, &poolInfo, NULL, &cmdPool), "Failed to create TLAS cmd pool");
		VkCommandBufferAllocateInfo cmdInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = cmdPool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
		VkCommandBuffer tlasCmd;
		VK_CHECK(vkAllocateCommandBuffers(r->core.device, &cmdInfo, &tlasCmd), "Failed to allocate TLAS cmd");
		VK_CHECK(vkBeginCommandBuffer(tlasCmd, &VK_CMD_BEGIN_INFO_ONETIME), "Failed to begin TLAS cmd");

		// Copy instance data
		VkBufferCopy copyRegion = {.size = instanceSize};
		vkCmdCopyBuffer(tlasCmd, instStaging, r->yhrt_instance_buf, 1, &copyRegion);

		VkMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR};
		vkCmdPipelineBarrier(tlasCmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1, &barrier, 0, NULL, 0, NULL);

		// Build TLAS
		uint64_t instAddr = yhrt_get_buffer_address(r, r->yhrt_instance_buf);
		VkAccelerationStructureGeometryKHR tlasGeometry = {
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
			.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
			.flags = VK_GEOMETRY_OPAQUE_BIT_KHR,
			.geometry.instances = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR, .data.deviceAddress = instAddr},
		};
		VkAccelerationStructureBuildGeometryInfoKHR tlasBuildInfo = {
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
			.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
			.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR,
			.geometryCount = 1,
			.pGeometries = &tlasGeometry,
		};
		uint32_t instCount = (uint32_t)vcount;
		VkAccelerationStructureBuildSizesInfoKHR tlasSizeInfo = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
		rt_funcs.GetAccelerationStructureBuildSizesKHR(r->core.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &tlasBuildInfo, &instCount, &tlasSizeInfo);

		yhrt_create_device_buffer(r, tlasSizeInfo.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, &r->yhrt_tlas_buf, &r->yhrt_tlas_mem);

		// Reuse as_scratch or allocate new
		if (tlasSizeInfo.buildScratchSize > 0) {
			// Need scratch for TLAS - allocate if bigger than BLAS scratch
			VK_DESTROY_BUFFER(r->core.device, r->yhrt_as_scratch_buf, r->yhrt_as_scratch_mem);
			yhrt_create_device_buffer(r, tlasSizeInfo.buildScratchSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, &r->yhrt_as_scratch_buf, &r->yhrt_as_scratch_mem);
		}

		VkAccelerationStructureCreateInfoKHR asCreateInfo = {
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
			.buffer = r->yhrt_tlas_buf,
			.size = tlasSizeInfo.accelerationStructureSize,
			.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
		};
		VK_CHECK(rt_funcs.CreateAccelerationStructureKHR(r->core.device, &asCreateInfo, NULL, &r->yhrt_tlas), "Failed to create TLAS");

		tlasBuildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
		tlasBuildInfo.dstAccelerationStructure = r->yhrt_tlas;
		tlasBuildInfo.scratchData.deviceAddress = yhrt_get_buffer_address(r, r->yhrt_as_scratch_buf);

		VkAccelerationStructureBuildRangeInfoKHR tlasRangeInfo = {.primitiveCount = (uint32_t)vcount};
		const VkAccelerationStructureBuildRangeInfoKHR *pTlasRange = &tlasRangeInfo;
		rt_funcs.CmdBuildAccelerationStructuresKHR(tlasCmd, 1, &tlasBuildInfo, &pTlasRange);

		barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
		barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT;
		vkCmdPipelineBarrier(tlasCmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, NULL, 0, NULL);

		VK_CHECK(vkEndCommandBuffer(tlasCmd), "Failed to end TLAS cmd");
		VkSubmitInfo submitInfo = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &tlasCmd};
		VK_CHECK(vkQueueSubmit(r->core.graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE), "Failed to submit TLAS build");
		vkQueueWaitIdle(r->core.graphicsQueue);
		vkDestroyCommandPool(r->core.device, cmdPool, NULL);
		VK_DESTROY_BUFFER(r->core.device, instStaging, instStagingMem);
		free(instances);
	}

	// ---- Descriptor Pool + Set ----
	{
		VkDescriptorPoolSize poolSizes[] = {
			{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 5},
			{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1},
		};
		VkDescriptorPoolCreateInfo poolInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 1, .poolSizeCount = 2, .pPoolSizes = poolSizes};
		VK_CHECK(vkCreateDescriptorPool(r->core.device, &poolInfo, NULL, &r->yhrt_desc_pool), "Failed to create YHRT descriptor pool");

		VkDescriptorSetAllocateInfo setInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = r->yhrt_desc_pool, .descriptorSetCount = 1, .pSetLayouts = &r->yhrt_desc_set_layout};
		VK_CHECK(vkAllocateDescriptorSets(r->core.device, &setInfo, &r->yhrt_desc_set), "Failed to allocate YHRT descriptor set");

		VkDescriptorBufferInfo nodeInfo = {r->yhrt_node_buf, 0, VK_WHOLE_SIZE};
		VkDescriptorBufferInfo forceInfo = {r->yhrt_force_buf, 0, VK_WHOLE_SIZE};
		VkDescriptorBufferInfo edgeInfo = {r->yhrt_edge_buf, 0, VK_WHOLE_SIZE};
		VkDescriptorBufferInfo fnormInfo = {r->yhrt_fnorm_buf, 0, VK_WHOLE_SIZE};
		VkDescriptorBufferInfo instanceInfo = {r->yhrt_instance_buf, 0, VK_WHOLE_SIZE};
		VkWriteDescriptorSet writes[6] = {
			{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, r->yhrt_desc_set, 0, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, NULL, &nodeInfo, NULL}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, r->yhrt_desc_set, 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, NULL, &forceInfo, NULL}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, r->yhrt_desc_set, 2, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, NULL, &edgeInfo, NULL}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, r->yhrt_desc_set, 4, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, NULL, &fnormInfo, NULL}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, r->yhrt_desc_set, 5, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, NULL, &instanceInfo, NULL},
		};
		vkUpdateDescriptorSets(r->core.device, 5, writes, 0, NULL);

		// TLAS descriptor write
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
	printf("[YHRT] Session started, %d iterations planned\n", r->yhrt_maxiter);
}

// ============================================================================
// Per-Frame Dispatch
// ============================================================================

bool yhrt_dispatch_step(Renderer *r, VkCommandBuffer cmd)
{
	if (!r->yhrt_active || !r->yhrt_supported)
		return false;

	if (r->yhrt_current_iter >= r->yhrt_maxiter)
		return false;

	// ---- Wait for previous dispatch, then read back Fnorm for adaptive cooling ----
	VK_CHECK(vkWaitForFences(r->core.device, 1, &r->yhrt_dispatch_fence, VK_TRUE, UINT64_MAX), "Failed to wait for YHRT dispatch fence");
	VK_CHECK(vkResetFences(r->core.device, 1, &r->yhrt_dispatch_fence), "Failed to reset YHRT dispatch fence");

	if (r->yhrt_current_iter > 0) {
		float fnorm = 0.0f;
		void *mapped;
		if (vkMapMemory(r->core.device, r->yhrt_staging_mem, 0, sizeof(float), 0, &mapped) == VK_SUCCESS) {
			fnorm = *(float *)mapped;
			vkUnmapMemory(r->core.device, r->yhrt_staging_mem);
		}

		printf("[YHRT] iter=%d, step=%g, Fnorm=%g, Fnorm0=%g, repulsive_exp=%g, natlen=%g\n", r->yhrt_current_iter - 1, r->yhrt_step, fnorm, r->yhrt_Fnorm0, r->yhrt_p, r->yhrt_K);

		if (fnorm < r->yhrt_Fnorm0) {
			if (fnorm > 0.95f * r->yhrt_Fnorm0) {
				// step unchanged
			} else {
				r->yhrt_step *= 0.99f / IGRAPH_YHU_COOL;
			}
		} else {
			r->yhrt_step *= IGRAPH_YHU_COOL;
		}
		r->yhrt_Fnorm0 = fnorm;
	}

	// ---- Reset Fnorm via GPU fill (avoids CPU/GPU race on staging buffer) ----
	vkCmdFillBuffer(cmd, r->yhrt_fnorm_buf, 0, sizeof(float), 0);
	VkMemoryBarrier clrBarrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &clrBarrier, 0, NULL, 0, NULL);

	// ---- Dispatch repulsion ----
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, r->yhrt_repulsion_pipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, r->yhrt_pipeline_layout, 0, 1, &r->yhrt_desc_set, 0, NULL);
	struct
	{
		float KP, CRK, p, step_size;
		uint32_t vertex_count, edge_count;
		float R;
	} pc = {r->yhrt_KP, r->yhrt_CRK, r->yhrt_p, r->yhrt_step, r->yhrt_vcount, r->yhrt_ecount, r->yhrt_R};
	vkCmdPushConstants(cmd, r->yhrt_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 28, &pc);
	vkCmdDispatch(cmd, (r->yhrt_vcount + YHRT_WORKGROUP_SIZE - 1) / YHRT_WORKGROUP_SIZE, 1, 1);

	// ---- Barrier repulsion -> attraction ----
	VkMemoryBarrier repBarrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_SHADER_READ_BIT};
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &repBarrier, 0, NULL, 0, NULL);

	// ---- Dispatch attraction ----
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, r->yhrt_attraction_pipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, r->yhrt_pipeline_layout, 0, 1, &r->yhrt_desc_set, 0, NULL);
	vkCmdPushConstants(cmd, r->yhrt_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 28, &pc);
	vkCmdDispatch(cmd, (r->yhrt_ecount + YHRT_WORKGROUP_SIZE - 1) / YHRT_WORKGROUP_SIZE, 1, 1);

	// ---- Barrier attraction -> update ----
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &repBarrier, 0, NULL, 0, NULL);

	// ---- Dispatch update ----
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, r->yhrt_update_pipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, r->yhrt_pipeline_layout, 0, 1, &r->yhrt_desc_set, 0, NULL);
	vkCmdPushConstants(cmd, r->yhrt_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 28, &pc);
	vkCmdDispatch(cmd, (r->yhrt_vcount + YHRT_WORKGROUP_SIZE - 1) / YHRT_WORKGROUP_SIZE, 1, 1);

	// ---- Barrier update -> instance update ----
	VkMemoryBarrier updBarrier2 = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_SHADER_READ_BIT};
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &updBarrier2, 0, NULL, 0, NULL);

	// ---- Dispatch instance update (copies node positions to TLAS instance transforms) ----
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, r->yhrt_update_instances_pipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, r->yhrt_pipeline_layout, 0, 1, &r->yhrt_desc_set, 0, NULL);
	vkCmdPushConstants(cmd, r->yhrt_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 28, &pc);
	vkCmdDispatch(cmd, (r->yhrt_vcount + YHRT_WORKGROUP_SIZE - 1) / YHRT_WORKGROUP_SIZE, 1, 1);

	// ---- Barrier instance update -> TLAS build ----
	VkMemoryBarrier instBarrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_SHADER_READ_BIT};
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &instBarrier, 0, NULL, 0, NULL);

	// ---- Update TLAS with new instance transforms ----
	{
		uint32_t instCount = r->yhrt_vcount;
		uint64_t instAddr = yhrt_get_buffer_address(r, r->yhrt_instance_buf);

		VkAccelerationStructureGeometryKHR tlasGeometry = {
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
			.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
			.flags = VK_GEOMETRY_OPAQUE_BIT_KHR,
			.geometry.instances = {.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR, .data.deviceAddress = instAddr},
		};
		VkAccelerationStructureBuildGeometryInfoKHR tlasBuildInfo = {
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
			.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
			.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR,
			.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR,
			.srcAccelerationStructure = r->yhrt_tlas,
			.dstAccelerationStructure = r->yhrt_tlas,
			.geometryCount = 1,
			.pGeometries = &tlasGeometry,
			.scratchData.deviceAddress = yhrt_get_buffer_address(r, r->yhrt_as_scratch_buf),
		};
		VkAccelerationStructureBuildRangeInfoKHR tlasRangeInfo = {.primitiveCount = instCount};
		const VkAccelerationStructureBuildRangeInfoKHR *pTlasRange = &tlasRangeInfo;
		rt_funcs.CmdBuildAccelerationStructuresKHR(cmd, 1, &tlasBuildInfo, &pTlasRange);
	}

	// ---- Barrier TLAS build -> compute ----
	VkMemoryBarrier tlasBarrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR, .dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT};
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &tlasBarrier, 0, NULL, 0, NULL);

	// Periodic Fnorm readback
	r->yhrt_current_iter++;
	if (r->yhrt_current_iter % YHRT_FNORM_READBACK_INTERVAL == 0 || r->yhrt_current_iter >= r->yhrt_maxiter) {
		VkBufferCopy fnormCopy = {.size = sizeof(float)};
		vkCmdCopyBuffer(cmd, r->yhrt_fnorm_buf, r->yhrt_staging_buf, 1, &fnormCopy);
		VkMemoryBarrier readBarrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT};
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &readBarrier, 0, NULL, 0, NULL);
		r->yhrt_fnorm_readback_pending = true;
	}

	return true;
}

// ============================================================================
// Finish and Apply Results
// ============================================================================

void yhrt_finish(Renderer *r, GraphData *graph)
{
	if (!r->yhrt_active)
		return;

	vkQueueWaitIdle(r->core.graphicsQueue);

	// Wait for yhrt dispatch fence if a dispatch was in-flight
	if (r->yhrt_dispatch_fence != VK_NULL_HANDLE)
		vkWaitForFences(r->core.device, 1, &r->yhrt_dispatch_fence, VK_TRUE, UINT64_MAX);

	// Read back final Fnorm if pending (last iteration's value)
	if (r->yhrt_fnorm_readback_pending) {
		void *mapped;
		if (vkMapMemory(r->core.device, r->yhrt_staging_mem, 0, sizeof(float), 0, &mapped) == VK_SUCCESS) {
			float fnorm = *(float *)mapped;
			vkUnmapMemory(r->core.device, r->yhrt_staging_mem);

			printf("[YHRT] iter=%d (final), step=%g, Fnorm=%g, Fnorm0=%g, repulsive_exp=%g, natlen=%g\n", r->yhrt_current_iter - 1, r->yhrt_step, fnorm, r->yhrt_Fnorm0, r->yhrt_p, r->yhrt_K);
		}
		r->yhrt_fnorm_readback_pending = false;
	}

	// Read back node positions
	VkDeviceSize nodeSize = sizeof(vec4) * r->yhrt_vcount;
	VkBuffer readbackBuf;
	VkDeviceMemory readbackMem;
	yhrt_create_buffer(r, nodeSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &readbackBuf, &readbackMem);

	{
		VkCommandPoolCreateInfo poolInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, .queueFamilyIndex = (uint32_t)r->core.graphicsQueueFamily};
		VkCommandPool cmdPool;
		VK_CHECK(vkCreateCommandPool(r->core.device, &poolInfo, NULL, &cmdPool), "Failed to create readback cmd pool");
		VkCommandBufferAllocateInfo cmdInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = cmdPool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
		VkCommandBuffer cmd;
		VK_CHECK(vkAllocateCommandBuffers(r->core.device, &cmdInfo, &cmd), "Failed to allocate readback cmd");
		VK_CHECK(vkBeginCommandBuffer(cmd, &VK_CMD_BEGIN_INFO_ONETIME), "Failed to begin readback cmd");

		VkBufferCopy copyRegion = {.size = nodeSize};
		vkCmdCopyBuffer(cmd, r->yhrt_node_buf, readbackBuf, 1, &copyRegion);

		VK_CHECK(vkEndCommandBuffer(cmd), "Failed to end readback cmd");
		VkSubmitInfo submitInfo = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
		VK_CHECK(vkQueueSubmit(r->core.graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE), "Failed to submit readback");
		vkQueueWaitIdle(r->core.graphicsQueue);
		vkDestroyCommandPool(r->core.device, cmdPool, NULL);
	}

	void *mapped;
	VK_CHECK(vkMapMemory(r->core.device, readbackMem, 0, nodeSize, 0, &mapped), "Failed to map readback");
	float *positions = malloc(sizeof(float) * 4 * r->yhrt_vcount);
	memcpy(positions, mapped, nodeSize);
	vkUnmapMemory(r->core.device, readbackMem);
	VK_DESTROY_BUFFER(r->core.device, readbackBuf, readbackMem);

	// Apply to GraphData
	if (graph && graph->nodes) {
		igraph_matrix_destroy(&graph->current_layout);
		igraph_matrix_init(&graph->current_layout, r->yhrt_vcount, 3);

		for (uint32_t i = 0; i < r->yhrt_vcount; i++) {
			graph->nodes[i].position[0] = positions[i * 4 + 0];
			graph->nodes[i].position[1] = positions[i * 4 + 1];
			graph->nodes[i].position[2] = positions[i * 4 + 2];
			MATRIX(graph->current_layout, i, 0) = positions[i * 4 + 0];
			MATRIX(graph->current_layout, i, 1) = positions[i * 4 + 1];
			MATRIX(graph->current_layout, i, 2) = positions[i * 4 + 2];
		}
	}
	free(positions);

	// Update Vulkan vertex buffers with new positions
	if (graph)
		renderer_update_graph(r, graph);

	// Cleanup GPU resources
	yhrt_cleanup_session_buffers(r);
	r->yhrt_active = false;
	printf("[YHRT] Layout completed after %d iterations, final step=%.6f\n", r->yhrt_current_iter, r->yhrt_step);
}

// ============================================================================
// Full Cleanup (called at renderer shutdown)
// ============================================================================

void yhrt_destroy(Renderer *r)
{
	yhrt_cleanup_session_buffers(r);

	if (r->yhrt_update_pipeline != VK_NULL_HANDLE)
		vkDestroyPipeline(r->core.device, r->yhrt_update_pipeline, NULL);
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
