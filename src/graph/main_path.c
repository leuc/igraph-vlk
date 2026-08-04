/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "graph/main_path.h"

#include "app_state.h"
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "graph/graph_color.h"
#include "graph/graph_core.h"
#include "graph/wrappers_splc.h"
#include "vulkan/renderer.h"
#include "vulkan/renderer_criticality.h"

typedef enum {
	MAIN_PATH_SELECT_BASKET,
	MAIN_PATH_SELECT_PATH,
} MainPathSelection;

typedef struct
{
	const char *weight_attr;
	MainPathSelection selection;
	int *flags;
	uint32_t node_count;
} MainPathSelectionResult;

typedef struct
{
	uint32_t *edges;
	uint32_t count;
	float bottleneck;
} MainPathPath;

static const char *main_path_method_name(const char *weight_attr)
{
	if (strcmp(weight_attr, "main-path-weight-splc") == 0)
		return "splc";
	if (strcmp(weight_attr, "main-path-weight-spc") == 0)
		return "spc";
	return "spe";
}

static bool main_path_build_adjacency(const igraph_t *graph, uint32_t **out_first, uint32_t **out_next, uint32_t **out_from, uint32_t **out_to)
{
	igraph_integer_t n = igraph_vcount(graph);
	igraph_integer_t m = igraph_ecount(graph);
	uint32_t *first = malloc(sizeof(uint32_t) * (size_t)n);
	uint32_t *next = malloc(sizeof(uint32_t) * (size_t)(m > 0 ? m : 1));
	uint32_t *from = malloc(sizeof(uint32_t) * (size_t)(m > 0 ? m : 1));
	uint32_t *to = malloc(sizeof(uint32_t) * (size_t)(m > 0 ? m : 1));
	if (!first || !next || !from || !to) {
		free(first);
		free(next);
		free(from);
		free(to);
		return false;
	}
	for (igraph_integer_t v = 0; v < n; v++)
		first[v] = UINT32_MAX;
	for (igraph_integer_t e = 0; e < m; e++) {
		igraph_integer_t u, v;
		igraph_edge(graph, e, &u, &v);
		from[e] = (uint32_t)u;
		to[e] = (uint32_t)v;
		next[e] = first[u];
		first[u] = (uint32_t)e;
	}
	*out_first = first;
	*out_next = next;
	*out_from = from;
	*out_to = to;
	return true;
}

static bool main_path_topological_order(const igraph_t *graph, igraph_vector_int_t *order)
{
	if (igraph_vector_int_init(order, 0) != IGRAPH_SUCCESS)
		return false;
	if (igraph_topological_sorting(graph, order, IGRAPH_OUT) != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(order);
		return false;
	}
	return true;
}

