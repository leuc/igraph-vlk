/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "graph/wrappers_flow.h"
#include "app_state.h"
#include "graph/graph_animation.h"
#include "graph/graph_color.h"
#include "graph/graph_core.h"
#include "vulkan/renderer.h"

#include <igraph.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAXFLOW_SENTINEL_RANK 10000000

typedef struct
{
	float *edge_flows;
	float max_flow_value;
	int sample_count;
	int *node_ranks;
} MaxflowResult;

void *compute_maxflow_sampling(ExecutionContext *ctx)
{
	igraph_t *graph = &ctx->app_state->current_graph.g;
	GraphData *graph_data = &ctx->app_state->current_graph;

	if (!graph_data->graph_initialized) {
		fprintf(stderr, "[%s] Graph not initialized\n", __func__);
		return NULL;
	}

	igraph_integer_t n = igraph_vcount(graph);
	igraph_integer_t m = igraph_ecount(graph);

	if (n < 2 || m == 0) {
		fprintf(stderr, "[maxflow] Graph too small for flow analysis (n=%d, m=%d)\n", (int)n, (int)m);
		return NULL;
	}

	if (!igraph_is_directed(graph)) {
		fprintf(stderr, "[maxflow] Graph must be directed for flow analysis\n");
		return NULL;
	}

	MaxflowResult *result = malloc(sizeof(MaxflowResult));
	if (!result)
		return NULL;

	result->edge_flows = calloc(m, sizeof(float));
	if (!result->edge_flows) {
		free(result);
		return NULL;
	}

	result->max_flow_value = 0.0f;
	result->sample_count = 0;

	igraph_rng_seed(igraph_rng_default(), (igraph_uint_t)(size_t)time(NULL));

	igraph_vector_t weights;
	bool has_weights = graph_build_edge_weights(graph, &weights);

	int num_pairs = 5;
	int pair_idx = 0;
	igraph_integer_t primary_source = -1;

	for (int attempt = 0; attempt < 10 && pair_idx < num_pairs; attempt++) {
		igraph_integer_t source = igraph_rng_get_integer(igraph_rng_default(), 0, (igraph_integer_t)n - 1);

		igraph_vector_int_t order;
		if (igraph_vector_int_init(&order, 0) != IGRAPH_SUCCESS)
			continue;
		igraph_error_t bfs_ret = igraph_bfs_simple(graph, source, IGRAPH_OUT, &order, NULL, NULL);
		if (bfs_ret != IGRAPH_SUCCESS) {
			igraph_vector_int_destroy(&order);
			continue;
		}

		igraph_integer_t order_len = igraph_vector_int_size(&order);
		igraph_integer_t target = -1;
		for (igraph_integer_t i = 0; i < order_len; i++) {
			if (VECTOR(order)[i] != source) {
				target = VECTOR(order)[i];
				break;
			}
		}
		igraph_vector_int_destroy(&order);

		if (target < 0)
			continue;

		igraph_vector_t flow;
		if (igraph_vector_init(&flow, 0) != IGRAPH_SUCCESS)
			continue;

		igraph_real_t flow_value;
		igraph_error_t code = igraph_maxflow(graph, &flow_value, &flow, NULL, NULL, NULL, source, target, has_weights ? &weights : NULL, NULL);

		if (code == IGRAPH_SUCCESS && igraph_vector_size(&flow) == m) {
			result->max_flow_value = fmaxf(result->max_flow_value, (float)flow_value);

			for (igraph_integer_t i = 0; i < m; i++)
				result->edge_flows[i] += (float)VECTOR(flow)[i];

			result->sample_count++;
			if (primary_source < 0)
				primary_source = source;
			fprintf(stderr, "[maxflow]   pair[%d] %" IGRAPH_PRId "→%" IGRAPH_PRId " flow_value=%.4f reachable=%" IGRAPH_PRId "\n", pair_idx, source, target, (float)flow_value, order_len);
		} else {
			fprintf(stderr, "[maxflow]   pair[%d] %" IGRAPH_PRId "→%" IGRAPH_PRId " SKIPPED code=%d flow_size=%" IGRAPH_PRId " reachable=%" IGRAPH_PRId "\n", pair_idx, source, target, (int)code, igraph_vector_size(&flow), order_len);
		}

		igraph_vector_destroy(&flow);
		pair_idx++;
	}

	if (has_weights)
		igraph_vector_destroy(&weights);

	if (primary_source < 0)
		primary_source = 0;

	if (result->sample_count > 0) {
		for (igraph_integer_t i = 0; i < m; i++)
			result->edge_flows[i] /= result->sample_count;
	}

	if (result->max_flow_value == 0.0f)
		result->max_flow_value = 1.0f;

	float flow_threshold = result->max_flow_value * 0.01f;
	int nz_edges = 0;
	for (igraph_integer_t i = 0; i < m; i++) {
		if (result->edge_flows[i] > 0.0f)
			nz_edges++;
	}
	fprintf(stderr, "[maxflow] %d pairs, max=%.4f nz=%d/%" IGRAPH_PRId "\n", result->sample_count, result->max_flow_value, nz_edges, m);

	int *node_ranks = malloc(n * sizeof(int));
	if (!node_ranks) {
		free(result->edge_flows);
		free(result);
		return NULL;
	}
	for (igraph_integer_t i = 0; i < n; i++)
		node_ranks[i] = MAXFLOW_SENTINEL_RANK;

	// Build ragged adjacency of flow edges
	int *adj_cap = calloc(n, sizeof(int));
	int **adj = NULL;
	int *adj_cnt = NULL;
	int *queue = NULL;

	if (adj_cap) {
		adj_cnt = calloc(n, sizeof(int));
		adj = malloc(n * sizeof(int *));
		if (adj_cnt && adj) {
			for (igraph_integer_t i = 0; i < m; i++) {
				if (result->edge_flows[i] > flow_threshold)
					adj_cap[graph_data->edges[i].from]++;
			}
			for (igraph_integer_t i = 0; i < n; i++)
				adj[i] = adj_cap[i] > 0 ? malloc(adj_cap[i] * sizeof(int)) : NULL;

			for (igraph_integer_t i = 0; i < m; i++) {
				if (result->edge_flows[i] > flow_threshold) {
					igraph_integer_t f = graph_data->edges[i].from;
					adj[f][adj_cnt[f]++] = graph_data->edges[i].to;
				}
			}

			queue = malloc(n * sizeof(int));
			if (queue) {
				int head = 0, tail = 0;
				node_ranks[primary_source] = 0;
				queue[tail++] = primary_source;
				while (head < tail) {
					int v = queue[head++];
					for (int j = 0; j < adj_cnt[v]; j++) {
						int u = adj[v][j];
						if (node_ranks[u] == MAXFLOW_SENTINEL_RANK) {
							node_ranks[u] = node_ranks[v] + 1;
							queue[tail++] = u;
						}
					}
				}
			}
		}
	}

	// Cleanup adjacency
	if (adj) {
		for (igraph_integer_t i = 0; i < n; i++)
			free(adj[i]);
		free(adj);
	}
	free(adj_cap);
	free(adj_cnt);
	free(queue);

	result->node_ranks = node_ranks;

	printf("[maxflow] Computed flows for %d source→target pairs, max=%.4f, nz=%d/%" IGRAPH_PRId "\n", result->sample_count, result->max_flow_value, nz_edges, m);

	return result;
}

