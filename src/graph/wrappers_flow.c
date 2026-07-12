/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "graph/wrappers_flow.h"
#include "app_state.h"
#include "graph/graph_core.h"
#include "vulkan/renderer.h"
#include "vulkan/renderer_anim.h"

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
	uint32_t *edge_from;
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

	int num_pairs = 5;
	int pair_idx = 0;
	int primary_source = -1;

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

		int order_len = igraph_vector_int_size(&order);
		int target = -1;
		for (int i = 0; i < order_len; i++) {
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
		igraph_error_t code = igraph_maxflow(graph, &flow_value, &flow, NULL, NULL, NULL, source, target, NULL, NULL);

		if (code == IGRAPH_SUCCESS && igraph_vector_size(&flow) == m) {
			result->max_flow_value = fmaxf(result->max_flow_value, (float)flow_value);

			for (int i = 0; i < (int)m; i++)
				result->edge_flows[i] += (float)VECTOR(flow)[i];

			result->sample_count++;
			if (primary_source < 0)
				primary_source = (int)source;
			fprintf(stderr, "[maxflow]   pair[%d] %d→%d flow_value=%.4f reachable=%d\n", pair_idx, (int)source, target, (float)flow_value, order_len);
		} else {
			fprintf(stderr, "[maxflow]   pair[%d] %d→%d SKIPPED code=%d flow_size=%d reachable=%d\n", pair_idx, (int)source, target, (int)code, (int)igraph_vector_size(&flow), order_len);
		}

		igraph_vector_destroy(&flow);
		pair_idx++;
	}

	if (primary_source < 0)
		primary_source = 0;

	if (result->sample_count > 0) {
		for (int i = 0; i < (int)m; i++)
			result->edge_flows[i] /= result->sample_count;
	}

	if (result->max_flow_value == 0.0f)
		result->max_flow_value = 1.0f;

	float flow_threshold = result->max_flow_value * 0.01f;
	int nz_edges = 0;
	for (int i = 0; i < (int)m; i++) {
		if (result->edge_flows[i] > 0.0f)
			nz_edges++;
	}
	fprintf(stderr, "[maxflow] %d pairs, max=%.4f nz=%d/%d\n", result->sample_count, result->max_flow_value, nz_edges, (int)m);

	int *node_ranks = malloc(n * sizeof(int));
	if (!node_ranks) {
		free(result->edge_flows);
		free(result);
		return NULL;
	}
	for (int i = 0; i < (int)n; i++)
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
			for (int i = 0; i < (int)m; i++) {
				if (result->edge_flows[i] > flow_threshold)
					adj_cap[graph_data->edges[i].from]++;
			}
			for (int i = 0; i < (int)n; i++)
				adj[i] = adj_cap[i] > 0 ? malloc(adj_cap[i] * sizeof(int)) : NULL;

			for (int i = 0; i < (int)m; i++) {
				if (result->edge_flows[i] > flow_threshold) {
					int f = graph_data->edges[i].from;
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
		for (int i = 0; i < (int)n; i++)
			free(adj[i]);
		free(adj);
	}
	free(adj_cap);
	free(adj_cnt);
	free(queue);

	result->node_ranks = node_ranks;

	result->edge_from = malloc(m * sizeof(uint32_t));
	if (result->edge_from) {
		for (int i = 0; i < (int)m; i++)
			result->edge_from[i] = graph_data->edges[i].from;
	}

	printf("[maxflow] Computed flows for %d source→target pairs, max=%.4f, nz=%d/%d\n", result->sample_count, result->max_flow_value, nz_edges, (int)m);

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

	renderer_anim_reset_nodes(&state->renderer, graph_data);
	renderer_anim_reset_edges(&state->renderer);

	if (!graph_rebuild_edges(graph_data)) {
		fprintf(stderr, "[maxflow apply] graph_rebuild_edges failed\n");
		return;
	}
	for (int i = 0; i < graph_data->edge_count; i++) {
		graph_data->edges[i].weight = flow_result->edge_flows[i];
	}
	renderer_anim_setup_edge_visualization(&state->renderer, graph_data, flow_result->edge_flows, graph_data->edge_count, flow_result->max_flow_value);
	renderer_anim_setup_flow_reveal(&state->renderer, graph_data, flow_result->node_ranks, flow_result->edge_from, graph_data->node_count, graph_data->edge_count, 4.0f);

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
	free(result->edge_from);
	free(result);
}
