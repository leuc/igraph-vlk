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
#include "graph/main_path_search.h"
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

void *compute_main_path_splc_global(ExecutionContext *ctx)
{
	return main_path_load_selection(ctx, "splc", "global");
}

void *compute_main_path_spc_global(ExecutionContext *ctx)
{
	return main_path_load_selection(ctx, "spc", "global");
}

void *compute_main_path_spe_global(ExecutionContext *ctx)
{
	return main_path_load_selection(ctx, "spe", "global");
}

void *compute_main_path_unit_global(ExecutionContext *ctx)
{
	return main_path_load_selection(ctx, "unit", "global");
}

void *compute_main_path_nppc_global(ExecutionContext *ctx)
{
	return main_path_load_selection(ctx, "nppc", "global");
}

// Loaded inputs shared by every Local/Backward-Local/Multiple/Key-Route/Network selection below
// (Liu & Lu 2012 variants not covered by the GPU-computed Global/Basket selections above): the
// graph plus its already-cached main-path-weight-{method}/main-path-strength-{method} edges.
// Unlike Basket/Global Path, these selections are recomputed fresh on every call rather than
// cached as attributes (see main_path_search.h).
typedef struct
{
	igraph_vector_t weights;
	igraph_vector_t strengths;
	igraph_t *graph;
	uint32_t node_count;
	uint32_t edge_count;
} MainPathSearchInputs;

static bool main_path_search_load_inputs(ExecutionContext *ctx, const char *method, MainPathSearchInputs *inputs)
{
	if (!ctx || !ctx->app_state || !ctx->app_state->current_graph.graph_initialized)
		return false;
	GraphData *graph = &ctx->app_state->current_graph;
	if (igraph_vector_init(&inputs->weights, 0) != IGRAPH_SUCCESS)
		return false;
	if (igraph_vector_init(&inputs->strengths, 0) != IGRAPH_SUCCESS) {
		igraph_vector_destroy(&inputs->weights);
		return false;
	}
	if (!main_path_cache_load_weight_and_strength(&graph->g, method, graph->edge_count, &inputs->weights, &inputs->strengths)) {
		igraph_vector_destroy(&inputs->strengths);
		igraph_vector_destroy(&inputs->weights);
		return false;
	}
	inputs->graph = &graph->g;
	inputs->node_count = graph->node_count;
	inputs->edge_count = graph->edge_count;
	return true;
}

static void main_path_search_free_inputs(MainPathSearchInputs *inputs)
{
	igraph_vector_destroy(&inputs->strengths);
	igraph_vector_destroy(&inputs->weights);
}

// Tags a freshly computed MainPathSelectionResult with the method/selection identity that
// produced it, so apply_main_path_selection can persist main-path-{selection}-{method}
// consistently for every selection (not just the GPU-cached Basket/Global Path, which are
// already tagged inside main_path_cache_load_selection).
static void *main_path_search_tag(void *result_data, const char *method, const char *selection)
{
	MainPathSelectionResult *result = result_data;
	if (result) {
		result->method = method;
		result->selection = selection;
	}
	return result_data;
}

// Local main path

void *compute_main_path_splc_local(ExecutionContext *ctx)
{
	MainPathSearchInputs in;
	if (!main_path_search_load_inputs(ctx, "splc", &in))
		return NULL;
	void *result = main_path_search_local(in.graph, &in.weights, &in.strengths, in.node_count, in.edge_count);
	main_path_search_free_inputs(&in);
	return main_path_search_tag(result, "splc", "local");
}

void *compute_main_path_spc_local(ExecutionContext *ctx)
{
	MainPathSearchInputs in;
	if (!main_path_search_load_inputs(ctx, "spc", &in))
		return NULL;
	void *result = main_path_search_local(in.graph, &in.weights, &in.strengths, in.node_count, in.edge_count);
	main_path_search_free_inputs(&in);
	return main_path_search_tag(result, "spc", "local");
}

void *compute_main_path_spe_local(ExecutionContext *ctx)
{
	MainPathSearchInputs in;
	if (!main_path_search_load_inputs(ctx, "spe", &in))
		return NULL;
	void *result = main_path_search_local(in.graph, &in.weights, &in.strengths, in.node_count, in.edge_count);
	main_path_search_free_inputs(&in);
	return main_path_search_tag(result, "spe", "local");
}

