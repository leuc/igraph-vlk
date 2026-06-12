#define _GNU_SOURCE

#include "app_state.h"
#include "graph/wrappers_layout.h"
#include "interaction/state.h"
#include "vulkan/buffers.h"
#include "vulkan/renderer.h"
#include "vulkan/renderer_escape_layout.h"
#include "vulkan/utils.h"
#include <float.h>
#include <igraph.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// GPU-side physics buffer types (must match shader layout)
// ============================================================================

typedef struct
{
	float position[4];		// xyz = pos, w = mass
	float escape_vector[4]; // xyz = escape_dir, w = openness
	float freedom[4];		// x = local_freedom [0..1], w = is_awake
} NodePhysicsGPU;

typedef struct
{
	uint32_t nodeA;
	uint32_t nodeB;
} EdgeGPU;

typedef struct
{
	float alpha;
	float avg_degree;
	uint32_t node_count;
} EscapeSimParams;

// Forward declarations for per-frame tick
static uint32_t escape_readback_positions(Renderer *r, GraphData *data);
static void escape_record_iteration(VkCommandBuffer cmd, Renderer *r, EscapeSimParams *params, uint32_t node_count, uint32_t edge_count, uint32_t frame_index);

// ============================================================================
// CPU worker: random initial placement in a cube
// ============================================================================

void *compute_escape_layout(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	fprintf(stderr, "[Escape] Worker: vcount=%ld\n", (long)vcount);
	if (vcount == 0)
		return NULL;

	igraph_matrix_t *result = IGRAPH_MALLOC(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 3) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		return NULL;
	}

	float half_side = 5.0f * powf((float)vcount / 0.05f, 1.0f / 3.0f);
	srand(42);
	for (igraph_integer_t i = 0; i < vcount; i++) {
		MATRIX(*result, i, 0) = (igraph_real_t)((float)rand() / (float)RAND_MAX * 2.0f * half_side - half_side);
		MATRIX(*result, i, 1) = (igraph_real_t)((float)rand() / (float)RAND_MAX * 2.0f * half_side - half_side);
		MATRIX(*result, i, 2) = (igraph_real_t)((float)rand() / (float)RAND_MAX * 2.0f * half_side - half_side);
	}

	fprintf(stderr, "[Escape] Worker: %ld nodes randomly placed in cube [-%.0f,%.0f]\n", (long)vcount, half_side, half_side);
	return result;
}

// ============================================================================
// GPU simulation context management
// ============================================================================

// ============================================================================
// Per-frame GPU simulation tick (non-blocking)
// ============================================================================

bool igraph_vlk_layout_escape_tick(Renderer *r)
{
	if (!r->escape_sim_active)
		return false;

	uint32_t n = r->escape_node_count;
	uint32_t m = r->escape_edge_count;

	if (r->escape_needs_wait) {
		VK_CHECK(vkWaitForFences(r->core.device, 1, &r->escape_fence, VK_TRUE, UINT64_MAX), "Failed to wait for escape fence");
		VK_CHECK(vkResetFences(r->core.device, 1, &r->escape_fence), "Failed to reset escape fence");

		// Read back positions from GPU and update graph visualization every iteration
		uint32_t sleeping = escape_readback_positions(r, r->escape_graph_data);

		// Summary every 10 iters
		if (r->escape_current_iter % 10 == 0)
			printf("[Escape] iter %4u | sleeping=%u/%u disp_rel=%.6f conv_count=%u\n", r->escape_current_iter, sleeping, n, r->escape_prev_displacement, r->escape_convergence_count);

		// CPU-side TLAS update for next frame's RT pass
		if (r->escape_rt_supported) {
			renderer_escape_update_tlas_cpu(r, n);
		}

		// Motion-based convergence: stable displacement for 5 consecutive frames
		if (r->escape_prev_displacement < 1e-3f) {
			r->escape_convergence_count++;
		} else {
			r->escape_convergence_count = 0;
		}

		if (r->escape_convergence_count >= 5) {
			printf("[Escape] Converged (displacement < 0.1%% bb_diag for 5 frames) at iteration %u, sleeping=%u/%u\n", r->escape_current_iter, sleeping, n);
			r->escape_sim_active = false;
			return false;
		}

		if (r->escape_current_iter >= r->escape_max_iters) {
			printf("[Escape] Max iterations reached (%u) sleeping=%u/%u\n", r->escape_max_iters, sleeping, n);
			r->escape_sim_active = false;
			return false;
		}
	}

	EscapeSimParams params = {
		.alpha = r->escape_alpha,
		.avg_degree = r->escape_avg_degree,
		.node_count = n,
	};

	// Record and submit iteration
	VK_CHECK(vkResetCommandBuffer(r->escape_cmd_buf, 0), "Failed to reset escape command buffer");
	VK_CHECK(vkBeginCommandBuffer(r->escape_cmd_buf, &VK_CMD_BEGIN_INFO_ONETIME), "Failed to begin escape command buffer");
	escape_record_iteration(r->escape_cmd_buf, r, &params, n, m, r->escape_current_iter);
	VK_CHECK(vkEndCommandBuffer(r->escape_cmd_buf), "Failed to end escape command buffer");

	VkSubmitInfo submitInfo = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &r->escape_cmd_buf};
	VkResult res = vkQueueSubmit(r->core.graphicsQueue, 1, &submitInfo, r->escape_fence);
	if (res != VK_SUCCESS) {
		fprintf(stderr, "[escape] Queue submit failed at iteration %u\n", r->escape_current_iter);
		r->escape_sim_active = false;
		return false;
	}

	r->escape_needs_wait = true;
	r->escape_current_iter++;

	if (r->escape_current_iter % 100 == 0 || r->escape_current_iter == r->escape_max_iters)
		printf("[Escape] Submitted iter %4u / %u\n", r->escape_current_iter, r->escape_max_iters);

	return true;
}

