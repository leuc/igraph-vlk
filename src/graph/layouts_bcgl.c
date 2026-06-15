#include "graph/wrappers_layout.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "app_state.h"
#include "graph/graph_core.h"
#include "vulkan/renderer.h"
#include "vulkan/renderer_bcgl.h"

// ============================================================================
// Worker: Seed random 3D layout for BCGL starting positions
// ============================================================================
void *compute_layout_bcgl(igraph_t *graph)
{
	if (!graph || igraph_vcount(graph) == 0)
		return NULL;

	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = IGRAPH_MALLOC(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 3) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		return NULL;
	}

	igraph_layout_random_3d(graph, result);
	return result;
}

// ============================================================================
// Debug: Read mapped GPU memory for avg/max velocity and bounding box
// ============================================================================
static void debug_print_bcgl_stats(Renderer *r, uint32_t current_iter, uint32_t total_iters)
{
	BCGLComputeContext *ctx = &r->bcgl_ctx;
	if (ctx->node_mem == VK_NULL_HANDLE)
		return;

	igraph_integer_t n = r->nodeCount;
	void *mapped;
	vkMapMemory(r->core.device, ctx->node_mem, 0, sizeof(BCGLNodeData) * n, 0, &mapped);
	BCGLNodeData *gpu_nodes = (BCGLNodeData *)mapped;

	float total_velocity = 0.0f;
	float max_velocity = 0.0f;
	float min_x = 999999.0f, max_x = -999999.0f;
	float min_y = 999999.0f, max_y = -999999.0f;

	for (igraph_integer_t i = 0; i < n; i++) {
		float vx = gpu_nodes[i].velocity[0];
		float vy = gpu_nodes[i].velocity[1];
		float vz = gpu_nodes[i].velocity[2];
		float speed = sqrtf(vx * vx + vy * vy + vz * vz);

		total_velocity += speed;
		if (speed > max_velocity)
			max_velocity = speed;

		if (gpu_nodes[i].pos[0] < min_x)
			min_x = gpu_nodes[i].pos[0];
		if (gpu_nodes[i].pos[0] > max_x)
			max_x = gpu_nodes[i].pos[0];
		if (gpu_nodes[i].pos[1] < min_y)
			min_y = gpu_nodes[i].pos[1];
		if (gpu_nodes[i].pos[1] > max_y)
			max_y = gpu_nodes[i].pos[1];
	}

	vkUnmapMemory(r->core.device, ctx->node_mem);

	float avg_velocity = total_velocity / (float)n;
	float spread_x = max_x - min_x;
	float spread_y = max_y - min_y;

	printf("[BCGL Progress] Iter %3u/%3u | Avg Vel: %6.3f | Max Vel: %6.3f | Spread: %.1f x %.1f\n", current_iter, total_iters, avg_velocity, max_velocity, spread_x, spread_y);
}

// ============================================================================
// Apply: Push seed into graph, init GPU buffers, run optimization, readback
// ============================================================================
void apply_layout_bcgl(ExecutionContext *ctx, void *result_data)
{
	if (!ctx || !ctx->app_state || !ctx->current_graph || !result_data)
		return;

	AppState *state = ctx->app_state;
	GraphData *data = &state->current_graph;
	Renderer *renderer = &state->renderer;
	igraph_matrix_t *layout = (igraph_matrix_t *)result_data;

	// Write the seed positions into graph data
	igraph_matrix_destroy(&data->current_layout);
	igraph_matrix_init_copy(&data->current_layout, layout);
	igraph_layout_align(&data->g, &data->current_layout);

	if (data->nodes) {
		for (igraph_integer_t i = 0; i < data->node_count; i++) {
			data->nodes[i].position[0] = (float)MATRIX(data->current_layout, i, 0);
			data->nodes[i].position[1] = (float)MATRIX(data->current_layout, i, 1);
			data->nodes[i].position[2] = (igraph_matrix_ncol(&data->current_layout) > 2) ? (float)MATRIX(data->current_layout, i, 2) : 0.0f;
		}
	}

	// Init BCGL GPU buffers and run the optimization
	renderer_init_bcgl_buffers(renderer, data);

	uint32_t total_iterations = 500;
	//if (data->node_count > 50000)
	//	total_iterations = 20;
	//else if (data->node_count > 10000)
	//	total_iterations = 50;

	// Break the execution into 10 chunks for debugging
	uint32_t chunks = 10;
	uint32_t iter_per_chunk = total_iterations / chunks;

	printf("--- Starting BCGL GPU Optimization ---\n");
	for (uint32_t i = 0; i < chunks; i++) {
		renderer_dispatch_bcgl_layout(renderer, data, iter_per_chunk);

		uint32_t current_iter = (i + 1) * iter_per_chunk;
		debug_print_bcgl_stats(renderer, current_iter, total_iterations);
	}
	printf("--- Optimization Complete ---\n");

	renderer_readback_bcgl_positions(renderer, data);

	// Sync positions to the layout matrix so standard apply path works
	igraph_matrix_destroy(layout);
	igraph_matrix_init(layout, data->node_count, 3);
	for (igraph_integer_t i = 0; i < data->node_count; i++) {
		MATRIX(*layout, i, 0) = (igraph_real_t)data->nodes[i].position[0];
		MATRIX(*layout, i, 1) = (igraph_real_t)data->nodes[i].position[1];
		MATRIX(*layout, i, 2) = (igraph_real_t)data->nodes[i].position[2];
	}

	renderer_update_graph(renderer, data);
	renderer->labelTreeNeedsRebuild = true;

	printf("BCGL layout applied (%u vertices, %u edges)\n", data->node_count, data->edge_count);
}

// ============================================================================
// Free: Reuse the standard layout matrix free
// ============================================================================
void free_layout_bcgl(void *result_data)
{
	free_layout_matrix(result_data);
}