void *compute_main_path_unit_local(ExecutionContext *ctx)
{
	MainPathSearchInputs in;
	if (!main_path_search_load_inputs(ctx, "unit", &in))
		return NULL;
	void *result = main_path_search_local(in.graph, &in.weights, &in.strengths, in.node_count, in.edge_count);
	main_path_search_free_inputs(&in);
	return main_path_search_tag(result, "unit", "local");
}

void *compute_main_path_nppc_local(ExecutionContext *ctx)
{
	MainPathSearchInputs in;
	if (!main_path_search_load_inputs(ctx, "nppc", &in))
		return NULL;
	void *result = main_path_search_local(in.graph, &in.weights, &in.strengths, in.node_count, in.edge_count);
	main_path_search_free_inputs(&in);
	return main_path_search_tag(result, "nppc", "local");
}

// Backward local main path

void *compute_main_path_splc_backward_local(ExecutionContext *ctx)
{
	MainPathSearchInputs in;
	if (!main_path_search_load_inputs(ctx, "splc", &in))
		return NULL;
	void *result = main_path_search_backward_local(in.graph, &in.weights, &in.strengths, in.node_count, in.edge_count);
	main_path_search_free_inputs(&in);
	return main_path_search_tag(result, "splc", "backward_local");
}

void *compute_main_path_spc_backward_local(ExecutionContext *ctx)
{
	MainPathSearchInputs in;
	if (!main_path_search_load_inputs(ctx, "spc", &in))
		return NULL;
	void *result = main_path_search_backward_local(in.graph, &in.weights, &in.strengths, in.node_count, in.edge_count);
	main_path_search_free_inputs(&in);
	return main_path_search_tag(result, "spc", "backward_local");
}

void *compute_main_path_spe_backward_local(ExecutionContext *ctx)
{
	MainPathSearchInputs in;
	if (!main_path_search_load_inputs(ctx, "spe", &in))
		return NULL;
	void *result = main_path_search_backward_local(in.graph, &in.weights, &in.strengths, in.node_count, in.edge_count);
	main_path_search_free_inputs(&in);
	return main_path_search_tag(result, "spe", "backward_local");
}

void *compute_main_path_unit_backward_local(ExecutionContext *ctx)
{
	MainPathSearchInputs in;
	if (!main_path_search_load_inputs(ctx, "unit", &in))
		return NULL;
	void *result = main_path_search_backward_local(in.graph, &in.weights, &in.strengths, in.node_count, in.edge_count);
	main_path_search_free_inputs(&in);
	return main_path_search_tag(result, "unit", "backward_local");
}

void *compute_main_path_nppc_backward_local(ExecutionContext *ctx)
{
	MainPathSearchInputs in;
	if (!main_path_search_load_inputs(ctx, "nppc", &in))
		return NULL;
	void *result = main_path_search_backward_local(in.graph, &in.weights, &in.strengths, in.node_count, in.edge_count);
	main_path_search_free_inputs(&in);
	return main_path_search_tag(result, "nppc", "backward_local");
}

// Multiple main paths (20% tolerance -- Liu & Lu 2012's own case-study value, p.535)

#define MAIN_PATH_MULTIPLE_TOLERANCE_PCT 20.0

void *compute_main_path_splc_multiple(ExecutionContext *ctx)
{
	MainPathSearchInputs in;
	if (!main_path_search_load_inputs(ctx, "splc", &in))
		return NULL;
	void *result = main_path_search_multiple(in.graph, &in.weights, &in.strengths, in.node_count, in.edge_count, MAIN_PATH_MULTIPLE_TOLERANCE_PCT);
	main_path_search_free_inputs(&in);
	return main_path_search_tag(result, "splc", "multiple");
}

void *compute_main_path_spc_multiple(ExecutionContext *ctx)
{
	MainPathSearchInputs in;
	if (!main_path_search_load_inputs(ctx, "spc", &in))
		return NULL;
	void *result = main_path_search_multiple(in.graph, &in.weights, &in.strengths, in.node_count, in.edge_count, MAIN_PATH_MULTIPLE_TOLERANCE_PCT);
	main_path_search_free_inputs(&in);
	return main_path_search_tag(result, "spc", "multiple");
}

void *compute_main_path_spe_multiple(ExecutionContext *ctx)
{
	MainPathSearchInputs in;
	if (!main_path_search_load_inputs(ctx, "spe", &in))
		return NULL;
	void *result = main_path_search_multiple(in.graph, &in.weights, &in.strengths, in.node_count, in.edge_count, MAIN_PATH_MULTIPLE_TOLERANCE_PCT);
	main_path_search_free_inputs(&in);
	return main_path_search_tag(result, "spe", "multiple");
}