static void escape_ensure_command_resources(Renderer *r)
{
	if (r->escape_cmd_pool != VK_NULL_HANDLE)
		return;

	VkCommandPoolCreateInfo poolInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = 0, .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT};
	VK_CHECK(vkCreateCommandPool(r->core.device, &poolInfo, NULL, &r->escape_cmd_pool), "Failed to create escape command pool");

	VkCommandBufferAllocateInfo allocInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = r->escape_cmd_pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
	VK_CHECK(vkAllocateCommandBuffers(r->core.device, &allocInfo, &r->escape_cmd_buf), "Failed to allocate escape command buffer");

	VK_CHECK(vkCreateFence(r->core.device, &VK_SIGNALED_FENCE_INFO, NULL, &r->escape_fence), "Failed to create escape fence");
	VK_CHECK(vkResetFences(r->core.device, 1, &r->escape_fence), "Failed to reset escape fence");
}

static void escape_create_gpu_buffers(Renderer *r, GraphData *data)
{
	uint32_t n = data->node_count;
	uint32_t m = data->edge_count;

	uint32_t *adj_neighbors = NULL;
	uint32_t *adj_offsets = NULL;
	uint32_t *adj_edge_counts = NULL;
	uint32_t total_edges = 0;

	if (n > 0) {
		adj_offsets = (uint32_t *)calloc(n, sizeof(uint32_t));
		adj_edge_counts = (uint32_t *)calloc(n, sizeof(uint32_t));

		igraph_vector_int_t neis;
		igraph_vector_int_init(&neis, 0);

		for (uint32_t i = 0; i < n; i++) {
			adj_offsets[i] = total_edges;
			igraph_neighbors(&data->g, &neis, (igraph_integer_t)i, IGRAPH_ALL, IGRAPH_LOOPS, IGRAPH_NO_MULTIPLE);
			uint32_t deg = (uint32_t)igraph_vector_int_size(&neis);
			adj_edge_counts[i] = deg;
			total_edges += deg;
		}

		adj_neighbors = (uint32_t *)calloc(total_edges, sizeof(uint32_t));
		uint32_t idx = 0;
		for (uint32_t i = 0; i < n; i++) {
			igraph_neighbors(&data->g, &neis, (igraph_integer_t)i, IGRAPH_ALL, IGRAPH_LOOPS, IGRAPH_NO_MULTIPLE);
			for (uint32_t j = 0; j < (uint32_t)igraph_vector_int_size(&neis); j++)
				adj_neighbors[idx++] = (uint32_t)VECTOR(neis)[j];
		}
		igraph_vector_int_destroy(&neis);
	}

	EdgeGPU *edges = NULL;
	if (m > 0) {
		edges = (EdgeGPU *)calloc(m, sizeof(EdgeGPU));
		for (uint32_t i = 0; i < m; i++) {
			edges[i].nodeA = data->edges[i].from;
			edges[i].nodeB = data->edges[i].to;
		}
	}

	// Destroy old buffers if any
	VK_DESTROY_BUFFER(r->core.device, r->escape_physics_buffer, r->escape_physics_memory);
	VK_DESTROY_BUFFER(r->core.device, r->escape_adjacency_buffer, r->escape_adjacency_memory);
	VK_DESTROY_BUFFER(r->core.device, r->escape_offsets_buffer, r->escape_offsets_memory);
	VK_DESTROY_BUFFER(r->core.device, r->escape_counts_buffer, r->escape_counts_memory);
	VK_DESTROY_BUFFER(r->core.device, r->escape_edges_buffer, r->escape_edges_memory);
	VK_DESTROY_BUFFER(r->core.device, r->escape_global_stress_buffer, r->escape_global_stress_memory);

	// Allocate GPU buffers
	VkDeviceSize phys_size = sizeof(NodePhysicsGPU) * n;
	VkDeviceSize adj_size = sizeof(uint32_t) * (total_edges > 0 ? total_edges : 1);
	VkDeviceSize offs_size = sizeof(uint32_t) * (n > 0 ? n : 1);
	VkDeviceSize cnts_size = sizeof(uint32_t) * (n > 0 ? n : 1);
	VkDeviceSize edge_size = sizeof(EdgeGPU) * (m > 0 ? m : 1);
	VkDeviceSize stress_size = sizeof(float);

	VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, phys_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, &r->escape_physics_buffer, &r->escape_physics_memory);
	VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, adj_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &r->escape_adjacency_buffer, &r->escape_adjacency_memory);
	VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, offs_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &r->escape_offsets_buffer, &r->escape_offsets_memory);
	VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, cnts_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &r->escape_counts_buffer, &r->escape_counts_memory);
	VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, edge_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &r->escape_edges_buffer, &r->escape_edges_memory);
	VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, stress_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, &r->escape_global_stress_buffer, &r->escape_global_stress_memory);

	// Compute centroid for escape vector direction
	float cx = 0.0f, cy = 0.0f, cz = 0.0f;
	for (uint32_t i = 0; i < n; i++) {
		cx += data->nodes[i].position[0];
		cy += data->nodes[i].position[1];
		cz += data->nodes[i].position[2];
	}
	cx /= (float)n;
	cy /= (float)n;
	cz /= (float)n;

	// Compute bounding box diagonal for parameter scaling
	float bb_min_x = FLT_MAX, bb_max_x = -FLT_MAX;
	float bb_min_y = FLT_MAX, bb_max_y = -FLT_MAX;
	float bb_min_z = FLT_MAX, bb_max_z = -FLT_MAX;
	for (uint32_t i = 0; i < n; i++) {
		float x = data->nodes[i].position[0], y = data->nodes[i].position[1], z = data->nodes[i].position[2];
		if (x < bb_min_x)
			bb_min_x = x;
		if (x > bb_max_x)
			bb_max_x = x;
		if (y < bb_min_y)
			bb_min_y = y;
		if (y > bb_max_y)
			bb_max_y = y;
		if (z < bb_min_z)
			bb_min_z = z;
		if (z > bb_max_z)
			bb_max_z = z;
	}
	float bb_diag = sqrtf((bb_max_x - bb_min_x) * (bb_max_x - bb_min_x) + (bb_max_y - bb_min_y) * (bb_max_y - bb_min_y) + (bb_max_z - bb_min_z) * (bb_max_z - bb_min_z));

	// Upload initial physics data with escape vectors
	NodePhysicsGPU *phys = (NodePhysicsGPU *)calloc(n, sizeof(NodePhysicsGPU));
	for (uint32_t i = 0; i < n; i++) {
		phys[i].position[0] = data->nodes[i].position[0];
		phys[i].position[1] = data->nodes[i].position[1];
		phys[i].position[2] = data->nodes[i].position[2];
		// Pack node's collision radius (tetrahedron scale) into .w, scaled by degree
		// Used by RT shader to offset ray origin outside own geometry
		phys[i].position[3] = log2f((float)data->nodes[i].degree + 2.0f) * 0.5f;
		// Escape vector: radial repulsion away from centroid, scaled by coreness
		float dx = data->nodes[i].position[0] - cx;
		float dy = data->nodes[i].position[1] - cy;
		float dz = data->nodes[i].position[2] - cz;
		float len = sqrtf(dx * dx + dy * dy + dz * dz);
		if (len > 0.001f) {
			phys[i].escape_vector[0] = dx / len;
			phys[i].escape_vector[1] = dy / len;
			phys[i].escape_vector[2] = dz / len;
		} else {
			phys[i].escape_vector[0] = 0.0f;
			phys[i].escape_vector[1] = 1.0f; // fallback: up
			phys[i].escape_vector[2] = 0.0f;
		}
		// All nodes start awake with maximum freedom
		phys[i].freedom[3] = 1.0f;
		phys[i].freedom[0] = 1.0f;
	}
	update_buffer(r->core.device, r->escape_physics_memory, phys_size, phys);

	free(phys);

	if (adj_neighbors)
		update_buffer(r->core.device, r->escape_adjacency_memory, adj_size, adj_neighbors);
	if (adj_offsets)
		update_buffer(r->core.device, r->escape_offsets_memory, offs_size, adj_offsets);
	if (adj_edge_counts)
		update_buffer(r->core.device, r->escape_counts_memory, cnts_size, adj_edge_counts);
	if (edges)
		update_buffer(r->core.device, r->escape_edges_memory, edge_size, edges);
	float zero_stress = 0.0f;
	update_buffer(r->core.device, r->escape_global_stress_memory, sizeof(float), &zero_stress);

	free(adj_neighbors);
	free(adj_offsets);
	free(adj_edge_counts);
	free(edges);

	// Update descriptor sets
	VkDescriptorBufferInfo physInfo = {r->escape_physics_buffer, 0, VK_WHOLE_SIZE};
	VkDescriptorBufferInfo adjInfo = {r->escape_adjacency_buffer, 0, VK_WHOLE_SIZE};
	VkDescriptorBufferInfo offsInfo = {r->escape_offsets_buffer, 0, VK_WHOLE_SIZE};
	VkDescriptorBufferInfo cntsInfo = {r->escape_counts_buffer, 0, VK_WHOLE_SIZE};
	VkDescriptorBufferInfo edgeInfo = {r->escape_edges_buffer, 0, VK_WHOLE_SIZE};
	VkDescriptorBufferInfo stressInfo = {r->escape_global_stress_buffer, 0, VK_WHOLE_SIZE};

	VkWriteDescriptorSet physicsWrites[] = {
		VK_WRITE_DESC_BUFFER(r->escape_physics_desc_set, 0, &physInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
		VK_WRITE_DESC_BUFFER(r->escape_physics_desc_set, 1, &adjInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
		VK_WRITE_DESC_BUFFER(r->escape_physics_desc_set, 2, &offsInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
		VK_WRITE_DESC_BUFFER(r->escape_physics_desc_set, 3, &cntsInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
	};
	vkUpdateDescriptorSets(r->core.device, 4, physicsWrites, 0, NULL);

	VkWriteDescriptorSet stressWrites[] = {
		VK_WRITE_DESC_BUFFER(r->escape_stress_desc_set, 0, &physInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
		VK_WRITE_DESC_BUFFER(r->escape_stress_desc_set, 1, &edgeInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
		VK_WRITE_DESC_BUFFER(r->escape_stress_desc_set, 2, &stressInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER),
	};
	vkUpdateDescriptorSets(r->core.device, 3, stressWrites, 0, NULL);

	// Build RT acceleration structures and pipeline on first call
	if (!r->escape_rt_initialized) {
		fprintf(stderr, "[Escape] Building RT acceleration structures...\n");
		renderer_escape_build_blas(r);
		renderer_escape_build_tlas(r, data, n);
		renderer_escape_create_rt_pipeline(r, r->escape_physics_buffer);
		r->escape_rt_supported = true;
		r->escape_rt_initialized = true;
	} else {
		// Physics buffer was recreated — rebind it in the RT descriptor set
		renderer_escape_update_rt_physics_buffer(r, r->escape_physics_buffer);
	}
}

// ============================================================================
// Record a single simulation iteration into command buffer
// ============================================================================

static void escape_record_iteration(VkCommandBuffer cmd, Renderer *r, EscapeSimParams *params, uint32_t node_count, uint32_t edge_count, uint32_t frame_index)
{
	vkCmdFillBuffer(cmd, r->escape_global_stress_buffer, 0, sizeof(float), 0);

	// Barrier: transfer -> compute
	VkMemoryBarrier clearBarrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &clearBarrier, 0, NULL, 0, NULL);

	// RT Pass: compute escape vectors via ray tracing (writes escape_vector in physics buffer)
	if (r->escape_rt_supported) {
		// Barrier: previous compute writes / host writes -> RT read (memory domain visibility)
		VkMemoryBarrier preRtBarrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_SHADER_READ_BIT};
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, 0, 1, &preRtBarrier, 0, NULL, 0, NULL);

		renderer_escape_record_rt_pass(cmd, r, node_count, frame_index);

		// Barrier: RT shaders -> compute (physics reads escape_vector)
		VkMemoryBarrier rtBarrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_SHADER_READ_BIT};
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &rtBarrier, 0, NULL, 0, NULL);
	}

	// Dispatch physics (force blend)
	uint32_t group_nodes = (node_count + 255) / 256;
	params->node_count = node_count;
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, r->escape_physics_pipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, r->escape_physics_pipeline_layout, 0, 1, &r->escape_physics_desc_set, 0, NULL);
	vkCmdPushConstants(cmd, r->escape_physics_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(EscapeSimParams), params);
	vkCmdDispatch(cmd, group_nodes, 1, 1);
	// Barrier: physics -> stress
	VkMemoryBarrier physicsBarrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_SHADER_READ_BIT};
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &physicsBarrier, 0, NULL, 0, NULL);

	if (edge_count > 0) {
		uint32_t group_edges = (edge_count + 255) / 256;
		struct
		{
			uint32_t edge_count;
		} stress_pc = {edge_count};

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, r->escape_stress_pipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, r->escape_stress_pipeline_layout, 0, 1, &r->escape_stress_desc_set, 0, NULL);
		vkCmdPushConstants(cmd, r->escape_stress_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(stress_pc), &stress_pc);
		vkCmdDispatch(cmd, group_edges, 1, 1);
	}
}

