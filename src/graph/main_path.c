/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "graph/main_path.h"

#include "app_state.h"
#include <stdio.h>
#include <stdlib.h>

#include "graph/graph_animation.h"
#include "graph/graph_color.h"
#include "graph/graph_core.h"
#include "graph/main_path_cache.h"
#include "vulkan/renderer.h"
#include "vulkan/renderer_anim.h"
#include "vulkan/renderer_criticality.h"

typedef struct
{
	igraph_vector_int_t levels;
	int num_levels;
	uint32_t node_count;
	uint32_t edge_count;
	uint32_t weight_mode;
	const char *method;
} MainPathPrep;

static bool main_path_calculate_levels(const igraph_t *graph, igraph_vector_int_t *levels)
{
	igraph_integer_t n = igraph_vcount(graph);
	if (n == 0 || !igraph_is_directed(graph))
		return false;
	igraph_bool_t is_dag = false;
	if (igraph_is_dag(graph, &is_dag) != IGRAPH_SUCCESS || !is_dag)
		return false;
	igraph_vector_int_t order;
	if (igraph_vector_int_init(&order, 0) != IGRAPH_SUCCESS)
		return false;
	if (igraph_topological_sorting(graph, &order, IGRAPH_OUT) != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(&order);
		return false;
	}
	if (igraph_vector_int_init(levels, n) != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(&order);
		return false;
	}
	igraph_vector_int_null(levels);
	for (igraph_integer_t i = 0; i < igraph_vector_int_size(&order); i++) {
		igraph_integer_t u = VECTOR(order)[i];
		igraph_vector_int_t neighbours;
		if (igraph_vector_int_init(&neighbours, 0) != IGRAPH_SUCCESS) {
			igraph_vector_int_destroy(levels);
			igraph_vector_int_destroy(&order);
			return false;
		}
		if (igraph_neighbors(graph, &neighbours, u, IGRAPH_OUT, IGRAPH_LOOPS, IGRAPH_NO_MULTIPLE) != IGRAPH_SUCCESS) {
			igraph_vector_int_destroy(&neighbours);
			igraph_vector_int_destroy(levels);
			igraph_vector_int_destroy(&order);
			return false;
		}
		for (igraph_integer_t j = 0; j < igraph_vector_int_size(&neighbours); j++) {
			igraph_integer_t v = VECTOR(neighbours)[j];
			if (VECTOR(*levels)[v] < VECTOR(*levels)[u] + 1)
				VECTOR(*levels)[v] = VECTOR(*levels)[u] + 1;
		}
		igraph_vector_int_destroy(&neighbours);
	}
	igraph_vector_int_destroy(&order);
	return true;
}

static void *main_path_prepare_weighting(ExecutionContext *ctx, uint32_t weight_mode, const char *method)
{
	if (!ctx || !ctx->app_state || !ctx->app_state->current_graph.graph_initialized)
		return NULL;
	GraphData *graph = &ctx->app_state->current_graph;
	if (igraph_vcount(&graph->g) == 0 || !igraph_is_directed(&graph->g)) {
		fprintf(stderr, "[Main Path] weighting requires a non-empty directed DAG\n");
		return NULL;
	}
	igraph_bool_t is_dag = false;
	if (igraph_is_dag(&graph->g, &is_dag) != IGRAPH_SUCCESS || !is_dag) {
		fprintf(stderr, "[Main Path] graph contains cycles; use the explicit cycle-cleanup command first\n");
		return NULL;
	}
	MainPathPrep *prep = calloc(1, sizeof(*prep));
	if (!prep || !main_path_calculate_levels(&graph->g, &prep->levels)) {
		free(prep);
		return NULL;
	}
	for (igraph_integer_t i = 0; i < igraph_vector_int_size(&prep->levels); i++)
		if (VECTOR(prep->levels)[i] + 1 > prep->num_levels)
			prep->num_levels = VECTOR(prep->levels)[i] + 1;
	prep->node_count = graph->node_count;
	prep->edge_count = graph->edge_count;
	prep->weight_mode = weight_mode;
	prep->method = method;
	return prep;
}

