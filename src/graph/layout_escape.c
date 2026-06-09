#define _GNU_SOURCE

#include "app_state.h"
#include "graph/wrappers_layout.h"
#include "interaction/state.h"
#include "vulkan/buffers.h"
#include "vulkan/renderer.h"
#include "vulkan/utils.h"
#include <float.h>
#include <igraph.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// Internal structs for topology-aware sorting
// ============================================================================

typedef struct
{
	igraph_integer_t id;
	igraph_integer_t degree;
	igraph_integer_t coreness;
} NodeTopology;

static int compare_topology(const void *a, const void *b)
{
	const NodeTopology *nodeA = (const NodeTopology *)a;
	const NodeTopology *nodeB = (const NodeTopology *)b;
	if (nodeA->coreness != nodeB->coreness)
		return (int)(nodeB->coreness - nodeA->coreness);
	return (int)(nodeB->degree - nodeA->degree);
}

// ============================================================================
// Proper 3D Hilbert space-filling curve
// Based on John Skilling's algorithm (AIP Conf. Proc. 707, 381, 2004)
// ============================================================================

// State transition table: next[state][octant] -> next state
static const int hilbert_next[8][8] = {
	{0, 3, 4, 7, 6, 1, 2, 5}, {7, 0, 5, 2, 1, 6, 3, 4}, {6, 7, 2, 5, 4, 3, 0, 1}, {1, 6, 3, 0, 7, 4, 5, 2}, {2, 5, 6, 1, 0, 7, 4, 3}, {3, 4, 7, 6, 5, 2, 1, 0}, {4, 1, 0, 3, 2, 5, 6, 7}, {5, 2, 1, 4, 3, 0, 7, 6},
};

// Coordinate transform table: trans[state][octant] -> transformed octant bits
static const int hilbert_trans[8][8] = {
	{0, 1, 3, 2, 6, 7, 5, 4}, {2, 3, 1, 0, 4, 5, 7, 6}, {4, 5, 7, 6, 0, 1, 3, 2}, {6, 7, 5, 4, 2, 3, 1, 0}, {1, 0, 2, 3, 7, 6, 4, 5}, {3, 2, 0, 1, 5, 4, 6, 7}, {5, 4, 6, 7, 1, 0, 2, 3}, {7, 6, 4, 5, 3, 2, 0, 1},
};

// Maps a Hilbert index to 3D coordinates in a 2^order cube
// order = number of bits per dimension (e.g., 5 gives a 32^3 grid)
static void get_hilbert_3d_position(int rank, int order, float *x, float *y, float *z, float spacing)
{
	int cx = 0, cy = 0, cz = 0;
	int state = 0;

	for (int i = order - 1; i >= 0; i--) {
		int octant = (rank >> (3 * i)) & 7;
		int t = hilbert_trans[state][octant];
		cx = (cx << 1) | (t & 1);
		cy = (cy << 1) | ((t >> 1) & 1);
		cz = (cz << 1) | ((t >> 2) & 1);
		state = hilbert_next[state][octant];
	}

	// Center the grid around 0 and apply spacing
	float half = (float)(1 << (order - 1));
	*x = ((float)cx - half) * spacing;
	*y = ((float)cy - half) * spacing;
	*z = ((float)cz - half) * spacing;
}

// ============================================================================
// GPU-side physics buffer types (must match shader layout)
// ============================================================================

typedef struct
{
	float position[4]; // xyz = pos, w = mass
	float velocity[4]; // xyz = vel, w = padding
	float escape_vector[4];
} NodePhysicsGPU;

typedef struct
{
	uint32_t nodeA;
	uint32_t nodeB;
} EdgeGPU;

// ============================================================================
// CPU worker: topology analysis + space-filling curve placement
// ============================================================================

