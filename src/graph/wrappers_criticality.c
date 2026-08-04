/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "graph/wrappers_criticality.h"

#include "app_state.h"
#include "graph/graph_animation.h"
#include "graph/graph_color.h"
#include "graph/graph_core.h"
#include "graph/worker_thread.h"
#include "graph/wrappers_splc.h"
#include "vulkan/renderer.h"
#include "vulkan/renderer_criticality.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct
{
	igraph_vector_int_t levels;
	int num_levels;
	uint32_t weight_mode;
	igraph_integer_t node_count;
} CritPrep;

static void *crit_prepare(ExecutionContext *ctx, uint32_t weight_mode)
{
	const char *label = weight_mode == CRIT_WEIGHT_UNIT ? "Unit" : (weight_mode == CRIT_WEIGHT_SPE ? "SPE" : "SPC");
	worker_thread_set_status_message("Main path: preparing DAG...");
	MainPathPrep *dag = main_path_prepare(ctx);
	if (!dag)
		return NULL;
	if (!ctx->running) {
		free_main_path_prep(dag);
		return NULL;
	}

	CritPrep *prep = calloc(1, sizeof(*prep));
	if (!prep || igraph_vector_int_init_copy(&prep->levels, &dag->levels) != IGRAPH_SUCCESS) {
		free(prep);
		free_main_path_prep(dag);
		return NULL;
	}
	prep->num_levels = dag->num_levels;
	prep->weight_mode = weight_mode;
	prep->node_count = dag->node_count;
	free_main_path_prep(dag);

	char msg[128];
	snprintf(msg, sizeof(msg), "Main path %s: %d levels ready", label, prep->num_levels);
	worker_thread_set_status_message(msg);
	worker_thread_set_progress(0.6f);
	return prep;
}

void *compute_main_path_spc(ExecutionContext *ctx)
{
	return crit_prepare(ctx, CRIT_WEIGHT_SPC);
}

void *compute_main_path_unit(ExecutionContext *ctx)
{
	return crit_prepare(ctx, CRIT_WEIGHT_UNIT);
}

void *compute_main_path_spe(ExecutionContext *ctx)
{
	return crit_prepare(ctx, CRIT_WEIGHT_SPE);
}

void apply_main_path_weighting(ExecutionContext *ctx, void *result_data)
{
	if (!ctx || !ctx->app_state || !result_data)
		return;
	CritPrep *prep = result_data;
	AppState *state = ctx->app_state;
	GraphData *graph = &state->current_graph;
	if (prep->node_count != (igraph_integer_t)graph->node_count) {
		fprintf(stderr, "[MainPath] stale result; graph changed during preparation\n");
		return;
	}

	graph_reset_emphasis(graph);
	graph_animation_clear(&state->renderer);
	state->renderer.needsAttributeUpload = VK_TRUE;
	if (!graph_rebuild_edges(graph)) {
		fprintf(stderr, "[MainPath] graph_rebuild_edges failed\n");
		return;
	}
	renderer_update_graph(&state->renderer, graph);
	if (!renderer_init_criticality_buffers(&state->renderer, graph, &prep->levels, prep->num_levels, prep->weight_mode, NULL) || !renderer_start_main_path_weighting(&state->renderer)) {
		fprintf(stderr, "[MainPath] failed to initialize live %s weighting\n", prep->weight_mode == CRIT_WEIGHT_SPE ? "SPE" : "SPC");
		return;
	}
	state->renderer.label.tree_needs_rebuild = true;
}

bool poll_main_path_weighting(ExecutionContext *ctx)
{
	if (!ctx || !ctx->app_state)
		return true;
	Renderer *renderer = &ctx->app_state->renderer;
	return !renderer->crit.active && !renderer->crit.readback_pending;
}

void free_main_path_weighting_result(void *result_data)
{
	CritPrep *prep = result_data;
	if (!prep)
		return;
	igraph_vector_int_destroy(&prep->levels);
	free(prep);
}
