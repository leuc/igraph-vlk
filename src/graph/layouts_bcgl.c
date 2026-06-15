#include "graph/wrappers_layout.h"

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
	renderer_dispatch_bcgl_layout(renderer, data, 200);
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