static bool main_path_best_path(const igraph_t *graph, const igraph_vector_int_t *order, const uint32_t *first, const uint32_t *next, const uint32_t *to, const float *weights, const float *residual, MainPathPath *out)
{
	igraph_integer_t n = igraph_vcount(graph);
	float *score = malloc(sizeof(float) * (size_t)n);
	int *previous = malloc(sizeof(int) * (size_t)n);
	igraph_vector_int_t indegree;
	igraph_vector_int_t outdegree;
	out->edges = NULL;
	out->count = 0;
	out->bottleneck = 0.0f;
	if (!score || !previous) {
		free(score);
		free(previous);
		return false;
	}
	if (igraph_vector_int_init(&indegree, n) != IGRAPH_SUCCESS) {
		free(score);
		free(previous);
		return false;
	}
	if (igraph_vector_int_init(&outdegree, n) != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(&indegree);
		free(score);
		free(previous);
		return false;
	}
	igraph_degree(graph, &indegree, igraph_vss_all(), IGRAPH_IN, IGRAPH_LOOPS);
	igraph_degree(graph, &outdegree, igraph_vss_all(), IGRAPH_OUT, IGRAPH_LOOPS);
	for (igraph_integer_t v = 0; v < n; v++) {
		score[v] = VECTOR(indegree)[v] == 0 ? 0.0f : -FLT_MAX;
		previous[v] = -1;
	}
	for (igraph_integer_t i = 0; i < igraph_vector_int_size(order); i++) {
		uint32_t u = (uint32_t)VECTOR(*order)[i];
		if (score[u] == -FLT_MAX)
			continue;
		for (uint32_t e = first[u]; e != UINT32_MAX; e = next[e]) {
			float value = residual ? residual[e] : weights[e];
			if (!(value > 0.0f) || !isfinite(value))
				continue;
			uint32_t v = to[e];
			float candidate = score[u] + value;
			if (candidate > score[v] || (candidate == score[v] && (previous[v] < 0 || e < (uint32_t)previous[v]))) {
				score[v] = candidate;
				previous[v] = (int)e;
			}
		}
	}
	int sink = -1;
	for (igraph_integer_t v = 0; v < n; v++)
		if (VECTOR(outdegree)[v] == 0 && score[v] > -FLT_MAX && (sink < 0 || score[v] > score[sink] || (score[v] == score[sink] && v < sink)))
			sink = (int)v;
	igraph_vector_int_destroy(&indegree);
	igraph_vector_int_destroy(&outdegree);
	if (sink < 0 || previous[sink] < 0) {
		free(score);
		free(previous);
		return false;
	}

	out->edges = malloc(sizeof(uint32_t) * (size_t)n);
	if (!out->edges) {
		free(score);
		free(previous);
		return false;
	}
	out->count = 0;
	out->bottleneck = FLT_MAX;
	for (int v = sink; previous[v] >= 0;) {
		uint32_t e = (uint32_t)previous[v];
		out->edges[out->count++] = e;
		float value = residual ? residual[e] : weights[e];
		if (value < out->bottleneck)
			out->bottleneck = value;
		igraph_integer_t u, ignored;
		igraph_edge(graph, e, &u, &ignored);
		v = (int)u;
	}
	for (uint32_t i = 0; i < out->count / 2; i++) {
		uint32_t tmp = out->edges[i];
		out->edges[i] = out->edges[out->count - 1 - i];
		out->edges[out->count - 1 - i] = tmp;
	}
	free(score);
	free(previous);
	if (out->count == 0 || !(out->bottleneck > 0.0f)) {
		free(out->edges);
		out->edges = NULL;
		out->count = 0;
		out->bottleneck = 0.0f;
		return false;
	}
	return true;
}

static void *main_path_compute_selection(ExecutionContext *ctx, const char *weight_attr, MainPathSelection selection)
{
	if (!ctx || !ctx->app_state || !ctx->app_state->current_graph.graph_initialized)
		return NULL;
	GraphData *graph = &ctx->app_state->current_graph;
	igraph_vector_t values;
	if (igraph_vector_init(&values, 0) != IGRAPH_SUCCESS)
		return NULL;
	if (!graph_cache_load_edge_attr(&graph->g, weight_attr, &values) || igraph_vector_size(&values) != graph->edge_count) {
		fprintf(stderr, "[Main Path] Run Weighting before Selection\n");
		igraph_vector_destroy(&values);
		return NULL;
	}
	for (uint32_t e = 0; e < graph->edge_count; e++)
		if (!isfinite(VECTOR(values)[e])) {
			fprintf(stderr, "[Main Path] Selection rejects overflowing SPC weights; use SPE\n");
			igraph_vector_destroy(&values);
			return NULL;
		}

	igraph_vector_int_t order;
	if (!main_path_topological_order(&graph->g, &order)) {
		igraph_vector_destroy(&values);
		return NULL;
	}
	uint32_t *first = NULL, *next = NULL, *from = NULL, *to = NULL;
	if (!main_path_build_adjacency(&graph->g, &first, &next, &from, &to)) {
		igraph_vector_int_destroy(&order);
		igraph_vector_destroy(&values);
		return NULL;
	}
	igraph_integer_t n = igraph_vcount(&graph->g);
	float *height = calloc((size_t)n, sizeof(float));
	float *depth = calloc((size_t)n, sizeof(float));
	int *flags = calloc((size_t)n, sizeof(int));
	if (!height || !depth || !flags) {
		free(height);
		free(depth);
		free(flags);
		free(first);
		free(next);
		free(from);
		free(to);
		igraph_vector_int_destroy(&order);
		igraph_vector_destroy(&values);
		return NULL;
	}
	for (igraph_integer_t i = 0; i < igraph_vector_int_size(&order); i++) {
		uint32_t u = VECTOR(order)[i];
		for (uint32_t e = first[u]; e != UINT32_MAX; e = next[e]) {
			float candidate = height[u] + (float)VECTOR(values)[e];
			if (candidate > height[to[e]])
				height[to[e]] = candidate;
		}
	}
	for (igraph_integer_t i = igraph_vector_int_size(&order) - 1; i >= 0; i--) {
		uint32_t u = VECTOR(order)[i];
		for (uint32_t e = first[u]; e != UINT32_MAX; e = next[e]) {
			float candidate = (float)VECTOR(values)[e] + depth[to[e]];
			if (candidate > depth[u])
				depth[u] = candidate;
		}
	}
	float H = 0.0f;
	for (igraph_integer_t v = 0; v < n; v++)
		if (height[v] + depth[v] > H)
			H = height[v] + depth[v];
	if (selection == MAIN_PATH_SELECT_BASKET) {
		for (igraph_integer_t v = 0; v < n; v++)
			flags[v] = fabsf(H - height[v] - depth[v]) < 1e-4f ? 1 : 0;
	} else {
		MainPathPath path = {0};
		if (main_path_best_path(&graph->g, &order, first, next, to, (const float *)VECTOR(values), NULL, &path)) {
			for (uint32_t i = 0; i < path.count; i++) {
				flags[from[path.edges[i]]] = 1;
				flags[to[path.edges[i]]] = 1;
			}
			free(path.edges);
		}
	}
	MainPathSelectionResult *result = malloc(sizeof(MainPathSelectionResult));
	if (result) {
		result->weight_attr = weight_attr;
		result->selection = selection;
		result->flags = flags;
		result->node_count = graph->node_count;
	}
	if (!result)
		free(flags);
	free(height);
	free(depth);
	free(first);
	free(next);
	free(from);
	free(to);
	igraph_vector_int_destroy(&order);
	igraph_vector_destroy(&values);
	return result;
}