void *compute_escape_layout(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	fprintf(stderr, "[Escape DEBUG] CPU worker: vcount=%ld\n", (long)vcount);
	if (vcount == 0) {
		fprintf(stderr, "[Escape DEBUG] CPU worker: empty graph, returning NULL\n");
		return NULL;
	}

	NodeTopology *sorted = (NodeTopology *)malloc(vcount * sizeof(NodeTopology));
	if (!sorted) {
		fprintf(stderr, "[Escape DEBUG] CPU worker: malloc failed for sorted\n");
		return NULL;
	}

	igraph_vector_int_t degrees, coreness;
	igraph_vector_int_init(&degrees, vcount);
	igraph_vector_int_init(&coreness, vcount);

	igraph_degree(graph, &degrees, igraph_vss_all(), IGRAPH_ALL, IGRAPH_LOOPS_ONCE);
	igraph_coreness(graph, &coreness, IGRAPH_ALL);

	igraph_integer_t max_deg = 0, max_core = 0;
	igraph_integer_t core_bins[10] = {0};
	for (igraph_integer_t i = 0; i < vcount; i++) {
		sorted[i].id = i;
		sorted[i].degree = VECTOR(degrees)[i];
		sorted[i].coreness = VECTOR(coreness)[i];
		if (sorted[i].degree > max_deg)
			max_deg = sorted[i].degree;
		if (sorted[i].coreness > max_core)
			max_core = sorted[i].coreness;
		int bin = (sorted[i].coreness < 9) ? (int)sorted[i].coreness : 9;
		core_bins[bin]++;
	}
	fprintf(stderr, "[Escape DEBUG] CPU worker: max_degree=%ld max_coreness=%ld\n", (long)max_deg, (long)max_core);
	fprintf(stderr, "[Escape DEBUG] CPU worker: coreness bins: ");
	for (int b = 0; b < 10; b++)
		fprintf(stderr, "%ld ", (long)core_bins[b]);
	fprintf(stderr, "\n");

	igraph_vector_int_destroy(&degrees);
	igraph_vector_int_destroy(&coreness);

	qsort(sorted, vcount, sizeof(NodeTopology), compare_topology);
	fprintf(stderr, "[Escape DEBUG] CPU worker: sorted nodes by coreness+degree\n");

	igraph_matrix_t *result = IGRAPH_MALLOC(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 3) != IGRAPH_SUCCESS) {
		fprintf(stderr, "[Escape DEBUG] CPU worker: igraph_matrix_init failed\n");
		IGRAPH_FREE(result);
		free(sorted);
		return NULL;
	}

	int *sorted_rank = (int *)malloc(vcount * sizeof(int));
	if (!sorted_rank) {
		fprintf(stderr, "[Escape DEBUG] CPU worker: malloc failed for sorted_rank\n");
		igraph_matrix_destroy(result);
		IGRAPH_FREE(result);
		free(sorted);
		return NULL;
	}
	for (igraph_integer_t i = 0; i < vcount; i++)
		sorted_rank[sorted[i].id] = i;

	int hilbert_order = (int)(ceil(log2((double)vcount) / 3.0));
	if (hilbert_order < 1)
		hilbert_order = 1;
	if (hilbert_order > 10)
		hilbert_order = 10;
	fprintf(stderr, "[Escape DEBUG] CPU worker: hilbert_order=%d grid=%d^3\n", hilbert_order, 1 << hilbert_order);

	float min_x = INFINITY, max_x = -INFINITY, min_y = INFINITY, max_y = -INFINITY, min_z = INFINITY, max_z = -INFINITY;
	for (igraph_integer_t i = 0; i < vcount; i++) {
		float px, py, pz;
		get_hilbert_3d_position(sorted_rank[i], hilbert_order, &px, &py, &pz, 2.0f);
		float core_scale = 1.0f + (float)sorted[i].coreness * 0.5f;
		MATRIX(*result, i, 0) = (igraph_real_t)(px * core_scale);
		MATRIX(*result, i, 1) = (igraph_real_t)(py * core_scale);
		MATRIX(*result, i, 2) = (igraph_real_t)(pz * core_scale);
		float x = (float)MATRIX(*result, i, 0), y = (float)MATRIX(*result, i, 1), z = (float)MATRIX(*result, i, 2);
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
	fprintf(stderr, "[Escape DEBUG] CPU worker: position bounds X=[%.2f,%.2f] Y=[%.2f,%.2f] Z=[%.2f,%.2f]\n", min_x, max_x, min_y, max_y, min_z, max_z);
	fprintf(stderr, "[Escape DEBUG] CPU worker: first 3 pos: (%.2f,%.2f,%.2f) (%.2f,%.2f,%.2f) (%.2f,%.2f,%.2f)\n", (float)MATRIX(*result, 0, 0), (float)MATRIX(*result, 0, 1), (float)MATRIX(*result, 0, 2), (float)MATRIX(*result, 1, 0), (float)MATRIX(*result, 1, 1), (float)MATRIX(*result, 1, 2), (float)MATRIX(*result, 2, 0), (float)MATRIX(*result, 2, 1), (float)MATRIX(*result, 2, 2));
	fprintf(stderr, "[Escape DEBUG] CPU worker: computed %ld Hilbert positions, returning matrix\n", (long)vcount);

	free(sorted_rank);
	free(sorted);
	return result;
}

