// =============================================================================
// YHRT — Yu Hu Ray-Traced Force-Directed Graph Layout (GPU)
// =============================================================================
//
// Implements a GPU-accelerated force-directed graph layout using Vulkan compute
// shaders and ray-tracing acceleration structures. The algorithm follows the
// Yu Hu (YHu) model (igraph's layout_yhu_3d), where each iteration consists of
// three compute passes:
//
//   1. REPULSION   (bh_force.comp)  — O(N log N) Barnes-Hut via rayQueryEXT
//   2. ATTRACTION  (yh_attraction.comp) — edge-based spring forces
//   3. UPDATE      (yh_update.comp)     — position integration + Fnorm reduction
//
// Repulsion uses the decoupled BarnesHutRT module (rt_barnes_hut.c) which
// builds an octree on CPU, linearizes it to a triangle-mesh BLAS, and
// dispatches ray queries against it. The BH module manages its own BLAS/TLAS.
//
// Attraction, update, and cooling schedule remain as in the original YHRT
// implementation.
//
// =============================================================================

#include "vulkan/rt_layout.h"

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vulkan/buffers.h"
#include "vulkan/renderer.h"
#include "vulkan/rt_barnes_hut.h"
#include "vulkan/rt_helpers.h"
#include "vulkan/utils.h"

#define YHRT_WORKGROUP_SIZE 256

// YHu constants matching igraph's layout_yhu_3d implementation.
// IGRAPH_YHU_C controls attraction/repulsion balance; IGRAPH_YHU_COOL is the
// cooling factor applied when Fnorm increases (step *= COOL) or decreases
// slowly (step *= 0.99/COOL to allow slight growth).
#define IGRAPH_YHU_C 0.2
#define IGRAPH_YHU_COOL 0.90

#define YHRT_DISPATCH_COMPUTE(cmd, pipeline, layout, descSet, pc, count) \
	do { \
		vkCmdBindPipeline((cmd), VK_PIPELINE_BIND_POINT_COMPUTE, (pipeline)); \
		vkCmdBindDescriptorSets((cmd), VK_PIPELINE_BIND_POINT_COMPUTE, (layout), 0, 1, &(descSet), 0, NULL); \
		vkCmdPushConstants((cmd), (layout), VK_SHADER_STAGE_COMPUTE_BIT, 0, 28, &(pc)); \
		vkCmdDispatch((cmd), (count), 1, 1); \
	} while (0)

// ============================================================================
// Algorithm Buffer Cleanup
// ============================================================================

static void yhrt_cleanup_algo_buffers(Renderer *r)
{
	VK_DESTROY_BUFFER(r->core.device, r->yhrt_node_buf, r->yhrt_node_mem);
	VK_DESTROY_BUFFER(r->core.device, r->yhrt_force_buf, r->yhrt_force_mem);
	VK_DESTROY_BUFFER(r->core.device, r->yhrt_edge_buf, r->yhrt_edge_mem);
	VK_DESTROY_BUFFER(r->core.device, r->yhrt_fnorm_buf, r->yhrt_fnorm_mem);
	VK_DESTROY_BUFFER(r->core.device, r->yhrt_staging_buf, r->yhrt_staging_mem);
	VK_DESTROY_BUFFER(r->core.device, r->yhrt_pos_staging_buf, r->yhrt_pos_staging_mem);

	if (r->yhrt_desc_pool != VK_NULL_HANDLE) {
		vkDestroyDescriptorPool(r->core.device, r->yhrt_desc_pool, NULL);
		r->yhrt_desc_pool = VK_NULL_HANDLE;
	}
	if (r->yhrt_cmd_pool != VK_NULL_HANDLE) {
		vkDestroyCommandPool(r->core.device, r->yhrt_cmd_pool, NULL);
		r->yhrt_cmd_pool = VK_NULL_HANDLE;
	}
	if (r->yhrt_dispatch_fence != VK_NULL_HANDLE) {
		vkDestroyFence(r->core.device, r->yhrt_dispatch_fence, NULL);
		r->yhrt_dispatch_fence = VK_NULL_HANDLE;
	}

	free(r->yhrt_cpu_positions);
	r->yhrt_cpu_positions = NULL;
}