static void *main_path_prepare_basket(ExecutionContext *ctx, uint32_t weight_mode, const char *weight_attr)
{
	MainPathPrep *prep = main_path_prepare(ctx);
	if (prep) {
		prep->weight_mode = weight_mode;
		prep->weight_attr = weight_attr;
	}
	return prep;
}

void *compute_main_path_splc_basket(ExecutionContext *ctx)
{
	return main_path_prepare_basket(ctx, CRIT_WEIGHT_SPLC, "main-path-weight-splc");
}
void *compute_main_path_spc_basket(ExecutionContext *ctx)
{
	return main_path_prepare_basket(ctx, CRIT_WEIGHT_SPC, "main-path-weight-spc");
}
void *compute_main_path_spe_basket(ExecutionContext *ctx)
{
	return main_path_prepare_basket(ctx, CRIT_WEIGHT_SPE, "main-path-weight-spe");
}
void *compute_main_path_unit_basket(ExecutionContext *ctx)
{
	return main_path_prepare_basket(ctx, CRIT_WEIGHT_UNIT, "main-path-weight-unit");
}
void *compute_main_path_splc_path(ExecutionContext *ctx)
{
	return main_path_compute_selection(ctx, "main-path-weight-splc", MAIN_PATH_SELECT_PATH);
}
void *compute_main_path_spc_path(ExecutionContext *ctx)
{
	return main_path_compute_selection(ctx, "main-path-weight-spc", MAIN_PATH_SELECT_PATH);
}
void *compute_main_path_spe_path(ExecutionContext *ctx)
{
	return main_path_compute_selection(ctx, "main-path-weight-spe", MAIN_PATH_SELECT_PATH);
}
void *compute_main_path_unit_path(ExecutionContext *ctx)
{
	return main_path_compute_selection(ctx, "main-path-weight-unit", MAIN_PATH_SELECT_PATH);
}