// ============================================================================
// Read back final positions from GPU to GraphData
// ============================================================================

static uint32_t escape_readback_positions(Renderer *r, GraphData *data)
{
	VkDeviceSize phys_size = sizeof(NodePhysicsGPU) * data->node_count;
	NodePhysicsGPU *phys = (NodePhysicsGPU *)malloc(phys_size);

	void *mapped;
	vkMapMemory(r->core.device, r->escape_physics_memory, 0, phys_size, 0, &mapped);
	memcpy(phys, mapped, phys_size);
	vkUnmapMemory(r->core.device, r->escape_physics_memory);

	// Static snapshot of initial positions for stuck-node detection
	static float *initial_pos = NULL;
	static uint32_t initial_n = 0;
	if (!initial_pos) {
		initial_n = data->node_count;
		initial_pos = (float *)malloc(initial_n * 3 * sizeof(float));
		for (uint32_t i = 0; i < initial_n; i++) {
			initial_pos[i * 3 + 0] = phys[i].position[0];
			initial_pos[i * 3 + 1] = phys[i].position[1];
			initial_pos[i * 3 + 2] = phys[i].position[2];
		}
	}

	// Per-frame displacement tracking for motion-based convergence
	static float *prev_pos = NULL;
	static uint32_t prev_n = 0;
	double total_displacement = 0.0;
	if (!prev_pos || prev_n != (uint32_t)data->node_count) {
		free(prev_pos);
		prev_n = data->node_count;
		prev_pos = (float *)malloc(prev_n * 3 * sizeof(float));
		for (uint32_t i = 0; i < prev_n; i++) {
			prev_pos[i * 3 + 0] = phys[i].position[0];
			prev_pos[i * 3 + 1] = phys[i].position[1];
			prev_pos[i * 3 + 2] = phys[i].position[2];
		}
		r->escape_prev_displacement = 1.0f;
	} else {
		for (uint32_t i = 0; i < (uint32_t)data->node_count; i++) {
			float dx = phys[i].position[0] - prev_pos[i * 3 + 0];
			float dy = phys[i].position[1] - prev_pos[i * 3 + 1];
			float dz = phys[i].position[2] - prev_pos[i * 3 + 2];
			total_displacement += sqrtf(dx * dx + dy * dy + dz * dz);
			prev_pos[i * 3 + 0] = phys[i].position[0];
			prev_pos[i * 3 + 1] = phys[i].position[1];
			prev_pos[i * 3 + 2] = phys[i].position[2];
		}
		float avg_displacement = (float)(total_displacement / (double)data->node_count);
		r->escape_prev_displacement = (r->escape_bb_diag > 0.001f) ? avg_displacement / r->escape_bb_diag : 1.0f;
	}

	uint32_t sleeping = 0;
	for (uint32_t i = 0; i < (uint32_t)data->node_count; i++) {
		data->nodes[i].position[0] = phys[i].position[0];
		data->nodes[i].position[1] = phys[i].position[1];
		data->nodes[i].position[2] = phys[i].position[2];
		MATRIX(data->current_layout, i, 0) = (igraph_real_t)phys[i].position[0];
		MATRIX(data->current_layout, i, 1) = (igraph_real_t)phys[i].position[1];
		MATRIX(data->current_layout, i, 2) = (igraph_real_t)phys[i].position[2];
		if (phys[i].freedom[3] < 0.5f)
			sleeping++;
	}

	// Per-iteration debug for first 10 iters
	if (r->escape_current_iter <= 10) {
		uint32_t n = data->node_count;
		// Find max-degree node and a low-degree node
		uint32_t deg_max_idx = 0, deg_max_val = 0, deg1_idx = UINT32_MAX;
		for (uint32_t i = 0; i < n && i < 10000; i++) {
			if (data->nodes[i].degree > deg_max_val) {
				deg_max_val = data->nodes[i].degree;
				deg_max_idx = i;
			}
			if (data->nodes[i].degree == 1 && deg1_idx == UINT32_MAX)
				deg1_idx = i;
		}
		uint32_t samples[3] = {deg_max_idx, deg1_idx, (deg1_idx == 0) ? 1u : 0u};
		// Deduplicate
		if (samples[1] == samples[0])
			samples[1] = (samples[0] == 0) ? 1 : 0;
		for (int si = 0; si < 3; si++) {
			uint32_t idx = samples[si];
			if (idx >= n)
				continue;
			float px = phys[idx].position[0], py = phys[idx].position[1], pz = phys[idx].position[2];
			float ex = phys[idx].escape_vector[0], ey = phys[idx].escape_vector[1], ez = phys[idx].escape_vector[2];
			float elen = sqrtf(ex * ex + ey * ey + ez * ez);
			float aabb_scale = 5.0f;
			printf("[Escape DBG iter %4u] node[%u] deg=%u pos=(%.1f,%.1f,%.1f) |e|=%f f=%.3f esc=%u aabb=%.2f coh=%.0f\n", r->escape_current_iter, idx, data->nodes[idx].degree, px, py, pz, elen, phys[idx].freedom[0], (uint32_t)(phys[idx].freedom[2] + 0.5f), aabb_scale, phys[idx].freedom[1]);
			// Print neighbor positions and distances
			if (data->nodes[idx].degree > 0) {
				uint32_t start = 0, count = 0;
				// Recompute adjacency inline (same as escape_create_gpu_buffers)
				igraph_vector_int_t neis;
				igraph_vector_int_init(&neis, 0);
				igraph_neighbors(&data->g, &neis, (igraph_integer_t)idx, IGRAPH_ALL, IGRAPH_LOOPS, IGRAPH_NO_MULTIPLE);
				count = (uint32_t)igraph_vector_int_size(&neis);
				if (count > 0 && count <= 10) {
					printf("[Escape DBG iter %4u] node[%u] neighbors(%u):", r->escape_current_iter, idx, count);
					for (uint32_t j = 0; j < count; j++) {
						uint32_t nid = (uint32_t)VECTOR(neis)[j];
						float nx = phys[nid].position[0], ny = phys[nid].position[1], nz = phys[nid].position[2];
						float dist = sqrtf((px - nx) * (px - nx) + (py - ny) * (py - ny) + (pz - nz) * (pz - nz));
						float nscale = 5.0f;
						printf(" [%u]->%.1f(aabb=%.1f+%.1f=%.1f)", nid, dist, aabb_scale, nscale, aabb_scale + nscale);
					}
					printf("\n");
				}
				igraph_vector_int_destroy(&neis);
			}
		}
	}

	// Comprehensive stats every 50 iters
	if (r->escape_current_iter % 50 == 0 || r->escape_current_iter == 1) {
		uint32_t n = data->node_count;
		float min_pl = FLT_MAX, max_pl = 0, sum_pl = 0;
		float min_x = FLT_MAX, max_x = -FLT_MAX;
		float min_y = FLT_MAX, max_y = -FLT_MAX;
		float min_z = FLT_MAX, max_z = -FLT_MAX;
		int e_hist[5] = {0};   // 0, (0,0.01], (0.01,0.1], (0.1,0.5], >0.5
		int f_hist[5] = {0};   // 0, (0,0.25], (0.25,0.5], (0.5,0.75], >0.75
		int esc_hist[5] = {0}; // escaped_count buckets: 0, 1-4, 5-8, 9-12, 13-16
		int cohesion_hit = 0, cohesion_escaped = 0, cohesion_none = 0;
		uint32_t stuck = 0;
		uint32_t deg0_idx = UINT32_MAX, deg_max_idx = UINT32_MAX, deg_avg_idx = UINT32_MAX;
		uint32_t deg_max_val = 0;
		for (uint32_t i = 0; i < n; i++) {
			float px = phys[i].position[0], py = phys[i].position[1], pz = phys[i].position[2];
			float pl = sqrtf(px * px + py * py + pz * pz);
			if (pl < min_pl)
				min_pl = pl;
			if (pl > max_pl)
				max_pl = pl;
			sum_pl += pl;
			if (px < min_x)
				min_x = px;
			if (px > max_x)
				max_x = px;
			if (py < min_y)
				min_y = py;
			if (py > max_y)
				max_y = py;
			if (pz < min_z)
				min_z = pz;
			if (pz > max_z)
				max_z = pz;

			float elen = sqrtf(phys[i].escape_vector[0] * phys[i].escape_vector[0] + phys[i].escape_vector[1] * phys[i].escape_vector[1] + phys[i].escape_vector[2] * phys[i].escape_vector[2]);
			if (elen == 0.0f)
				e_hist[0]++;
			else if (elen <= 0.01f)
				e_hist[1]++;
			else if (elen <= 0.1f)
				e_hist[2]++;
			else if (elen <= 0.5f)
				e_hist[3]++;
			else
				e_hist[4]++;

			float f = phys[i].freedom[0];
			if (f == 0.0f)
				f_hist[0]++;
			else if (f <= 0.25f)
				f_hist[1]++;
			else if (f <= 0.5f)
				f_hist[2]++;
			else if (f <= 0.75f)
				f_hist[3]++;
			else
				f_hist[4]++;

			// Debug: hitCount from freedom.y, escaped_count from freedom.z
			uint32_t ec = (uint32_t)(phys[i].freedom[2] + 0.5f);
			if (ec == 0)
				esc_hist[0]++;
			else if (ec <= 4)
				esc_hist[1]++;
			else if (ec <= 8)
				esc_hist[2]++;
			else if (ec <= 12)
				esc_hist[3]++;
			else
				esc_hist[4]++;

			float cohesion_val = phys[i].freedom[1];
			if (cohesion_val < 0.5f)
				cohesion_none++;
			else if (cohesion_val < 1.5f)
				cohesion_hit++;
			else
				cohesion_escaped++;

			float dx = px - initial_pos[i * 3 + 0], dy = py - initial_pos[i * 3 + 1], dz = pz - initial_pos[i * 3 + 2];
			if (sqrtf(dx * dx + dy * dy + dz * dz) < 0.001f)
				stuck++;

			uint32_t deg = data->nodes[i].degree;
			if (deg == 0 && deg0_idx == UINT32_MAX)
				deg0_idx = i;
			if (deg > deg_max_val) {
				deg_max_val = deg;
				deg_max_idx = i;
			}
		}
		float avg_pl = sum_pl / (float)n;

		// Find one node near average degree
		int32_t avg_deg_target = (int32_t)((float)data->edge_count / (float)n + 0.5f);
		int32_t best_diff = INT32_MAX;
		for (uint32_t i = 0; i < n && i < 1000; i++) {
			int32_t diff = abs((int32_t)data->nodes[i].degree - avg_deg_target);
			if (diff < best_diff) {
				best_diff = diff;
				deg_avg_idx = i;
			}
		}

		printf("[Escape STATS iter %4u] pos: |p|=[%.1f..%.1f] avg=%.1f  bounds X=[%.1f,%.1f] Y=[%.1f,%.1f] Z=[%.1f,%.1f]\n", r->escape_current_iter, min_pl, max_pl, avg_pl, min_x, max_x, min_y, max_y, min_z, max_z);
		printf("[Escape STATS iter %4u] |e|: zero=%d tiny=%d small=%d mid=%d large=%d  freedom: zero=%d low=%d mid=%d high=%d full=%d\n", r->escape_current_iter, e_hist[0], e_hist[1], e_hist[2], e_hist[3], e_hist[4], f_hist[0], f_hist[1], f_hist[2], f_hist[3], f_hist[4]);
		printf("[Escape STATS iter %4u] escaped: 0=%d 1-4=%d 5-8=%d 9-12=%d 13-16=%d\n", r->escape_current_iter, esc_hist[0], esc_hist[1], esc_hist[2], esc_hist[3], esc_hist[4]);
		printf("[Escape STATS iter %4u] cohesion: hit=%d escaped=%d none=%d\n", r->escape_current_iter, cohesion_hit, cohesion_escaped, cohesion_none);
		printf("[Escape STATS iter %4u] stuck=%u/%u sleeping=%u disp_rel=%.6f conv_count=%u\n", r->escape_current_iter, stuck, n, sleeping, r->escape_prev_displacement, r->escape_convergence_count);

		// Sample nodes: degree=0, degree=max, degree≈avg
		uint32_t sample_ids[] = {deg0_idx, deg_max_idx, deg_avg_idx};
		const char *sample_labels[] = {"deg=0", "deg=max", "deg~avg"};
		for (int si = 0; si < 3; si++) {
			uint32_t idx = sample_ids[si];
			if (idx >= n)
				continue;
			float px = phys[idx].position[0], py = phys[idx].position[1], pz = phys[idx].position[2];
			float pl = sqrtf(px * px + py * py + pz * pz);
			float ex = phys[idx].escape_vector[0], ey = phys[idx].escape_vector[1], ez = phys[idx].escape_vector[2];
			float el = sqrtf(ex * ex + ey * ey + ez * ez);
			uint32_t ec = (uint32_t)(phys[idx].freedom[2] + 0.5f);
			float coh = phys[idx].freedom[1];
			const char *coh_str = (coh < 0.5f) ? "none" : ((coh < 1.5f) ? "hit" : "escaped");
			printf("[Escape SAMPLE iter %4u] node[%u] %s deg=%u pos=(%.1f,%.1f,%.1f) |p|=%.1f  |e|=%.4f  f=%.3f escaped=%u/%u coh=%s awake=%d\n", r->escape_current_iter, idx, sample_labels[si], data->nodes[idx].degree, px, py, pz, pl, el, phys[idx].freedom[0], ec, 16, coh_str, phys[idx].freedom[3] >= 0.5f ? 1 : 0);
		}
	}

	free(phys);
	renderer_update_graph(r, data);
	return sleeping;
}

