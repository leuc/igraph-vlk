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
	igraph_integer_t topo_pos;
} NodeTopology;

static int compare_topology(const void *a, const void *b)
{
	const NodeTopology *nodeA = (const NodeTopology *)a;
	const NodeTopology *nodeB = (const NodeTopology *)b;
	if (nodeA->topo_pos != nodeB->topo_pos)
		return (int)(nodeA->topo_pos - nodeB->topo_pos);
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
// CPU worker: topology analysis + space-filling curve placement
// ============================================================================

void *compute_escape_layout(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	fprintf(stderr, "[Escape] Worker: vcount=%ld\n", (long)vcount);
	if (vcount == 0)
		return NULL;

	NodeTopology *sorted = (NodeTopology *)malloc(vcount * sizeof(NodeTopology));
	if (!sorted)
		return NULL;

	igraph_vector_int_t degrees;
	igraph_vector_int_init(&degrees, vcount);
	igraph_degree(graph, &degrees, igraph_vss_all(), IGRAPH_ALL, IGRAPH_LOOPS_ONCE);

	igraph_vector_int_t topo_order;
	igraph_vector_int_init(&topo_order, 0);
	igraph_error_t topo_ret = igraph_topological_sorting(graph, &topo_order, IGRAPH_OUT);
	if (topo_ret != IGRAPH_SUCCESS) {
		fprintf(stderr, "[Escape] Topological sorting failed (graph likely cyclic), using degree-only ordering\n");
		igraph_vector_int_destroy(&degrees);
		igraph_vector_int_destroy(&topo_order);
		free(sorted);
		return NULL;
	}

	igraph_vector_int_t rank;
	igraph_vector_int_init(&rank, vcount);
	for (igraph_integer_t i = 0; i < (igraph_integer_t)igraph_vector_int_size(&topo_order); i++)
		VECTOR(rank)[VECTOR(topo_order)[i]] = i;

	igraph_integer_t max_deg = 0;
	for (igraph_integer_t i = 0; i < vcount; i++) {
		sorted[i].id = i;
		sorted[i].degree = VECTOR(degrees)[i];
		sorted[i].topo_pos = VECTOR(rank)[i];
		if (sorted[i].degree > max_deg)
			max_deg = sorted[i].degree;
	}

	igraph_vector_int_destroy(&degrees);
	igraph_vector_int_destroy(&topo_order);
	igraph_vector_int_destroy(&rank);

	qsort(sorted, vcount, sizeof(NodeTopology), compare_topology);

	igraph_matrix_t *result = IGRAPH_MALLOC(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 3) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		free(sorted);
		return NULL;
	}

	int *sorted_rank = (int *)malloc(vcount * sizeof(int));
	if (!sorted_rank) {
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

	float min_x = INFINITY, max_x = -INFINITY, min_y = INFINITY, max_y = -INFINITY, min_z = INFINITY, max_z = -INFINITY;
	for (igraph_integer_t i = 0; i < vcount; i++) {
		float px, py, pz;
		get_hilbert_3d_position(sorted_rank[i], hilbert_order, &px, &py, &pz, 4.0f);
		float core_scale = 1.0f + log2f((float)sorted[i].degree + 2.0f) * 0.5f;
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
	fprintf(stderr, "[Escape] Worker: %ld nodes max_deg=%ld bounds X=[%.0f,%.0f] Y=[%.0f,%.0f] Z=[%.0f,%.0f]\n", (long)vcount, (long)max_deg, min_x, max_x, min_y, max_y, min_z, max_z);

	free(sorted_rank);
	free(sorted);
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

		// Debug: sample a few nodes' escape vectors and positions every 10 iters
		if (r->escape_current_iter % 10 == 0 && r->escape_graph_data) {
			VkDeviceSize phys_size = sizeof(NodePhysicsGPU) * n;
			NodePhysicsGPU *phys = (NodePhysicsGPU *)malloc(phys_size);
			void *mapped;
			vkMapMemory(r->core.device, r->escape_physics_memory, 0, phys_size, 0, &mapped);
			memcpy(phys, mapped, phys_size);
			vkUnmapMemory(r->core.device, r->escape_physics_memory);

			// Sample nodes 0, n/4, n/2, 3n/4, n-1
			uint32_t samples[] = {0, n / 4, n / 2, (3 * n) / 4, n - 1};
			float avg_escape_len = 0.0f;
			float avg_pos_len = 0.0f;
			float avg_freedom = 0.0f;
			for (uint32_t s = 0; s < 5 && samples[s] < n; s++) {
				uint32_t idx = samples[s];
				float ex = phys[idx].escape_vector[0], ey = phys[idx].escape_vector[1], ez = phys[idx].escape_vector[2];
				float el = sqrtf(ex * ex + ey * ey + ez * ez);
				float px = phys[idx].position[0], py = phys[idx].position[1], pz = phys[idx].position[2];
				float pl = sqrtf(px * px + py * py + pz * pz);
				float f = phys[idx].freedom[0];
				avg_escape_len += el;
				avg_pos_len += pl;
				avg_freedom += f;
				if (r->escape_current_iter == 10 || s == 0)
					printf("[Escape]   node[%u] pos=(%.2f,%.2f,%.2f) r=%.2f  escape=(%.6f,%.6f,%.6f) |e|=%.6f  f=%.3f\n", idx, px, py, pz, pl, ex, ey, ez, el, f);
			}
			avg_escape_len /= 5.0f;
			avg_pos_len /= 5.0f;
			avg_freedom /= 5.0f;
			printf("[Escape] iter %4u | sleeping=%u/%u avg |escape|=%.6f  avg |pos|=%.2f  avg freedom=%.3f\n", r->escape_current_iter, sleeping, n, avg_escape_len, avg_pos_len, avg_freedom);

			free(phys);
		}

		// CPU-side TLAS update for next frame's RT pass
		if (r->escape_rt_supported) {
			renderer_escape_update_tlas_cpu(r, n);
		}

		// Freedom-based convergence: all nodes asleep = done
		if (sleeping == n) {
			printf("[Escape] All nodes asleep at iteration %u\n", r->escape_current_iter);
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

	if (r->escape_current_iter % 10 == 0 || r->escape_current_iter == r->escape_max_iters)
		printf("[Escape] iter %4u / %u | alpha=%.3f\n", r->escape_current_iter, r->escape_max_iters, r->escape_alpha);

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

	float alpha0 = 5.0f;

	printf("[Escape] Params: avg_deg=%.1f density=%.4f alpha=%.3f\n", avg_degree, density, alpha0);

	// Store simulation state in Renderer for per-frame tick
	r->escape_running = true;
	r->escape_sim_active = true;
	r->escape_needs_wait = false;
	r->escape_current_iter = 0;
	r->escape_max_iters = 2000;
	r->escape_alpha = alpha0;
	r->escape_avg_degree = avg_degree;
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
