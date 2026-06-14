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
// Reusable RT infrastructure (BLAS/TLAS, instance buffer, command pool, staging)
// is provided by RTBase (rt_base.c). This file only contains Yifan Hu-specific
// algorithm code: pipelines, state, cooling schedule, and convergence checks.
//
// =============================================================================

#include "vulkan/rt_layout.h"

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vulkan/buffers.h"
#include "vulkan/octree.h"
#include "vulkan/renderer.h"
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
		vkCmdPushConstants((cmd), (layout), VK_SHADER_STAGE_COMPUTE_BIT, 0, 32, &(pc)); \
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
	VK_DESTROY_BUFFER(r->core.device, r->yhrt_mass_buf, r->yhrt_mass_mem);
	VK_DESTROY_BUFFER(r->core.device, r->yhrt_center_buf, r->yhrt_center_mem);

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
// Pipeline Initialization (called once)
// ============================================================================

void yhrt_init_pipelines(Renderer *r)
{
	r->yhrt_active = false;
	r->yhrt_fp64_supported = r->rt_base && rt_base_fp64_supported(r->rt_base);
	r->yhrt_desc_set_layout = VK_NULL_HANDLE;
	r->yhrt_pipeline_layout = VK_NULL_HANDLE;
	r->yhrt_repulsion_pipeline = VK_NULL_HANDLE;
	r->yhrt_attraction_pipeline = VK_NULL_HANDLE;
	r->yhrt_update_pipeline = VK_NULL_HANDLE;
	r->yhrt_update_fp64_pipeline = VK_NULL_HANDLE;
	r->yhrt_update_instances_pipeline = VK_NULL_HANDLE;
	r->yhrt_desc_pool = VK_NULL_HANDLE;

	if (!r->rt_base || !rt_base_is_supported(r->rt_base)) {
		printf("[YHRT] Ray tracing not supported, layout disabled\n");
		return;
	}

	// Descriptor Set Layout — shared by all 5 compute pipelines
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
	// Push constants: 32 bytes = 5 floats + 3 uints
	//   [KP, CRK, p, step_size, vertex_count, edge_count, R, num_levels]
	VkDescriptorSetLayoutBinding bindings[8] = {
		{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}, {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}, {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}, {3, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}, {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}, {5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}, {6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL}, {7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
	};
	VkDescriptorSetLayoutCreateInfo layoutInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 8, .pBindings = bindings};
	VK_CHECK(vkCreateDescriptorSetLayout(r->core.device, &layoutInfo, NULL, &r->yhrt_desc_set_layout), "Failed to create YHRT descriptor set layout");

	VkPushConstantRange pcRange = {.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0, .size = 32};
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
	if (!r->rt_base || !rt_base_is_supported(r->rt_base)) {
		fprintf(stderr, "[YHRT] Cannot start: ray tracing not supported\n");
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
	rt_base_create_device_buffer(r->rt_base, nodeSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, &r->yhrt_node_buf, &r->yhrt_node_mem);

	float *nodeData = malloc(sizeof(float) * 4 * vcount);
	for (igraph_integer_t i = 0; i < vcount; i++) {
		nodeData[i * 4 + 0] = (float)MATRIX(*init_positions, i, 0);
		nodeData[i * 4 + 1] = (float)MATRIX(*init_positions, i, 1);
		nodeData[i * 4 + 2] = (igraph_matrix_ncol(init_positions) > 2) ? (float)MATRIX(*init_positions, i, 2) : 0.0f;
		igraph_int_t deg;
		igraph_degree_1(graph, &deg, i, IGRAPH_ALL, IGRAPH_LOOPS);
		nodeData[i * 4 + 3] = (float)(deg > 0 ? deg : 1);
	}
	rt_base_staging_upload(r->rt_base, r->yhrt_node_buf, nodeData, nodeSize, true);
	free(nodeData);

	// ---- Build octree for multi-geometry BLAS ----
	Octree octree;
	int L = octree_adaptive_levels((int)vcount);
	r->yhrt_num_levels = L;

	float *flat_pos = malloc(sizeof(float) * (size_t)vcount * 3);
	for (igraph_integer_t i = 0; i < vcount; i++) {
		flat_pos[i * 3 + 0] = (float)MATRIX(*init_positions, i, 0);
		flat_pos[i * 3 + 1] = (float)MATRIX(*init_positions, i, 1);
		flat_pos[i * 3 + 2] = (igraph_matrix_ncol(init_positions) > 2) ? (float)MATRIX(*init_positions, i, 2) : 0.0f;
	}
	octree_build(&octree, flat_pos, (int)vcount, 3);
	free(flat_pos);

	int *node_level = malloc(sizeof(int) * (size_t)vcount);
	float *masses = malloc(sizeof(float) * (size_t)vcount);
	float *centers = malloc(sizeof(float) * (size_t)vcount * 3);
	octree_get_node_levels(&octree, node_level, masses, centers);

	float *radii = malloc(sizeof(float) * (size_t)L);
	octree_level_radii(&octree, L, radii);

	// ---- Initialize RT session with multi-geometry BLAS ----
	if (!rt_base_session_init(r->rt_base, graph, init_positions, radii, (uint32_t)L, node_level)) {
		fprintf(stderr, "[YHRT] RT base session init failed\n");
		octree_destroy(&octree);
		free(node_level);
		free(masses);
		free(centers);
		free(radii);
		return false;
	}

	// ---- Octree mass + center-of-mass buffers ----
	rt_base_create_device_buffer(r->rt_base, sizeof(float) * (size_t)vcount, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, &r->yhrt_mass_buf, &r->yhrt_mass_mem);
	rt_base_staging_upload(r->rt_base, r->yhrt_mass_buf, masses, sizeof(float) * (size_t)vcount, true);

	float *center_packed = malloc(sizeof(float) * 4 * (size_t)vcount);
	for (igraph_integer_t i = 0; i < vcount; i++) {
		center_packed[i * 4 + 0] = centers[i * 3 + 0];
		center_packed[i * 4 + 1] = centers[i * 3 + 1];
		center_packed[i * 4 + 2] = centers[i * 3 + 2];
		center_packed[i * 4 + 3] = 0.0f;
	}
	rt_base_create_device_buffer(r->rt_base, sizeof(float) * 4 * (size_t)vcount, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, &r->yhrt_center_buf, &r->yhrt_center_mem);
	rt_base_staging_upload(r->rt_base, r->yhrt_center_buf, center_packed, sizeof(float) * 4 * (size_t)vcount, true);
	free(center_packed);

	octree_destroy(&octree);
	free(node_level);
	free(masses);
	free(centers);
	free(radii);

	r->yhrt_octree_finalized = true;

	// ---- Fnorm + dispatch fence ----
	rt_base_create_device_buffer(r->rt_base, sizeof(double), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, &r->yhrt_fnorm_buf, &r->yhrt_fnorm_mem);
	rt_base_create_buffer(r->rt_base, sizeof(double), VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, &r->yhrt_staging_buf, &r->yhrt_staging_mem);
	{
		VkFenceCreateInfo fenceInfo = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT};
		VK_CHECK(vkCreateFence(r->core.device, &fenceInfo, NULL, &r->yhrt_dispatch_fence), "Failed to create YHRT dispatch fence");
	}

	// ---- Force buffer ----
	rt_base_create_device_buffer(r->rt_base, sizeof(vec4) * vcount, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &r->yhrt_force_buf, &r->yhrt_force_mem);

	// ---- Edge buffer ----
	{
		struct EdgeData
		{
			uint32_t from;
			uint32_t to;
			float weight;
		};
		VkDeviceSize edgeSize = sizeof(struct EdgeData) * ecount;
		rt_base_create_device_buffer(r->rt_base, edgeSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, &r->yhrt_edge_buf, &r->yhrt_edge_mem);

		struct EdgeData *edgeData = malloc(edgeSize);
		for (igraph_integer_t e = 0; e < ecount; e++) {
			edgeData[e].from = (uint32_t)IGRAPH_FROM(graph, e);
			edgeData[e].to = (uint32_t)IGRAPH_TO(graph, e);
			edgeData[e].weight = 1.0f;
		}
		rt_base_staging_upload(r->rt_base, r->yhrt_edge_buf, edgeData, edgeSize, true);
		free(edgeData);
	}

	// ---- Create descriptor pool and bind all resources ----
	{
		VkDescriptorPoolSize poolSizes[] = {
			{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 7},
			{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1},
		};
		VkDescriptorPoolCreateInfo dpInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 1, .poolSizeCount = 2, .pPoolSizes = poolSizes};
		VK_CHECK(vkCreateDescriptorPool(r->core.device, &dpInfo, NULL, &r->yhrt_desc_pool), "Failed to create YHRT descriptor pool");
		VkDescriptorSetAllocateInfo setInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = r->yhrt_desc_pool, .descriptorSetCount = 1, .pSetLayouts = &r->yhrt_desc_set_layout};
		VK_CHECK(vkAllocateDescriptorSets(r->core.device, &setInfo, &r->yhrt_desc_set), "Failed to allocate YHRT descriptor set");

		VkDescriptorBufferInfo nodeInfo = {r->yhrt_node_buf, 0, VK_WHOLE_SIZE};
		VkDescriptorBufferInfo forceInfo = {r->yhrt_force_buf, 0, VK_WHOLE_SIZE};
		VkDescriptorBufferInfo edgeInfo = {r->yhrt_edge_buf, 0, VK_WHOLE_SIZE};
		VkDescriptorBufferInfo fnormInfo = {r->yhrt_fnorm_buf, 0, VK_WHOLE_SIZE};
		VkDescriptorBufferInfo instInfo = {rt_base_instance_buf(r->rt_base), 0, VK_WHOLE_SIZE};
		VkDescriptorBufferInfo massInfo = {r->yhrt_mass_buf, 0, VK_WHOLE_SIZE};
		VkDescriptorBufferInfo centerInfo = {r->yhrt_center_buf, 0, VK_WHOLE_SIZE};

		VkWriteDescriptorSet writes[7] = {
			VK_WRITE_DESC_BUFFER(r->yhrt_desc_set, 0, &nodeInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER), VK_WRITE_DESC_BUFFER(r->yhrt_desc_set, 1, &forceInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER), VK_WRITE_DESC_BUFFER(r->yhrt_desc_set, 2, &edgeInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER), VK_WRITE_DESC_BUFFER(r->yhrt_desc_set, 4, &fnormInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER), VK_WRITE_DESC_BUFFER(r->yhrt_desc_set, 5, &instInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER), VK_WRITE_DESC_BUFFER(r->yhrt_desc_set, 6, &massInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER), VK_WRITE_DESC_BUFFER(r->yhrt_desc_set, 7, &centerInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
		};
		vkUpdateDescriptorSets(r->core.device, 7, writes, 0, NULL);

		VkAccelerationStructureKHR tlas = rt_base_tlas(r->rt_base);
		VkWriteDescriptorSetAccelerationStructureKHR asDescInfo = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
			.accelerationStructureCount = 1,
			.pAccelerationStructures = &tlas,
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
	if (!r->yhrt_active || !r->rt_base || !rt_base_is_supported(r->rt_base))
		return false;
	if (r->yhrt_current_iter >= r->yhrt_maxiter)
		return false;

	// Wait for previous GPU work — fence stays SIGNALED after this
	VK_CHECK(vkWaitForFences(r->core.device, 1, &r->yhrt_dispatch_fence, VK_TRUE, UINT64_MAX), "Failed to wait for YHRT dispatch fence");

	// Progressive insertion: add a batch of nodes every 5 iterations
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

		if (r->yhrt_step < r->yhrt_tolerance) {
			if (r->yhrt_active_vcount >= r->yhrt_vcount)
				return false;
			r->yhrt_step = r->yhrt_tolerance;
		}
	}

	// Only reset fence when we're about to submit new work
	VK_CHECK(vkResetFences(r->core.device, 1, &r->yhrt_dispatch_fence), "Failed to reset YHRT dispatch fence");

	// ---- Begin command buffer via rt_base ----
	VkCommandBuffer cmd = rt_base_begin_commands(r->rt_base);

	// Fnorm reset
	vkCmdFillBuffer(cmd, r->yhrt_fnorm_buf, 0, sizeof(double), 0);
	VK_PIPELINE_BARRIER(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

	struct
	{
		float KP, CRK, p, step_size;
		uint32_t vertex_count, edge_count;
		float R;
		uint32_t num_levels;
	} pc = {r->yhrt_KP, r->yhrt_CRK, r->yhrt_p, r->yhrt_step, r->yhrt_active_vcount, r->yhrt_ecount, r->yhrt_R, r->yhrt_num_levels};

	// Repulsion — only active nodes
	YHRT_DISPATCH_COMPUTE(cmd, r->yhrt_repulsion_pipeline, r->yhrt_pipeline_layout, r->yhrt_desc_set, pc, (r->yhrt_active_vcount + YHRT_WORKGROUP_SIZE - 1) / YHRT_WORKGROUP_SIZE);

	VK_PIPELINE_BARRIER(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

	// Attraction — all edges
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

	// TLAS update (in-place BVH patch, via rt_base)
	rt_base_record_tlas_update(r->rt_base, cmd, r->yhrt_active_vcount);

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
		vkCmdCopyBuffer(cmd, r->yhrt_node_buf, rt_base_node_staging_buf(r->rt_base), 1, &posCopy);
	}

	// ---- Submit via rt_base ----
	rt_base_submit_commands(r->rt_base, r->yhrt_dispatch_fence);

	r->yhrt_current_iter++;
	return true;
}

bool yhrt_worker_readback(Renderer *r, igraph_matrix_t *out_positions)
{
	if (!out_positions)
		return false;

	VK_CHECK(vkWaitForFences(r->core.device, 1, &r->yhrt_dispatch_fence, VK_TRUE, UINT64_MAX), "Failed to wait for YHRT readback fence");

	return rt_base_readback_positions(r->rt_base, r->yhrt_vcount, out_positions);
}

void yhrt_worker_cleanup(Renderer *r)
{
	pthread_mutex_lock(&r->core.graphicsQueueMutex);
	vkQueueWaitIdle(r->core.graphicsQueue);
	pthread_mutex_unlock(&r->core.graphicsQueueMutex);
	yhrt_cleanup_algo_buffers(r);
	rt_base_session_cleanup(r->rt_base);
	r->yhrt_active = false;
	printf("[YHRT] Worker session completed after %d iterations, final step=%.6f\n", r->yhrt_current_iter, r->yhrt_step);
}

// ============================================================================
// Full Cleanup (called at renderer shutdown)
// ============================================================================

void yhrt_destroy(Renderer *r)
{
	yhrt_cleanup_algo_buffers(r);
	if (r->rt_base)
		rt_base_session_cleanup(r->rt_base);

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