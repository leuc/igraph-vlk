#define _GNU_SOURCE

#include "app_state.h"
#include "graph/wrappers_layout.h"
#include "vulkan/rt_layout.h"
#include <igraph.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct
{
	igraph_matrix_t *positions;
	igraph_int_t maxiter;
} YHRTData;

// ============================================================================
// YIFAN HU RT LAYOUT (3D) - Worker
// ============================================================================
void *compute_igraph_layout_yifan_hu_rt_3d(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = IGRAPH_MALLOC(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 3) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		return NULL;
	}

	igraph_rng_seed(igraph_rng_default(), 42);
	for (igraph_integer_t i = 0; i < vcount; i++) {
		MATRIX(*result, i, 0) = RNG_UNIF(-1.0, 1.0);
		MATRIX(*result, i, 1) = RNG_UNIF(-1.0, 1.0);
		MATRIX(*result, i, 2) = RNG_UNIF(-1.0, 1.0);
	}

	YHRTData *data = IGRAPH_MALLOC(sizeof(YHRTData));
	data->positions = result;
	data->maxiter = 500;
	return data;
}

// ============================================================================
// YIFAN HU RT LAYOUT - Apply
// ============================================================================
void apply_yhrt_layout(ExecutionContext *ctx, void *result_data)
{
	if (!ctx || !ctx->app_state || !ctx->current_graph || !result_data) {
		fprintf(stderr, "[apply_yhrt_layout] Error: Invalid parameters\n");
		return;
	}

	YHRTData *data = (YHRTData *)result_data;
	Renderer *renderer = &ctx->app_state->renderer;

	if (!renderer->yhrt_supported) {
		fprintf(stderr, "[apply_yhrt_layout] Error: Ray tracing not supported on this GPU\n");
		return;
	}

	yhrt_start(renderer, ctx->current_graph, data->positions, data->maxiter);
}

// ============================================================================
// YIFAN HU RT LAYOUT - Free
// ============================================================================
void free_yhrt_data(void *result_data)
{
	if (result_data) {
		YHRTData *data = (YHRTData *)result_data;
		if (data->positions) {
			igraph_matrix_destroy(data->positions);
			IGRAPH_FREE(data->positions);
		}
		IGRAPH_FREE(data);
	}
}