void *compute_main_path_unit_multiple(ExecutionContext *ctx)
{
	MainPathSearchInputs in;
	if (!main_path_search_load_inputs(ctx, "unit", &in))
		return NULL;
	void *result = main_path_search_multiple(in.graph, &in.weights, &in.strengths, in.node_count, in.edge_count, MAIN_PATH_MULTIPLE_TOLERANCE_PCT);
	main_path_search_free_inputs(&in);
	return main_path_search_tag(result, "unit", "multiple");
}

void *compute_main_path_nppc_multiple(ExecutionContext *ctx)
{
	MainPathSearchInputs in;
	if (!main_path_search_load_inputs(ctx, "nppc", &in))
		return NULL;
	void *result = main_path_search_multiple(in.graph, &in.weights, &in.strengths, in.node_count, in.edge_count, MAIN_PATH_MULTIPLE_TOLERANCE_PCT);
	main_path_search_free_inputs(&in);
	return main_path_search_tag(result, "nppc", "multiple");
}

// Key-route main path (K=10 -- matches Jiang & Liu 2023's applied use of key-route search,
// https://doi.org/10.1002/asi.24748 S4.2.2)

#define MAIN_PATH_KEY_ROUTE_SEEDS 10

void *compute_main_path_splc_key_route(ExecutionContext *ctx)
{
	MainPathSearchInputs in;
	if (!main_path_search_load_inputs(ctx, "splc", &in))
		return NULL;
	void *result = main_path_search_key_route(in.graph, &in.weights, &in.strengths, in.node_count, in.edge_count, MAIN_PATH_KEY_ROUTE_SEEDS);
	main_path_search_free_inputs(&in);
	return main_path_search_tag(result, "splc", "key_route");
}

void *compute_main_path_spc_key_route(ExecutionContext *ctx)
{
	MainPathSearchInputs in;
	if (!main_path_search_load_inputs(ctx, "spc", &in))
		return NULL;
	void *result = main_path_search_key_route(in.graph, &in.weights, &in.strengths, in.node_count, in.edge_count, MAIN_PATH_KEY_ROUTE_SEEDS);
	main_path_search_free_inputs(&in);
	return main_path_search_tag(result, "spc", "key_route");
}

void *compute_main_path_spe_key_route(ExecutionContext *ctx)
{
	MainPathSearchInputs in;
	if (!main_path_search_load_inputs(ctx, "spe", &in))
		return NULL;
	void *result = main_path_search_key_route(in.graph, &in.weights, &in.strengths, in.node_count, in.edge_count, MAIN_PATH_KEY_ROUTE_SEEDS);
	main_path_search_free_inputs(&in);
	return main_path_search_tag(result, "spe", "key_route");
}

void *compute_main_path_unit_key_route(ExecutionContext *ctx)
{
	MainPathSearchInputs in;
	if (!main_path_search_load_inputs(ctx, "unit", &in))
		return NULL;
	void *result = main_path_search_key_route(in.graph, &in.weights, &in.strengths, in.node_count, in.edge_count, MAIN_PATH_KEY_ROUTE_SEEDS);
	main_path_search_free_inputs(&in);
	return main_path_search_tag(result, "unit", "key_route");
}

void *compute_main_path_nppc_key_route(ExecutionContext *ctx)
{
	MainPathSearchInputs in;
	if (!main_path_search_load_inputs(ctx, "nppc", &in))
		return NULL;
	void *result = main_path_search_key_route(in.graph, &in.weights, &in.strengths, in.node_count, in.edge_count, MAIN_PATH_KEY_ROUTE_SEEDS);
	main_path_search_free_inputs(&in);
	return main_path_search_tag(result, "nppc", "key_route");
}

// Network of main paths

void *compute_main_path_splc_network(ExecutionContext *ctx)
{
	MainPathSearchInputs in;
	if (!main_path_search_load_inputs(ctx, "splc", &in))
		return NULL;
	void *result = main_path_search_network(in.graph, &in.weights, &in.strengths, in.node_count, in.edge_count);
	main_path_search_free_inputs(&in);
	return main_path_search_tag(result, "splc", "network");
}