void apply_maxflow_sampling(ExecutionContext *ctx, void *result_data)
{
	if (!ctx || !ctx->app_state || !result_data)
		return;

	MaxflowResult *flow_result = (MaxflowResult *)result_data;
	AppState *state = ctx->app_state;
	GraphData *graph_data = &state->current_graph;

	if (graph_data->edge_count != (int)igraph_ecount(&graph_data->g)) {
		fprintf(stderr, "[maxflow apply] Edge count mismatch\n");
		return;
	}

	state->renderer.needsAttributeUpload = VK_TRUE;

	graph_reset_emphasis(graph_data);
	graph_animation_clear(&state->renderer);

	if (!graph_rebuild_edges(graph_data)) {
		fprintf(stderr, "[maxflow apply] graph_rebuild_edges failed\n");
		return;
	}
	for (int i = 0; i < graph_data->edge_count; i++) {
		graph_data->edges[i].weight = flow_result->edge_flows[i];
	}
	float *edge_values = NULL;
	if (flow_result->max_flow_value > 0.0f) {
		edge_values = malloc(sizeof(float) * graph_data->edge_count);
		if (!edge_values) {
			fprintf(stderr, "[maxflow apply] Failed to allocate normalized edge values\n");
			return;
		}
		float denominator = logf(flow_result->max_flow_value + 1.0f);
		for (uint32_t i = 0; i < graph_data->edge_count; i++)
			edge_values[i] = logf(flow_result->edge_flows[i] + 1.0f) / denominator;
	}
	GraphAnimationRequest request = {.node_steps = flow_result->node_ranks, .edge_values = edge_values, .duration = 4.0f};
	graph_animation_play(&state->renderer, graph_data, &request);
	free(edge_values);

	renderer_update_graph(&state->renderer, graph_data);
	state->renderer.label.tree_needs_rebuild = true;

	printf("Max flow visualization applied (max flow: %.2f)\n", flow_result->max_flow_value);
}

void free_maxflow_result(void *result_data)
{
	if (!result_data)
		return;

	MaxflowResult *result = (MaxflowResult *)result_data;
	free(result->edge_flows);
	free(result->node_ranks);
	free(result);
}