// ============================================================================
// GPU simulation context management
// ============================================================================

typedef struct
{
	float dt;
	float alpha;
	float beta;
	float ideal_length;
	float friction;
	uint32_t node_count;
} EscapeSimParams;

static void escape_ensure_command_resources(Renderer *r)
{
	if (r->escape_cmd_pool != VK_NULL_HANDLE) {
		fprintf(stderr, "[Escape DEBUG] Command resources already exist\n");
		return;
	}
	fprintf(stderr, "[Escape DEBUG] Creating command pool, buffer, and fence\n");

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
	fprintf(stderr, "[Escape DEBUG] Creating GPU buffers: nodes=%u edges=%u\n", n, m);

	// Compute adjacency data from igraph
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

		fprintf(stderr, "[Escape DEBUG] Building adjacency: n=%u total_neighbors=%u\n", n, total_edges);

		adj_neighbors = (uint32_t *)calloc(total_edges, sizeof(uint32_t));
		uint32_t idx = 0;
		for (uint32_t i = 0; i < n; i++) {
			igraph_neighbors(&data->g, &neis, (igraph_integer_t)i, IGRAPH_ALL, IGRAPH_LOOPS, IGRAPH_NO_MULTIPLE);
			for (uint32_t j = 0; j < (uint32_t)igraph_vector_int_size(&neis); j++)
				adj_neighbors[idx++] = (uint32_t)VECTOR(neis)[j];
		}
		igraph_vector_int_destroy(&neis);
	} else {
		fprintf(stderr, "[Escape DEBUG] No nodes, skipping adjacency\n");
	}

	// Build edge list for stress calculation
	EdgeGPU *edges = NULL;
	if (m > 0) {
		edges = (EdgeGPU *)calloc(m, sizeof(EdgeGPU));
		for (uint32_t i = 0; i < m; i++) {
			edges[i].nodeA = data->edges[i].from;
			edges[i].nodeB = data->edges[i].to;
		}
		fprintf(stderr, "[Escape DEBUG] Built edge list: m=%u\n", m);
	} else {
		fprintf(stderr, "[Escape DEBUG] No edges for stress calc\n");
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

	// Upload initial physics data
	NodePhysicsGPU *phys = (NodePhysicsGPU *)calloc(n, sizeof(NodePhysicsGPU));
	for (uint32_t i = 0; i < n; i++) {
		phys[i].position[0] = data->nodes[i].position[0];
		phys[i].position[1] = data->nodes[i].position[1];
		phys[i].position[2] = data->nodes[i].position[2];
		phys[i].position[3] = 1.0f; // mass
									// velocity and escape_vector are zero-initialized
	}
	fprintf(stderr, "[Escape DEBUG] Uploading %u nodes to GPU: first pos=(%.2f,%.2f,%.2f) last pos=(%.2f,%.2f,%.2f)\n", n, phys[0].position[0], phys[0].position[1], phys[0].position[2], phys[n - 1].position[0], phys[n - 1].position[1], phys[n - 1].position[2]);
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

	fprintf(stderr, "[Escape DEBUG] GPU buffers created and descriptors updated\n");
}

// ============================================================================
// Record a single simulation iteration into command buffer
// ============================================================================