void *compute_main_path_splc(ExecutionContext *ctx)
{
	return main_path_prepare_weighting(ctx, CRIT_WEIGHT_SPLC, "splc");
}

void *compute_main_path_spc(ExecutionContext *ctx)
{
	return main_path_prepare_weighting(ctx, CRIT_WEIGHT_SPC, "spc");
}

void *compute_main_path_unit(ExecutionContext *ctx)
{
	return main_path_prepare_weighting(ctx, CRIT_WEIGHT_UNIT, "unit");
}

void *compute_main_path_spe(ExecutionContext *ctx)
{
	return main_path_prepare_weighting(ctx, CRIT_WEIGHT_SPE, "spe");
}

void *compute_main_path_nppc(ExecutionContext *ctx)
{
	return main_path_prepare_weighting(ctx, CRIT_WEIGHT_NPPC, "nppc");
}

void free_main_path_prep(void *result_data)
{
	MainPathPrep *prep = result_data;
	if (!prep)
		return;
	igraph_vector_int_destroy(&prep->levels);
	free(prep);
}

void apply_main_path_weighting(ExecutionContext *ctx, void *result_data)
{
	MainPathPrep *prep = result_data;
	if (!ctx || !ctx->app_state || !prep)
		return;
	AppState *state = ctx->app_state;
	GraphData *graph = &state->current_graph;
	if (prep->node_count != graph->node_count || prep->edge_count != graph->edge_count)
		return;
	main_path_cache_remove_method(&graph->g, prep->method);
	graph_reset_emphasis(graph);
	graph_animation_clear(&state->renderer, graph);
	state->renderer.needsAttributeUpload = VK_TRUE;
	if (!graph_rebuild_edges(graph))
		return;
	renderer_update_graph(&state->renderer, graph);
	if (!renderer_init_criticality_buffers(&state->renderer, graph, &prep->levels, prep->num_levels, prep->weight_mode) || !renderer_start_main_path_weighting(&state->renderer, graph, &prep->levels))
		fprintf(stderr, "[Main Path] failed to initialize weighting\n");
	state->renderer.label.tree_needs_rebuild = true;
}

bool poll_main_path_weighting(ExecutionContext *ctx)
{
	if (!ctx || !ctx->app_state)
		return true;
	AppState *state = ctx->app_state;
	CritComputeContext *crit = &state->renderer.crit;
	if (!crit->active && !crit->readback_pending)
		return true;

	// GPU pipeline, in order: [NPPC reachability tiles] -> [forward/reverse level reveal] ->
	// [postprocess + readback]. Only NPPC has the batch-tile phase; the reveal phase is one sweep
	// (SPLC/UNIT) or two (SPC/SPE/NPPC, which also run a reverse sweep) — mirrors poll_bcgl_gpu's
	// pattern of writing state->job_progress/job_status_message directly from GPU-side counters,
	// since worker_thread_set_progress is only reachable while the CPU-side worker_func runs.
	const float postprocess_weight = 0.05f;
	const float batch_weight = crit->weight_mode == CRIT_WEIGHT_NPPC ? 0.35f : 0.0f;
	const float reveal_weight = 1.0f - batch_weight - postprocess_weight;
	bool has_reverse_sweep = crit->weight_mode == CRIT_WEIGHT_SPC || crit->weight_mode == CRIT_WEIGHT_SPE || crit->weight_mode == CRIT_WEIGHT_NPPC;
	int reveal_total_steps = crit->num_levels > 0 ? (has_reverse_sweep ? crit->num_levels * 2 : crit->num_levels) : 1;

	if (crit->readback_pending) {
		state->job_progress = 1.0f;
		snprintf(state->job_status_message, sizeof(state->job_status_message), "main path finishing");
	} else if (crit->nppc_batch_pending) {
		uint32_t tile_count = crit->nppc_batch_tile_count > 0 ? crit->nppc_batch_tile_count : 1;
		state->job_progress = batch_weight * (float)crit->nppc_batch_tile / (float)tile_count;
		snprintf(state->job_status_message, sizeof(state->job_status_message), "NPPC reachability tile %u/%u", crit->nppc_batch_tile + 1, crit->nppc_batch_tile_count);
	} else {
		int reveal_steps_done = crit->stage == CRIT_STAGE_LNW ? crit->current_level : crit->num_levels + (crit->num_levels - 1 - crit->current_level);
		state->job_progress = batch_weight + reveal_weight * (float)reveal_steps_done / (float)reveal_total_steps;
		snprintf(state->job_status_message, sizeof(state->job_status_message), "main path %s level %d/%d", crit->stage == CRIT_STAGE_LNW ? "forward" : "reverse", crit->current_level, crit->num_levels - 1);
	}
	return false;
}