void apply_main_path_selection(ExecutionContext *ctx, void *result_data)
{
	if (!ctx || !ctx->app_state || !result_data)
		return;
	MainPathSelectionResult *result = result_data;
	GraphData *graph = &ctx->app_state->current_graph;
	Renderer *renderer = &ctx->app_state->renderer;
	if (graph->node_count != result->node_count)
		return;
	igraph_vector_int_t flags;
	if (igraph_vector_int_init(&flags, result->node_count) != IGRAPH_SUCCESS)
		return;
	for (uint32_t v = 0; v < result->node_count; v++)
		VECTOR(flags)[v] = result->flags[v];
	char attr[64];
	snprintf(attr, sizeof(attr), "main-path-%s-%s", result->selection == MAIN_PATH_SELECT_BASKET ? "basket" : "path", main_path_method_name(result->weight_attr));
	graph_cache_store_vertex_attr_int(&graph->g, attr, &flags);
	graph_reset_emphasis(graph);
	for (uint32_t v = 0; v < graph->node_count; v++)
		if (!VECTOR(flags)[v])
			graph->nodes[v].emphasis = EMPHASIS_DIMMED;
	renderer->needsAttributeUpload = VK_TRUE;
	renderer_update_graph(renderer, graph);
	igraph_vector_int_destroy(&flags);
}

void free_main_path_selection(void *result_data)
{
	MainPathSelectionResult *result = result_data;
	if (!result)
		return;
	free(result->flags);
	free(result);
}

void apply_main_path_basket(ExecutionContext *ctx, void *result_data)
{
	MainPathPrep *prep = result_data;
	if (!ctx || !ctx->app_state || !prep)
		return;
	AppState *state = ctx->app_state;
	GraphData *graph = &state->current_graph;
	if (prep->node_count != (igraph_integer_t)graph->node_count)
		return;
	igraph_vector_t weights;
	if (igraph_vector_init(&weights, 0) != IGRAPH_SUCCESS)
		return;
	if (!graph_cache_load_edge_attr(&graph->g, prep->weight_attr, &weights) || igraph_vector_size(&weights) != graph->edge_count) {
		fprintf(stderr, "[Main Path] Run Weighting before Basket selection\n");
		igraph_vector_destroy(&weights);
		return;
	}
	for (uint32_t e = 0; e < graph->edge_count; e++)
		if (!isfinite(VECTOR(weights)[e])) {
			fprintf(stderr, "[Main Path] Basket rejects overflowing weights; use SPE\n");
			igraph_vector_destroy(&weights);
			return;
		}
	graph_reset_emphasis(graph);
	if (!graph_rebuild_edges(graph))
		return;
	renderer_update_graph(&state->renderer, graph);
	if (!renderer_init_criticality_buffers(&state->renderer, graph, &prep->levels, prep->num_levels, prep->weight_mode, &weights) || !renderer_start_main_path_selection(&state->renderer))
		fprintf(stderr, "[MainPath] failed to initialize GPU basket selection\n");
	igraph_vector_destroy(&weights);
}

bool poll_main_path_basket(ExecutionContext *ctx)
{
	if (!ctx || !ctx->app_state)
		return true;
	Renderer *renderer = &ctx->app_state->renderer;
	CritComputeContext *compute = &renderer->crit;
	if (compute->active || compute->readback_pending)
		return false;
	if (!compute->selection_ready)
		return true;
	GraphData *graph = &ctx->app_state->current_graph;
	igraph_vector_int_t flags;
	if (igraph_vector_int_init(&flags, compute->node_count) == IGRAPH_SUCCESS) {
		for (uint32_t v = 0; v < compute->node_count; v++)
			VECTOR(flags)[v] = compute->selection_flags[v];
		const char *method = compute->weight_mode == CRIT_WEIGHT_SPLC ? "splc" : (compute->weight_mode == CRIT_WEIGHT_SPC ? "spc" : (compute->weight_mode == CRIT_WEIGHT_SPE ? "spe" : "unit"));
		char attr[64];
		snprintf(attr, sizeof(attr), "main-path-basket-%s", method);
		graph_cache_store_vertex_attr_int(&graph->g, attr, &flags);
		graph_reset_emphasis(graph);
		for (uint32_t v = 0; v < graph->node_count; v++)
			if (!VECTOR(flags)[v])
				graph->nodes[v].emphasis = EMPHASIS_DIMMED;
		renderer->needsAttributeUpload = VK_TRUE;
		renderer_update_graph(renderer, graph);
		igraph_vector_int_destroy(&flags);
	}
	compute->selection_ready = false;
	free(compute->selection_flags);
	compute->selection_flags = NULL;
	return true;
}