// ============================================================================
// Pipeline Initialization (called once)
// ============================================================================

void yhrt_init_pipelines(Renderer *r)
{
	r->yhrt_active = false;
	r->yhrt_fp64_supported = false;
	r->yhrt_desc_set_layout = VK_NULL_HANDLE;
	r->yhrt_pipeline_layout = VK_NULL_HANDLE;
	r->yhrt_attraction_pipeline = VK_NULL_HANDLE;
	r->yhrt_update_pipeline = VK_NULL_HANDLE;
	r->yhrt_update_fp64_pipeline = VK_NULL_HANDLE;
	r->yhrt_desc_pool = VK_NULL_HANDLE;

	// Descriptor Set Layout — shared by YHRT compute pipelines (attraction + update)
	// Created unconditionally; bhrt support is checked at worker init.
	//
	// Binding  Type                          Count  Stage
	// ------  ----------------------------  -----  ------
	//   0     STORAGE_BUFFER (NodeBuffer)     1    COMPUTE
	//   1     STORAGE_BUFFER (ForceBuffer)    1    COMPUTE  (shared with BH module)
	//   2     STORAGE_BUFFER (EdgeBuffer)     1    COMPUTE
	//   4     STORAGE_BUFFER (FnormBuffer)    1    COMPUTE
	//
	// Gaps at 3,5 preserve original binding numbers so yh_attraction.comp and
	// yh_update.comp do not require shader modifications.
	//
	// Push constants: 28 bytes = 5 floats + 2 uints + 1 float
	//   [KP, CRK, p, step_size, vertex_count, edge_count, R]
	VkDescriptorSetLayoutBinding bindings[4] = {
		{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
		{1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
		{2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
		{4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
	};
	VkDescriptorSetLayoutCreateInfo layoutInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 4, .pBindings = bindings};
	VK_CHECK(vkCreateDescriptorSetLayout(r->core.device, &layoutInfo, NULL, &r->yhrt_desc_set_layout), "Failed to create YHRT descriptor set layout");

	VkPushConstantRange pcRange = {.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0, .size = 28};
	VkPipelineLayoutCreateInfo pipelineLayoutInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1, .pSetLayouts = &r->yhrt_desc_set_layout, .pushConstantRangeCount = 1, .pPushConstantRanges = &pcRange};
	VK_CHECK(vkCreatePipelineLayout(r->core.device, &pipelineLayoutInfo, NULL, &r->yhrt_pipeline_layout), "Failed to create YHRT pipeline layout");

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

	printf("[YHRT] Pipelines initialized successfully\n");
}

// ============================================================================
// Worker-Thread API: Blocking GPU iterations with igraph_progress
// ============================================================================

bool yhrt_worker_init(Renderer *r, igraph_t *graph, igraph_matrix_t *init_positions, igraph_int_t maxiter)
{
	if (!r->bhrt || !bhrt_is_supported(r->bhrt)) {
		fprintf(stderr, "[YHRT] Cannot start: Barnes-Hut RT not supported\n");
		return false;
	}

	yhrt_cleanup_algo_buffers(r);

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
	r->yhrt_R = r->yhrt_K * powf((float)r->yhrt_vcount, 1.0f / 4.0f);

	printf("[YHRT] Starting: vcount=%u ecount=%u K=%.4f R=%.4f KP=%.4f CRK=%.4f\n", r->yhrt_vcount, r->yhrt_ecount, r->yhrt_K, r->yhrt_R, r->yhrt_KP, r->yhrt_CRK);

	// ---- Allocate and initialize CPU positions array ----
	r->yhrt_cpu_positions = (float *)malloc(sizeof(float) * 3 * vcount);
	for (igraph_integer_t i = 0; i < vcount; i++) {
		r->yhrt_cpu_positions[i * 3 + 0] = (float)MATRIX(*init_positions, i, 0);
		r->yhrt_cpu_positions[i * 3 + 1] = (float)MATRIX(*init_positions, i, 1);
		r->yhrt_cpu_positions[i * 3 + 2] = (igraph_matrix_ncol(init_positions) > 2) ? (float)MATRIX(*init_positions, i, 2) : 0.0f;
	}

	// ---- Upload NodeBuffer ----
	VkDeviceSize nodeSize = sizeof(vec4) * vcount;
	rt_helpers_create_device_buffer(&r->core, nodeSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, &r->yhrt_node_buf, &r->yhrt_node_mem);

	float *nodeData = malloc(sizeof(float) * 4 * vcount);
	for (igraph_integer_t i = 0; i < vcount; i++) {
		nodeData[i * 4 + 0] = (float)MATRIX(*init_positions, i, 0);
		nodeData[i * 4 + 1] = (float)MATRIX(*init_positions, i, 1);
		nodeData[i * 4 + 2] = (igraph_matrix_ncol(init_positions) > 2) ? (float)MATRIX(*init_positions, i, 2) : 0.0f;
		igraph_int_t deg;
		igraph_degree_1(graph, &deg, i, IGRAPH_ALL, IGRAPH_LOOPS);
		nodeData[i * 4 + 3] = 1.0f;
	}
	printf("[YHRT] Node masses (first 10):");
	for (igraph_integer_t i = 0; i < 10 && i < vcount; i++)
		printf(" %.1f", nodeData[i * 4 + 3]);
	printf("\n");
	printf("[YHRT] Node positions + degree (first 3):\n");
	for (igraph_integer_t i = 0; i < 3 && i < vcount; i++)
		printf("  node[%ld] pos=(%.4f, %.4f, %.4f) deg=%.0f\n", (long)i, nodeData[i * 4 + 0], nodeData[i * 4 + 1], nodeData[i * 4 + 2], nodeData[i * 4 + 3]);

	rt_helpers_staging_upload(&r->core, r->yhrt_node_buf, nodeData, nodeSize);
	free(nodeData);

	// ---- Initialize BH session ----
	{
		float *bh_positions = (float *)malloc(sizeof(float) * 3 * vcount);
		for (igraph_integer_t i = 0; i < vcount; i++) {
			bh_positions[i * 3 + 0] = (float)MATRIX(*init_positions, i, 0);
			bh_positions[i * 3 + 1] = (float)MATRIX(*init_positions, i, 1);
			bh_positions[i * 3 + 2] = (igraph_matrix_ncol(init_positions) > 2) ? (float)MATRIX(*init_positions, i, 2) : 0.0f;
		}
		if (!bhrt_session_init(r->bhrt, bh_positions, NULL, (uint32_t)vcount, 0.5f, 1.0f)) {
			fprintf(stderr, "[YHRT] BH session init failed\n");
			free(bh_positions);
			return false;
		}
		free(bh_positions);
	}

	// ---- Fnorm + dispatch fence ----
	rt_helpers_create_device_buffer(&r->core, sizeof(double), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, &r->yhrt_fnorm_buf, &r->yhrt_fnorm_mem);

	rt_helpers_create_buffer(&r->core, sizeof(double), VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, &r->yhrt_staging_buf, &r->yhrt_staging_mem);

	// ---- Position staging buffer (GPU→CPU readback every iteration) ----
	rt_helpers_create_buffer(&r->core, sizeof(vec4) * vcount, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &r->yhrt_pos_staging_buf, &r->yhrt_pos_staging_mem);

	// ---- Command pool + buffer (per-iteration GPU work) ----
	VkCommandPoolCreateInfo cmdPoolInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = (uint32_t)r->core.graphicsQueueFamily};
	VK_CHECK(vkCreateCommandPool(r->core.device, &cmdPoolInfo, NULL, &r->yhrt_cmd_pool), "Failed to create YHRT command pool");
	VkCommandBufferAllocateInfo cmdBufInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = r->yhrt_cmd_pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
	VK_CHECK(vkAllocateCommandBuffers(r->core.device, &cmdBufInfo, &r->yhrt_cmd_buf), "Failed to allocate YHRT command buffer");

	{
		VkFenceCreateInfo fenceInfo = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT};
		VK_CHECK(vkCreateFence(r->core.device, &fenceInfo, NULL, &r->yhrt_dispatch_fence), "Failed to create YHRT dispatch fence");
	}

	// ---- Edge buffer ----
	{
		struct EdgeData
		{
			uint32_t from;
			uint32_t to;
			float weight;
		};
		VkDeviceSize edgeSize = sizeof(struct EdgeData) * ecount;
		rt_helpers_create_device_buffer(&r->core, edgeSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, &r->yhrt_edge_buf, &r->yhrt_edge_mem);

		struct EdgeData *edgeData = malloc(edgeSize);
		for (igraph_integer_t e = 0; e < ecount; e++) {
			edgeData[e].from = (uint32_t)IGRAPH_FROM(graph, e);
			edgeData[e].to = (uint32_t)IGRAPH_TO(graph, e);
			edgeData[e].weight = 1.0f;
		}
		rt_helpers_staging_upload(&r->core, r->yhrt_edge_buf, edgeData, edgeSize);
		free(edgeData);
	}

	// ---- Create descriptor pool and bind all resources ----
	{
		VkDescriptorPoolSize poolSizes[] = {
			{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4},
		};
		VkDescriptorPoolCreateInfo dpInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = poolSizes};
		VK_CHECK(vkCreateDescriptorPool(r->core.device, &dpInfo, NULL, &r->yhrt_desc_pool), "Failed to create YHRT descriptor pool");
		VkDescriptorSetAllocateInfo setInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = r->yhrt_desc_pool, .descriptorSetCount = 1, .pSetLayouts = &r->yhrt_desc_set_layout};
		VK_CHECK(vkAllocateDescriptorSets(r->core.device, &setInfo, &r->yhrt_desc_set), "Failed to allocate YHRT descriptor set");

		VkDescriptorBufferInfo nodeInfo = {r->yhrt_node_buf, 0, VK_WHOLE_SIZE};
		VkDescriptorBufferInfo forceInfo = {bhrt_force_buffer(r->bhrt), 0, VK_WHOLE_SIZE};
		VkDescriptorBufferInfo edgeInfo = {r->yhrt_edge_buf, 0, VK_WHOLE_SIZE};
		VkDescriptorBufferInfo fnormInfo = {r->yhrt_fnorm_buf, 0, VK_WHOLE_SIZE};

		VkWriteDescriptorSet writes[4] = {
			VK_WRITE_DESC_BUFFER(r->yhrt_desc_set, 0, &nodeInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
			VK_WRITE_DESC_BUFFER(r->yhrt_desc_set, 1, &forceInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
			VK_WRITE_DESC_BUFFER(r->yhrt_desc_set, 2, &edgeInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
			VK_WRITE_DESC_BUFFER(r->yhrt_desc_set, 4, &fnormInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
		};
		vkUpdateDescriptorSets(r->core.device, 4, writes, 0, NULL);
	}

	r->yhrt_active = true;
	printf("[YHRT] Worker session started, %d iterations planned\n", r->yhrt_maxiter);
	return true;
}

static void yhrt_readback_positions_to_cpu(Renderer *r)
{
	// Copy yhrt_node_buf → yhrt_pos_staging_buf via one-time submit, then map.
	VkCommandPoolCreateInfo poolInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, .queueFamilyIndex = (uint32_t)r->core.graphicsQueueFamily};
	VkCommandPool pool;
	VK_CHECK(vkCreateCommandPool(r->core.device, &poolInfo, NULL, &pool), "Failed to create YHRT readback pool");
	VkCommandBufferAllocateInfo cmdInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
	VkCommandBuffer cmd;
	VK_CHECK(vkAllocateCommandBuffers(r->core.device, &cmdInfo, &cmd), "Failed to allocate YHRT readback cmd");
	VK_CHECK(vkBeginCommandBuffer(cmd, &VK_CMD_BEGIN_INFO_ONETIME), "Failed to begin YHRT readback cmd");

	VkMemoryBarrier nodeBarrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT};
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &nodeBarrier, 0, NULL, 0, NULL);

	VkBufferCopy posCopy = {.size = sizeof(vec4) * r->yhrt_vcount};
	vkCmdCopyBuffer(cmd, r->yhrt_node_buf, r->yhrt_pos_staging_buf, 1, &posCopy);

	VK_CHECK(vkEndCommandBuffer(cmd), "Failed to end YHRT readback cmd");
	VkSubmitInfo submitInfo = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
	VkFence fence;
	VkFenceCreateInfo fenceInfo = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
	VK_CHECK(vkCreateFence(r->core.device, &fenceInfo, NULL, &fence), "Failed to create readback fence");
	pthread_mutex_lock(&r->core.graphicsQueueMutex);
	VK_CHECK(vkQueueSubmit(r->core.graphicsQueue, 1, &submitInfo, fence), "Failed to submit YHRT readback");
	pthread_mutex_unlock(&r->core.graphicsQueueMutex);
	VK_CHECK(vkWaitForFences(r->core.device, 1, &fence, VK_TRUE, UINT64_MAX), "Failed to wait for readback fence");
	vkDestroyFence(r->core.device, fence, NULL);
	vkDestroyCommandPool(r->core.device, pool, NULL);

	// Map and extract xyz → yhrt_cpu_positions (stride conversion: vec4 → xyz)
	void *mapped;
	VK_CHECK(vkMapMemory(r->core.device, r->yhrt_pos_staging_mem, 0, sizeof(vec4) * r->yhrt_vcount, 0, &mapped), "Failed to map YHRT readback");
	for (uint32_t i = 0; i < r->yhrt_vcount; i++) {
		float *src = &((float *)mapped)[i * 4];
		r->yhrt_cpu_positions[i * 3 + 0] = src[0];
		r->yhrt_cpu_positions[i * 3 + 1] = src[1];
		r->yhrt_cpu_positions[i * 3 + 2] = src[2];
	}
	vkUnmapMemory(r->core.device, r->yhrt_pos_staging_mem);
}