// ============================================================================
// Setup-only apply: applies initial Hilbert positions then starts GPU sim
// ============================================================================

void apply_escape_layout(ExecutionContext *ctx, void *result_data)
{
	printf("[Escape] Apply: initial positions\n");
	apply_layout_matrix(ctx, result_data);

	if (!ctx || !ctx->app_state) {
		fprintf(stderr, "[Escape] Apply: no app_state, skipping GPU drive\n");
		return;
	}

	AppState *state = ctx->app_state;
	Renderer *r = &state->renderer;
	GraphData *data = &state->current_graph;
	uint32_t n = data->node_count;
	uint32_t m = data->edge_count;

	if (n == 0)
		return;

	// Compute bounding box diagonal
	float bb_min_x = FLT_MAX, bb_max_x = -FLT_MAX;
	float bb_min_y = FLT_MAX, bb_max_y = -FLT_MAX;
	float bb_min_z = FLT_MAX, bb_max_z = -FLT_MAX;
	for (uint32_t i = 0; i < n; i++) {
		float x = data->nodes[i].position[0], y = data->nodes[i].position[1], z = data->nodes[i].position[2];
		if (x < bb_min_x)
			bb_min_x = x;
		if (x > bb_max_x)
			bb_max_x = x;
		if (y < bb_min_y)
			bb_min_y = y;
		if (y > bb_max_y)
			bb_max_y = y;
		if (z < bb_min_z)
			bb_min_z = z;
		if (z > bb_max_z)
			bb_max_z = z;
	}
	float bb_diag = sqrtf((bb_max_x - bb_min_x) * (bb_max_x - bb_min_x) + (bb_max_y - bb_min_y) * (bb_max_y - bb_min_y) + (bb_max_z - bb_min_z) * (bb_max_z - bb_min_z));
	r->escape_bb_diag = bb_diag;
	printf("[Escape] Setup: %u nodes %u edges bb_diag=%.1f\n", n, m, bb_diag);

	escape_ensure_command_resources(r);
	escape_create_gpu_buffers(r, data);

	// --- Dynamic parameter scaling based on graph properties ---
	float avg_degree = (n > 0) ? (float)m / (float)n : 1.0f;
	float density = (n > 1) ? (float)m / ((float)n * (float)(n - 1)) : 0.0f;

	float alpha0 = 10.0f;

	printf("[Escape] Params: avg_deg=%.1f density=%.4f alpha=%.3f\n", avg_degree, density, alpha0);

	// Store simulation state in Renderer for per-frame tick
	r->escape_running = true;
	r->escape_sim_active = true;
	r->escape_needs_wait = false;
	r->escape_current_iter = 0;
	r->escape_max_iters = 20000;
	r->escape_alpha = alpha0;
	r->escape_avg_degree = avg_degree;
	r->escape_prev_displacement = 1.0f;
	r->escape_convergence_count = 0;
	r->escape_node_count = n;
	r->escape_edge_count = m;
	r->escape_graph_data = data;
}