static void *main_path_load_selection(ExecutionContext *ctx, const char *method, const char *selection)
{
	if (!ctx || !ctx->app_state || !ctx->app_state->current_graph.graph_initialized)
		return NULL;
	GraphData *graph = &ctx->app_state->current_graph;
	return main_path_cache_load_selection(&graph->g, method, selection, graph->node_count, graph->edge_count);
}

void *compute_main_path_splc_basket(ExecutionContext *ctx)
{
	return main_path_load_selection(ctx, "splc", "basket");
}

void *compute_main_path_spc_basket(ExecutionContext *ctx)
{
	return main_path_load_selection(ctx, "spc", "basket");
}

void *compute_main_path_spe_basket(ExecutionContext *ctx)
{
	return main_path_load_selection(ctx, "spe", "basket");
}

void *compute_main_path_unit_basket(ExecutionContext *ctx)
{
	return main_path_load_selection(ctx, "unit", "basket");
}

void *compute_main_path_nppc_basket(ExecutionContext *ctx)
{
	return main_path_load_selection(ctx, "nppc", "basket");
}

void *compute_main_path_splc_path(ExecutionContext *ctx)
{
	return main_path_load_selection(ctx, "splc", "path");
}

void *compute_main_path_spc_path(ExecutionContext *ctx)
{
	return main_path_load_selection(ctx, "spc", "path");
}

void *compute_main_path_spe_path(ExecutionContext *ctx)
{
	return main_path_load_selection(ctx, "spe", "path");
}

void *compute_main_path_unit_path(ExecutionContext *ctx)
{
	return main_path_load_selection(ctx, "unit", "path");
}

void *compute_main_path_nppc_path(ExecutionContext *ctx)
{
	return main_path_load_selection(ctx, "nppc", "path");
}

void apply_main_path_selection(ExecutionContext *ctx, void *result_data)
{
	MainPathSelectionResult *result = result_data;
	if (!ctx || !ctx->app_state || !result)
		return;
	GraphData *graph = &ctx->app_state->current_graph;
	Renderer *renderer = &ctx->app_state->renderer;
	if (graph->node_count != result->node_count || graph->edge_count != result->edge_count)
		return;
	RendererAnimNode *nodes = calloc(result->node_count > 0 ? result->node_count : 1, sizeof(RendererAnimNode));
	RendererAnimEdge *edges = calloc(result->edge_count > 0 ? result->edge_count : 1, sizeof(RendererAnimEdge));
	if (!nodes || !edges) {
		free(nodes);
		free(edges);
		return;
	}
	float maximum = 0.0f;
	for (uint32_t e = 0; e < result->edge_count; e++) {
		edges[e].strength = result->strengths[e];
		if (edges[e].strength > maximum)
			maximum = edges[e].strength;
	}
	RendererAnimClip clip = {
		.nodes = nodes,
		.edges = edges,
		.node_count = result->node_count,
		.edge_count = result->edge_count,
		.strength_max = maximum,
		.fade = 0.3f,
		.reveal_mask = 0,
		.owner = RENDERER_ANIM_HOST,
	};
	bool played = renderer_anim_play(renderer, &clip);
	free(nodes);
	free(edges);
	if (!played)
		return;
	graph_reset_emphasis(graph);
	for (uint32_t v = 0; v < graph->node_count; v++)
		if (!result->flags[v])
			graph->nodes[v].emphasis = EMPHASIS_DIMMED;
	renderer->needsAttributeUpload = VK_TRUE;
	renderer_update_graph(renderer, graph);
}

void free_main_path_selection(void *result_data)
{
	main_path_cache_selection_free(result_data);
}