void *compute_main_path_spc_network(ExecutionContext *ctx)
{
	MainPathSearchInputs in;
	if (!main_path_search_load_inputs(ctx, "spc", &in))
		return NULL;
	void *result = main_path_search_network(in.graph, &in.weights, &in.strengths, in.node_count, in.edge_count);
	main_path_search_free_inputs(&in);
	return main_path_search_tag(result, "spc", "network");
}

void *compute_main_path_spe_network(ExecutionContext *ctx)
{
	MainPathSearchInputs in;
	if (!main_path_search_load_inputs(ctx, "spe", &in))
		return NULL;
	void *result = main_path_search_network(in.graph, &in.weights, &in.strengths, in.node_count, in.edge_count);
	main_path_search_free_inputs(&in);
	return main_path_search_tag(result, "spe", "network");
}

void *compute_main_path_unit_network(ExecutionContext *ctx)
{
	MainPathSearchInputs in;
	if (!main_path_search_load_inputs(ctx, "unit", &in))
		return NULL;
	void *result = main_path_search_network(in.graph, &in.weights, &in.strengths, in.node_count, in.edge_count);
	main_path_search_free_inputs(&in);
	return main_path_search_tag(result, "unit", "network");
}

void *compute_main_path_nppc_network(ExecutionContext *ctx)
{
	MainPathSearchInputs in;
	if (!main_path_search_load_inputs(ctx, "nppc", &in))
		return NULL;
	void *result = main_path_search_network(in.graph, &in.weights, &in.strengths, in.node_count, in.edge_count);
	main_path_search_free_inputs(&in);
	return main_path_search_tag(result, "nppc", "network");
}

// Valued network (Hummon & Carley 1993, https://doi.org/10.1016/0378-8733(93)90022-D, p.93 --
// their own case study used >=25 of an observed max of 80, i.e. ~31%, an explicitly arbitrary
// cutoff, not a rule; relative to the observed max, not to node_count -- see
// main_path_search_valued_network's doc comment for why)

#define MAIN_PATH_VALUED_NETWORK_THRESHOLD_FRACTION 0.3

void *compute_main_path_splc_valued_network(ExecutionContext *ctx)
{
	MainPathSearchInputs in;
	if (!main_path_search_load_inputs(ctx, "splc", &in))
		return NULL;
	MainPathSelectionResult *result = main_path_search_valued_network(in.graph, &in.weights, in.node_count, in.edge_count, MAIN_PATH_VALUED_NETWORK_THRESHOLD_FRACTION);
	main_path_search_free_inputs(&in);
	if (result) {
		uint32_t flagged = 0;
		float max_tie_frequency = 0.0f;
		for (uint32_t v = 0; v < result->node_count; v++)
			if (result->flags[v])
				flagged++;
		for (uint32_t e = 0; e < result->edge_count; e++)
			if (result->strengths[e] > max_tie_frequency)
				max_tie_frequency = result->strengths[e];
		fprintf(stderr, "[Main Path] valued network (splc): %u/%u nodes flagged, max tie frequency %.0f, threshold %.1f (%.0f%% of max) over %u sampled paths\n", flagged, result->node_count, max_tie_frequency, MAIN_PATH_VALUED_NETWORK_THRESHOLD_FRACTION * max_tie_frequency, MAIN_PATH_VALUED_NETWORK_THRESHOLD_FRACTION * 100.0, result->node_count);
	}
	return main_path_search_tag(result, "splc", "valued_network");
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
	// Persist main-path-{selection}-{method} for every selection, not just the GPU-cached
	// Basket/Global Path -- runs on the main thread (apply_ functions, unlike compute_ functions,
	// never run on the worker thread), so mutating graph->g's cattributes here is safe; doing
	// this from a compute_ function would race the main thread the way main_path_prepare()'s
	// worker-thread edge deletion already does (see TODO.md).
	if (result->method && result->selection) {
		igraph_vector_int_t flags;
		if (igraph_vector_int_init(&flags, result->node_count) == IGRAPH_SUCCESS) {
			for (uint32_t v = 0; v < result->node_count; v++)
				VECTOR(flags)[v] = result->flags[v];
			char attr_name[64];
			snprintf(attr_name, sizeof(attr_name), "main-path-%s-%s", result->selection, result->method);
			graph_cache_store_vertex_attr_int(&graph->g, attr_name, &flags);
			igraph_vector_int_destroy(&flags);
		}
	}
}

void free_main_path_selection(void *result_data)
{
	main_path_cache_selection_free(result_data);
}
