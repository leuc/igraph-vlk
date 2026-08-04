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

#include "graph/graph_animation.h"
#include "graph/graph_color.h"
#include "graph/graph_core.h"
#include "graph/wrappers_splc.h"
#include "vulkan/renderer.h"

#define MAIN_PATH_MAX_REPLAY_PATHS 64
#define MAIN_PATH_REPLAY_DURATION 12.0f

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

static void main_path_free_paths(MainPathPath *paths, uint32_t count)
{
	for (uint32_t i = 0; i < count; i++)
		free(paths[i].edges);
}

void main_path_play_weighting(Renderer *renderer, GraphData *graph, const char *weight_attr)
{
	if (!renderer || !graph || !weight_attr || graph->edge_count == 0)
		return;
	graph_animation_clear(renderer);
	igraph_vector_t values;
	if (igraph_vector_init(&values, 0) != IGRAPH_SUCCESS)
		return;
	if (!graph_cache_load_edge_attr(&graph->g, weight_attr, &values)) {
		fprintf(stderr, "[Main Path] Weighting '%s' is unavailable\n", weight_attr);
		igraph_vector_destroy(&values);
		return;
	}
	if (igraph_vector_size(&values) != graph->edge_count) {
		igraph_vector_destroy(&values);
		fprintf(stderr, "[Main Path] Weighting '%s' has stale edge count\n", weight_attr);
		return;
	}

	igraph_vector_int_t levels;
	if (igraph_vector_int_init(&levels, 0) != IGRAPH_SUCCESS) {
		igraph_vector_destroy(&values);
		return;
	}
	igraph_integer_t max_level = calculate_dag_levels(&graph->g, &levels);
	if (max_level < 0) {
		igraph_vector_int_destroy(&levels);
		igraph_vector_destroy(&values);
		return;
	}
	igraph_vector_int_t order;
	uint32_t *first = NULL, *next = NULL, *from = NULL, *to = NULL;
	if (!main_path_topological_order(&graph->g, &order)) {
		igraph_vector_int_destroy(&levels);
		igraph_vector_destroy(&values);
		return;
	}
	if (!main_path_build_adjacency(&graph->g, &first, &next, &from, &to)) {
		igraph_vector_int_destroy(&order);
		igraph_vector_int_destroy(&levels);
		igraph_vector_destroy(&values);
		return;
	}

	float *weights = malloc(sizeof(float) * graph->edge_count);
	float *residual = malloc(sizeof(float) * graph->edge_count);
	float max_weight = 0.0f;
	if (!weights || !residual) {
		free(weights);
		free(residual);
		free(first);
		free(next);
		free(from);
		free(to);
		igraph_vector_int_destroy(&order);
		igraph_vector_int_destroy(&levels);
		igraph_vector_destroy(&values);
		return;
	}
	for (uint32_t e = 0; e < graph->edge_count; e++) {
		weights[e] = (float)VECTOR(values)[e];
		if (!isfinite(weights[e])) {
			fprintf(stderr, "[Main Path] '%s' contains non-finite weights; use SPE for overflow-safe playback\n", weight_attr);
			free(weights);
			free(residual);
			free(first);
			free(next);
			free(from);
			free(to);
			igraph_vector_int_destroy(&order);
			igraph_vector_int_destroy(&levels);
			igraph_vector_destroy(&values);
			return;
		}
		if (weights[e] < 0.0f)
			weights[e] = 0.0f;
		residual[e] = weights[e];
		if (weights[e] > max_weight)
			max_weight = weights[e];
	}

	MainPathPath paths[MAIN_PATH_MAX_REPLAY_PATHS] = {0};
	uint32_t path_count = 0;
	while (path_count < MAIN_PATH_MAX_REPLAY_PATHS && main_path_best_path(&graph->g, &order, first, next, to, weights, residual, &paths[path_count])) {
		for (uint32_t i = 0; i < paths[path_count].count; i++) {
			uint32_t e = paths[path_count].edges[i];
			residual[e] -= paths[path_count].bottleneck;
			if (residual[e] < 1e-6f)
				residual[e] = 0.0f;
		}
		path_count++;
	}

	uint32_t *counts = calloc(graph->edge_count + 1, sizeof(uint32_t));
	if (!counts) {
		main_path_free_paths(paths, path_count);
		free(weights);
		free(residual);
		free(first);
		free(next);
		free(from);
		free(to);
		igraph_vector_int_destroy(&order);
		igraph_vector_int_destroy(&levels);
		igraph_vector_destroy(&values);
		return;
	}
	uint32_t total_events = 0;
	for (uint32_t p = 0; p < path_count; p++)
		for (uint32_t i = 0; i < paths[p].count; i++) {
			counts[paths[p].edges[i] + 1]++;
			total_events++;
		}
	for (uint32_t e = 1; e <= graph->edge_count; e++)
		counts[e] += counts[e - 1];
	uint32_t *cursor = malloc(sizeof(uint32_t) * graph->edge_count);
	RendererAnimEvent *events = malloc(sizeof(RendererAnimEvent) * (total_events > 0 ? total_events : 1));
	int *node_steps = malloc(sizeof(int) * graph->node_count);
	float *edge_values = malloc(sizeof(float) * graph->edge_count);
	if (cursor && events && node_steps && edge_values) {
		memcpy(cursor, counts, sizeof(uint32_t) * graph->edge_count);
		for (uint32_t v = 0; v < graph->node_count; v++)
			node_steps[v] = VECTOR(levels)[v];
		for (uint32_t e = 0; e < graph->edge_count; e++)
			edge_values[e] = max_weight > 0.0f ? weights[e] / max_weight : 0.0f;
		float path_window = path_count > 0 ? MAIN_PATH_REPLAY_DURATION / (float)path_count : 0.0f;
		for (uint32_t p = 0; p < path_count; p++) {
			float edge_window = paths[p].count > 0 ? path_window / (float)paths[p].count : 0.0f;
			for (uint32_t i = 0; i < paths[p].count; i++) {
				uint32_t e = paths[p].edges[i];
				uint32_t dst = cursor[e]++;
				events[dst] = (RendererAnimEvent){.start_time = (float)p * path_window + (float)i * edge_window, .duration = edge_window, .value = edge_values[e]};
			}
		}
		GraphAnimationRequest request = {.node_steps = node_steps, .edge_values = edge_values, .edge_event_offsets = counts, .edge_events = events, .edge_event_count = total_events, .duration = MAIN_PATH_REPLAY_DURATION};
		graph_animation_play(renderer, graph, &request);
	}
	free(cursor);
	free(events);
	free(node_steps);
	free(edge_values);
	free(counts);
	main_path_free_paths(paths, path_count);
	free(weights);
	free(residual);
	free(first);
	free(next);
	free(from);
	free(to);
	igraph_vector_int_destroy(&order);
	igraph_vector_int_destroy(&levels);
	igraph_vector_destroy(&values);
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

void *compute_main_path_splc_basket(ExecutionContext *ctx)
{
	return main_path_compute_selection(ctx, "main-path-weight-splc", MAIN_PATH_SELECT_BASKET);
}
void *compute_main_path_spc_basket(ExecutionContext *ctx)
{
	return main_path_compute_selection(ctx, "main-path-weight-spc", MAIN_PATH_SELECT_BASKET);
}
void *compute_main_path_spe_basket(ExecutionContext *ctx)
{
	return main_path_compute_selection(ctx, "main-path-weight-spe", MAIN_PATH_SELECT_BASKET);
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