// ============================================================================
// Cleanup escape GPU resources
// ============================================================================

void igraph_vlk_layout_escape_cleanup(Renderer *r)
{
	renderer_escape_cleanup_rt(r);

	VK_DESTROY_BUFFER(r->core.device, r->escape_physics_buffer, r->escape_physics_memory);
	VK_DESTROY_BUFFER(r->core.device, r->escape_adjacency_buffer, r->escape_adjacency_memory);
	VK_DESTROY_BUFFER(r->core.device, r->escape_offsets_buffer, r->escape_offsets_memory);
	VK_DESTROY_BUFFER(r->core.device, r->escape_counts_buffer, r->escape_counts_memory);
	VK_DESTROY_BUFFER(r->core.device, r->escape_edges_buffer, r->escape_edges_memory);
	VK_DESTROY_BUFFER(r->core.device, r->escape_global_stress_buffer, r->escape_global_stress_memory);

	if (r->escape_fence != VK_NULL_HANDLE) {
		vkDestroyFence(r->core.device, r->escape_fence, NULL);
		r->escape_fence = VK_NULL_HANDLE;
	}
	if (r->escape_cmd_buf != VK_NULL_HANDLE) {
		vkFreeCommandBuffers(r->core.device, r->escape_cmd_pool, 1, &r->escape_cmd_buf);
		r->escape_cmd_buf = VK_NULL_HANDLE;
	}
	if (r->escape_cmd_pool != VK_NULL_HANDLE) {
		vkDestroyCommandPool(r->core.device, r->escape_cmd_pool, NULL);
		r->escape_cmd_pool = VK_NULL_HANDLE;
	}

	if (r->escape_descriptor_pool != VK_NULL_HANDLE) {
		vkDestroyDescriptorPool(r->core.device, r->escape_descriptor_pool, NULL);
		r->escape_descriptor_pool = VK_NULL_HANDLE;
	}
	if (r->escape_physics_pipeline != VK_NULL_HANDLE) {
		vkDestroyPipeline(r->core.device, r->escape_physics_pipeline, NULL);
		r->escape_physics_pipeline = VK_NULL_HANDLE;
	}
	if (r->escape_stress_pipeline != VK_NULL_HANDLE) {
		vkDestroyPipeline(r->core.device, r->escape_stress_pipeline, NULL);
		r->escape_stress_pipeline = VK_NULL_HANDLE;
	}
	if (r->escape_physics_pipeline_layout != VK_NULL_HANDLE) {
		vkDestroyPipelineLayout(r->core.device, r->escape_physics_pipeline_layout, NULL);
		r->escape_physics_pipeline_layout = VK_NULL_HANDLE;
	}
	if (r->escape_stress_pipeline_layout != VK_NULL_HANDLE) {
		vkDestroyPipelineLayout(r->core.device, r->escape_stress_pipeline_layout, NULL);
		r->escape_stress_pipeline_layout = VK_NULL_HANDLE;
	}
	if (r->escape_physics_desc_layout != VK_NULL_HANDLE) {
		vkDestroyDescriptorSetLayout(r->core.device, r->escape_physics_desc_layout, NULL);
		r->escape_physics_desc_layout = VK_NULL_HANDLE;
	}
	if (r->escape_stress_desc_layout != VK_NULL_HANDLE) {
		vkDestroyDescriptorSetLayout(r->core.device, r->escape_stress_desc_layout, NULL);
		r->escape_stress_desc_layout = VK_NULL_HANDLE;
	}

	r->escape_initialized = VK_FALSE;
	r->escape_rt_initialized = false;
}