static void escape_record_iteration(VkCommandBuffer cmd, Renderer *r, EscapeSimParams *params, uint32_t node_count, uint32_t edge_count)
{
	// Clear stress buffer
	vkCmdFillBuffer(cmd, r->escape_global_stress_buffer, 0, sizeof(float), 0);
	fprintf(stderr, "[Escape RECORD] Filled stress buffer with 0\n");

	// Barrier: transfer -> compute
	VkMemoryBarrier clearBarrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &clearBarrier, 0, NULL, 0, NULL);

	// Dispatch physics (force blend)
	uint32_t group_nodes = (node_count + 255) / 256;
	params->node_count = node_count;
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, r->escape_physics_pipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, r->escape_physics_pipeline_layout, 0, 1, &r->escape_physics_desc_set, 0, NULL);
	vkCmdPushConstants(cmd, r->escape_physics_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(EscapeSimParams), params);
	vkCmdDispatch(cmd, group_nodes, 1, 1);
	fprintf(stderr, "[Escape RECORD] Dispatched physics: groups=%u node_count=%u dt=%.4f alpha=%.2f beta=%.2f ideal=%.2f friction=%.2f\n", group_nodes, node_count, params->dt, params->alpha, params->beta, params->ideal_length, params->friction);

	// Barrier: physics -> stress + TLAS
	VkMemoryBarrier physicsBarrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_SHADER_READ_BIT};
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &physicsBarrier, 0, NULL, 0, NULL);

	if (edge_count > 0) {
		uint32_t group_edges = (edge_count + 255) / 256;
		struct
		{
			float ideal_length;
			uint32_t edge_count;
		} stress_pc = {params->ideal_length, edge_count};

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, r->escape_stress_pipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, r->escape_stress_pipeline_layout, 0, 1, &r->escape_stress_desc_set, 0, NULL);
		vkCmdPushConstants(cmd, r->escape_stress_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(stress_pc), &stress_pc);
		vkCmdDispatch(cmd, group_edges, 1, 1);
		fprintf(stderr, "[Escape RECORD] Dispatched stress calc: groups=%u edge_count=%u ideal_length=%.2f\n", group_edges, edge_count, params->ideal_length);
	} else {
		fprintf(stderr, "[Escape RECORD] No edges, skipping stress calc\n");
	}
}

// ============================================================================
// Convergence check (read back stress from GPU)
// ============================================================================

static bool escape_check_convergence(Renderer *r, float epsilon)
{
	float current_stress = 0.0f;
	void *data;
	vkMapMemory(r->core.device, r->escape_global_stress_memory, 0, sizeof(float), 0, &data);
	memcpy(&current_stress, data, sizeof(float));
	vkUnmapMemory(r->core.device, r->escape_global_stress_memory);

	float delta = r->escape_previous_stress - current_stress;
	r->escape_previous_stress = current_stress;

	if (fabsf(delta) < epsilon) {
		r->escape_stable_frames++;
	} else {
		r->escape_stable_frames = 0;
	}

	fprintf(stderr, "[Escape CONV] stress=%.6f delta=%.6f stable=%u\n", current_stress, delta, r->escape_stable_frames);

	return r->escape_stable_frames >= 10;
}

// ============================================================================
// Read back final positions from GPU to GraphData
// ============================================================================

static void escape_readback_positions(Renderer *r, GraphData *data)
{
	VkDeviceSize phys_size = sizeof(NodePhysicsGPU) * data->node_count;
	NodePhysicsGPU *phys = (NodePhysicsGPU *)malloc(phys_size);

	void *mapped;
	vkMapMemory(r->core.device, r->escape_physics_memory, 0, phys_size, 0, &mapped);
	memcpy(phys, mapped, phys_size);
	vkUnmapMemory(r->core.device, r->escape_physics_memory);

	for (uint32_t i = 0; i < (uint32_t)data->node_count; i++) {
		data->nodes[i].position[0] = phys[i].position[0];
		data->nodes[i].position[1] = phys[i].position[1];
		data->nodes[i].position[2] = phys[i].position[2];
		// Update the stored layout matrix for persistence
		MATRIX(data->current_layout, i, 0) = (igraph_real_t)phys[i].position[0];
		MATRIX(data->current_layout, i, 1) = (igraph_real_t)phys[i].position[1];
		MATRIX(data->current_layout, i, 2) = (igraph_real_t)phys[i].position[2];
	}

	free(phys);
	renderer_update_graph(r, data);
}

// ============================================================================
// Main GPU simulation driver
// ============================================================================