bool yhrt_worker_step(Renderer *r)
{
	if (!r->yhrt_active || !r->bhrt || !bhrt_is_supported(r->bhrt))
		return false;
	if (r->yhrt_current_iter >= r->yhrt_maxiter)
		return false;

	// Wait for previous GPU work — fence stays SIGNALED after this
	VK_CHECK(vkWaitForFences(r->core.device, 1, &r->yhrt_dispatch_fence, VK_TRUE, UINT64_MAX), "Failed to wait for YHRT dispatch fence");

	// Read back Fnorm from previous iteration (valid because fence was just waited on)
	if (r->yhrt_current_iter > 0) {
		float fnorm = 0.0f;
		void *mapped;
		if (vkMapMemory(r->core.device, r->yhrt_staging_mem, 0, sizeof(double), 0, &mapped) == VK_SUCCESS) {
			if (r->yhrt_fp64_supported)
				fnorm = (float)(*(double *)mapped);
			else
				fnorm = (*(float *)mapped) * (float)r->yhrt_vcount;
			vkUnmapMemory(r->core.device, r->yhrt_staging_mem);
		}

		printf("[YHRT] iter=%d, step=%g, Fnorm=%g, Fnorm0=%g, repulsive_exp=%g, natlen=%g\n", r->yhrt_current_iter - 1, r->yhrt_step, fnorm, r->yhrt_Fnorm0, r->yhrt_p, r->yhrt_K);

		if (fnorm < r->yhrt_Fnorm0) {
			if (fnorm > 0.95f * r->yhrt_Fnorm0) {
			} else {
				r->yhrt_step *= 0.99f / IGRAPH_YHU_COOL;
			}
		} else {
			r->yhrt_step *= IGRAPH_YHU_COOL;
		}
		r->yhrt_Fnorm0 = fnorm;

		if (r->yhrt_step < r->yhrt_tolerance)
			return false;
	}

	// ---- Read back positions from GPU for BH octree rebuild ----
	yhrt_readback_positions_to_cpu(r);

	// ---- First-iteration debug: repulsion forces with correct positions ----
	if (r->yhrt_current_iter == 0) {
		printf("[DEBUG] ===== Iteration 0 debug: repulsion-only pass =====\n");

		printf("[DEBUG] yhrt_cpu_positions[0..2] = (%.6f, %.6f, %.6f)\n", r->yhrt_cpu_positions[0], r->yhrt_cpu_positions[1], r->yhrt_cpu_positions[2]);
		printf("[DEBUG] yhrt_cpu_positions[3..5] = (%.6f, %.6f, %.6f)\n", r->yhrt_cpu_positions[3], r->yhrt_cpu_positions[4], r->yhrt_cpu_positions[5]);
		printf("[DEBUG] yhrt_cpu_positions[6..8] = (%.6f, %.6f, %.6f)\n", r->yhrt_cpu_positions[6], r->yhrt_cpu_positions[7], r->yhrt_cpu_positions[8]);

		// CPU O(N^2) reference for particle 0 against all others
		{
			float fx0 = 0, fy0 = 0, fz0 = 0;
			for (uint32_t dbj = 1; dbj < r->yhrt_vcount; dbj++) {
				float dx = r->yhrt_cpu_positions[0] - r->yhrt_cpu_positions[dbj * 3];
				float dy = r->yhrt_cpu_positions[1] - r->yhrt_cpu_positions[dbj * 3 + 1];
				float dz = r->yhrt_cpu_positions[2] - r->yhrt_cpu_positions[dbj * 3 + 2];
				float d2 = dx * dx + dy * dy + dz * dz;
				if (d2 > 1e-12f) {
					fx0 += dx / d2;
					fy0 += dy / d2;
					fz0 += dz / d2;
				}
			}
			float fmag0 = sqrtf(fx0 * fx0 + fy0 * fy0 + fz0 * fz0);
			printf("[DEBUG] CPU O(N^2) force[0] = (%.6f, %.6f, %.6f) |F|=%.6f\n", fx0, fy0, fz0, fmag0);
		}

		VkFence dbgFence;
		VkFenceCreateInfo dbgFenceInfo = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
		VK_CHECK(vkCreateFence(r->core.device, &dbgFenceInfo, NULL, &dbgFence), "debug fence");

		VK_CHECK(vkResetCommandBuffer(r->yhrt_cmd_buf, 0), "debug reset cmd");
		VkCommandBufferBeginInfo dbgBegin = VK_CMD_BEGIN_INFO_ONETIME;
		VK_CHECK(vkBeginCommandBuffer(r->yhrt_cmd_buf, &dbgBegin), "debug begin cmd");

		bhrt_build(r->bhrt, r->yhrt_cpu_positions, r->yhrt_cmd_buf);

		vkCmdFillBuffer(r->yhrt_cmd_buf, bhrt_force_buffer(r->bhrt), 0, sizeof(float) * 4 * r->yhrt_vcount, 0);
		VK_PIPELINE_BARRIER(r->yhrt_cmd_buf, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

		VK_PIPELINE_BARRIER(r->yhrt_cmd_buf, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
		bhrt_record_dispatch(r->bhrt, r->yhrt_cmd_buf);

		VK_CHECK(vkEndCommandBuffer(r->yhrt_cmd_buf), "debug end cmd");

		VkSubmitInfo dbgSubmit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &r->yhrt_cmd_buf};
		pthread_mutex_lock(&r->core.graphicsQueueMutex);
		VK_CHECK(vkQueueSubmit(r->core.graphicsQueue, 1, &dbgSubmit, dbgFence), "debug submit");
		pthread_mutex_unlock(&r->core.graphicsQueueMutex);
		VK_CHECK(vkWaitForFences(r->core.device, 1, &dbgFence, VK_TRUE, UINT64_MAX), "debug wait");
		vkDestroyFence(r->core.device, dbgFence, NULL);

		float *forceDbg = (float *)malloc(sizeof(float) * 4 * r->yhrt_vcount);
		bhrt_readback_forces(r->bhrt, forceDbg);

		float totalFmag = 0.0f;
		for (uint32_t dbi = 0; dbi < r->yhrt_vcount; dbi++) {
			float fx = forceDbg[dbi * 4], fy = forceDbg[dbi * 4 + 1], fz = forceDbg[dbi * 4 + 2];
			float fmag = sqrtf(fx * fx + fy * fy + fz * fz);
			totalFmag += fmag;
			if (dbi < 5)
				printf("[DEBUG] GPU Force[%u] = (%.6f, %.6f, %.6f) |F|=%.6f\n", dbi, fx, fy, fz, fmag);
		}
		printf("[DEBUG] GPU Repulsion Fnorm (sum|F|) = %.6f  (avg=%.6f)\n", totalFmag, totalFmag / (float)r->yhrt_vcount);

		free(forceDbg);
		printf("[DEBUG] ===== End iteration 0 debug =====\n");
	}

	// Only reset fence when we're about to submit new work
	VK_CHECK(vkResetFences(r->core.device, 1, &r->yhrt_dispatch_fence), "Failed to reset YHRT dispatch fence");

	// ---- Begin command buffer (our own pool) ----
	VK_CHECK(vkResetCommandBuffer(r->yhrt_cmd_buf, 0), "Failed to reset YHRT cmd buffer");
	VkCommandBufferBeginInfo beginInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
	VK_CHECK(vkBeginCommandBuffer(r->yhrt_cmd_buf, &beginInfo), "Failed to begin YHRT cmd buffer");
	VkCommandBuffer cmd = r->yhrt_cmd_buf;

	// ---- BH octree build + BLAS/TLAS rebuild ----
	bhrt_build(r->bhrt, r->yhrt_cpu_positions, cmd);

	// Barrier: AS build → compute
	VK_PIPELINE_BARRIER(cmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

	// Zero force buffer before repulsion dispatch (ensures defined initial state)
	vkCmdFillBuffer(cmd, bhrt_force_buffer(r->bhrt), 0, sizeof(float) * 4 * r->yhrt_vcount, 0);
	VK_PIPELINE_BARRIER(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

	// ---- BH repulsion dispatch ----
	bhrt_record_dispatch(r->bhrt, cmd);

	VK_PIPELINE_BARRIER(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

	// ---- Fnorm reset (before attraction/update accumulate) ----
	vkCmdFillBuffer(cmd, r->yhrt_fnorm_buf, 0, sizeof(double), 0);
	VK_PIPELINE_BARRIER(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

	struct
	{
		float KP, CRK, p, step_size;
		uint32_t vertex_count, edge_count;
		float R;
	} pc = {r->yhrt_KP, r->yhrt_CRK, r->yhrt_p, r->yhrt_step, r->yhrt_vcount, r->yhrt_ecount, r->yhrt_R};

	// Attraction — all edges
	YHRT_DISPATCH_COMPUTE(cmd, r->yhrt_attraction_pipeline, r->yhrt_pipeline_layout, r->yhrt_desc_set, pc, (r->yhrt_ecount + YHRT_WORKGROUP_SIZE - 1) / YHRT_WORKGROUP_SIZE);

	VK_PIPELINE_BARRIER(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

	// Update — only active nodes
	VkPipeline updatePipeline = r->yhrt_fp64_supported ? r->yhrt_update_fp64_pipeline : r->yhrt_update_pipeline;
	YHRT_DISPATCH_COMPUTE(cmd, updatePipeline, r->yhrt_pipeline_layout, r->yhrt_desc_set, pc, (r->yhrt_vcount + YHRT_WORKGROUP_SIZE - 1) / YHRT_WORKGROUP_SIZE);

	// Fnorm + position readback copies
	VK_PIPELINE_BARRIER(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
	VkBufferCopy fnormCopy = {.size = sizeof(double)};
	vkCmdCopyBuffer(cmd, r->yhrt_fnorm_buf, r->yhrt_staging_buf, 1, &fnormCopy);
	r->yhrt_fnorm_readback_pending = true;

	// Position readback to staging (for next iteration's bhrt_build, all nodes)
	VkBufferCopy posCopy = {.size = sizeof(vec4) * r->yhrt_vcount};
	vkCmdCopyBuffer(cmd, r->yhrt_node_buf, r->yhrt_pos_staging_buf, 1, &posCopy);

	// ---- Submit ----
	VK_CHECK(vkEndCommandBuffer(cmd), "Failed to end YHRT cmd buffer");
	VkSubmitInfo submitInfo = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
	pthread_mutex_lock(&r->core.graphicsQueueMutex);
	VK_CHECK(vkQueueSubmit(r->core.graphicsQueue, 1, &submitInfo, r->yhrt_dispatch_fence), "Failed to submit YHRT work");
	pthread_mutex_unlock(&r->core.graphicsQueueMutex);

	r->yhrt_current_iter++;
	return true;
}

bool yhrt_worker_readback(Renderer *r, igraph_matrix_t *out_positions)
{
	if (!out_positions)
		return false;

	VK_CHECK(vkWaitForFences(r->core.device, 1, &r->yhrt_dispatch_fence, VK_TRUE, UINT64_MAX), "Failed to wait for YHRT readback fence");

	// Read from yhrt_pos_staging_buf (last step copied node_buf → pos_staging_buf)
	void *mapped;
	VK_CHECK(vkMapMemory(r->core.device, r->yhrt_pos_staging_mem, 0, sizeof(vec4) * r->yhrt_vcount, 0, &mapped), "Failed to map YHRT readback");

	float *positions = (float *)mapped;
	igraph_integer_t vc = (igraph_integer_t)r->yhrt_vcount;
	igraph_integer_t ncols = igraph_matrix_ncol(out_positions);

	for (igraph_integer_t i = 0; i < vc; i++) {
		MATRIX(*out_positions, i, 0) = positions[i * 4 + 0];
		MATRIX(*out_positions, i, 1) = positions[i * 4 + 1];
		if (ncols > 2)
			MATRIX(*out_positions, i, 2) = positions[i * 4 + 2];
	}
	vkUnmapMemory(r->core.device, r->yhrt_pos_staging_mem);
	return true;
}

void yhrt_worker_cleanup(Renderer *r)
{
	VK_CHECK(vkWaitForFences(r->core.device, 1, &r->yhrt_dispatch_fence, VK_TRUE, UINT64_MAX), "Failed to wait for YHRT cleanup");
	yhrt_cleanup_algo_buffers(r);
	bhrt_session_cleanup(r->bhrt);
	r->yhrt_active = false;
	printf("[YHRT] Worker session completed after %d iterations, final step=%.6f\n", r->yhrt_current_iter, r->yhrt_step);
}

// ============================================================================
// Full Cleanup (called at renderer shutdown)
// ============================================================================

void yhrt_destroy(Renderer *r)
{
	yhrt_cleanup_algo_buffers(r);
	bhrt_session_cleanup(r->bhrt);

	if (r->yhrt_update_pipeline != VK_NULL_HANDLE)
		vkDestroyPipeline(r->core.device, r->yhrt_update_pipeline, NULL);
	if (r->yhrt_update_fp64_pipeline != VK_NULL_HANDLE)
		vkDestroyPipeline(r->core.device, r->yhrt_update_fp64_pipeline, NULL);
	if (r->yhrt_attraction_pipeline != VK_NULL_HANDLE)
		vkDestroyPipeline(r->core.device, r->yhrt_attraction_pipeline, NULL);
	if (r->yhrt_pipeline_layout != VK_NULL_HANDLE)
		vkDestroyPipelineLayout(r->core.device, r->yhrt_pipeline_layout, NULL);
	if (r->yhrt_desc_set_layout != VK_NULL_HANDLE)
		vkDestroyDescriptorSetLayout(r->core.device, r->yhrt_desc_set_layout, NULL);

	r->yhrt_attraction_pipeline = VK_NULL_HANDLE;
	r->yhrt_update_pipeline = VK_NULL_HANDLE;
	r->yhrt_update_fp64_pipeline = VK_NULL_HANDLE;
	r->yhrt_pipeline_layout = VK_NULL_HANDLE;
	r->yhrt_desc_set_layout = VK_NULL_HANDLE;
	r->yhrt_active = false;
}