void igraph_vlk_layout_escape_drive(AppState *state, float ideal_length, uint32_t max_iters, float epsilon)
{
	printf("[Escape DRIVE] ENTERING igraph_vlk_layout_escape_drive: ideal_length=%.2f max_iters=%u epsilon=%.6f\n", ideal_length, max_iters, epsilon);

	Renderer *r = &state->renderer;
	GraphData *data = &state->current_graph;
	uint32_t n = data->node_count;
	uint32_t m = data->edge_count;

	printf("[Escape DRIVE] nodes=%u edges=%u\n", n, m);

	if (n == 0) {
		printf("[Escape DRIVE] no nodes, returning\n");
		return;
	}

	// Initialize command resources
	escape_ensure_command_resources(r);

	// Create / update GPU buffers
	escape_create_gpu_buffers(r, data);

	// Simulation parameters
	EscapeSimParams params = {
		.dt = 0.016f,
		.alpha = 1.0f,
		.beta = 0.75f,
		.ideal_length = ideal_length,
		.friction = 0.85f,
	};

	r->escape_previous_stress = INFINITY;
	r->escape_stable_frames = 0;
	r->escape_running = true;

	printf("[Escape DRIVE] Starting simulation loop: max_iters=%u nodes=%u edges=%u\n", max_iters, n, m);

	for (uint32_t iter = 0; iter < max_iters && r->escape_running; iter++) {
		r->escape_iteration = (int)iter;
		printf("[Escape] === iter %u / %u ===\n", iter + 1, max_iters);
		// Wait for previous iteration
		if (iter > 0) {
			VK_CHECK(vkWaitForFences(r->core.device, 1, &r->escape_fence, VK_TRUE, UINT64_MAX), "Failed to wait for escape fence");
		}
		VK_CHECK(vkResetFences(r->core.device, 1, &r->escape_fence), "Failed to reset escape fence");

		// Record iteration
		VK_CHECK(vkResetCommandBuffer(r->escape_cmd_buf, 0), "Failed to reset escape command buffer");
		VK_CHECK(vkBeginCommandBuffer(r->escape_cmd_buf, &VK_CMD_BEGIN_INFO_ONETIME), "Failed to begin escape command buffer");

		escape_record_iteration(r->escape_cmd_buf, r, &params, n, m);

		VK_CHECK(vkEndCommandBuffer(r->escape_cmd_buf), "Failed to end escape command buffer");

		// Submit
		VkSubmitInfo submitInfo = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &r->escape_cmd_buf};
		VkResult res = vkQueueSubmit(r->core.graphicsQueue, 1, &submitInfo, r->escape_fence);
		if (res != VK_SUCCESS) {
			fprintf(stderr, "[escape] Queue submit failed at iteration %u\n", iter);
			r->escape_running = false;
			break;
		}

		// Wait for completion and check convergence
		VK_CHECK(vkWaitForFences(r->core.device, 1, &r->escape_fence, VK_TRUE, UINT64_MAX), "Failed to wait for escape fence on readback");
		bool converged = escape_check_convergence(r, epsilon);
		printf("[Escape] iter %4u | stress=%.6f stable=%u converged=%d\n", iter, r->escape_previous_stress, r->escape_stable_frames, converged);
		if (r->escape_stable_frames >= 10) {
			printf("[Escape Layout] Converged at iteration %u (stress: %f)\n", iter, r->escape_previous_stress);
			break;
		}

		// Cooling: reduce repulsion weight over time
		if (iter > max_iters / 2)
			params.alpha *= 0.95f;
	}

	// Read back final positions and update graph
	printf("[Escape DRIVE] Simulation loop ended, reading back final positions...\n");
	escape_readback_positions(r, data);
	r->escape_running = false;

	printf("[Escape Layout] Completed %u iterations (converged at iter %u)\n", max_iters, r->escape_iteration);
	printf("[Escape DRIVE] EXITING igraph_vlk_layout_escape_drive\n");
}

// ============================================================================
// Custom apply function: applies initial Hilbert positions then runs GPU drive
// ============================================================================

void apply_escape_layout(ExecutionContext *ctx, void *result_data)
{
	fprintf(stderr, "[Escape] apply_escape_layout: applying initial positions\n");
	apply_layout_matrix(ctx, result_data);

	if (!ctx || !ctx->app_state) {
		fprintf(stderr, "[Escape] apply_escape_layout: no app_state, skipping GPU drive\n");
		return;
	}

	AppState *state = ctx->app_state;
	igraph_integer_t n = igraph_vcount(ctx->current_graph);
	igraph_integer_t m = igraph_ecount(ctx->current_graph);
	fprintf(stderr, "[Escape] apply_escape_layout: calling GPU drive with n=%ld m=%ld\n", (long)n, (long)m);

	igraph_vlk_layout_escape_drive(state, 2.0f, 100, 0.001f);
	fprintf(stderr, "[Escape] apply_escape_layout: GPU drive completed\n");
}

// ============================================================================
// Cleanup escape GPU resources
// ============================================================================

void igraph_vlk_layout_escape_cleanup(Renderer *r)
{
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
}
