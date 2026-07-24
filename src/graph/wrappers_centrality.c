/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "graph/wrappers_centrality.h"
#include "app_state.h"
#include "interaction/state.h"
#include "vulkan/renderer.h"
#include <igraph.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

// ============================================================================
// Worker Functions
// Each returns igraph_vector_t* (centrality scores) on success, NULL on failure
// ============================================================================

// Degree centrality
void *compute_igraph_degree(ExecutionContext *ctx)
{
	igraph_t *graph = &ctx->app_state->current_graph.g;
	if (!ctx->app_state->current_graph.graph_initialized) {
		fprintf(stderr, "[%s] Graph not initialized\n", __func__);
		return NULL;
	}
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_vector_t *result = IGRAPH_MALLOC(sizeof(igraph_vector_t));
	if (igraph_vector_init(result, vcount) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		return NULL;
	}

	igraph_vector_int_t degrees;
	if (igraph_vector_int_init(&degrees, vcount) != IGRAPH_SUCCESS) {
		igraph_vector_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}
	igraph_degree(graph, &degrees, igraph_vss_all(), IGRAPH_ALL, IGRAPH_LOOPS);

	for (igraph_integer_t i = 0; i < vcount; i++) {
		VECTOR(*result)[i] = (igraph_real_t)VECTOR(degrees)[i];
	}

	igraph_vector_int_destroy(&degrees);
	return result;
}

// Closeness centrality
void *compute_igraph_closeness_cutoff(ExecutionContext *ctx)
{
	igraph_t *graph = &ctx->app_state->current_graph.g;
	if (!ctx->app_state->current_graph.graph_initialized) {
		fprintf(stderr, "[%s] Graph not initialized\n", __func__);
		return NULL;
	}
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_vector_t *result = IGRAPH_MALLOC(sizeof(igraph_vector_t));
	if (igraph_vector_init(result, vcount) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		return NULL;
	}

	igraph_bool_t all_reach;
	igraph_vector_t weights;
	bool has_weights = graph_build_edge_weights(graph, &weights);

	igraph_error_t code = igraph_closeness_cutoff(graph, result, NULL, &all_reach, igraph_vss_all(), IGRAPH_ALL, has_weights ? &weights : NULL, 1, 0);

	if (has_weights)
		igraph_vector_destroy(&weights);

	if (code != IGRAPH_SUCCESS) {
		igraph_vector_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}
	return result;
}

// Betweenness centrality
void *compute_igraph_betweenness(ExecutionContext *ctx)
{
	igraph_t *graph = &ctx->app_state->current_graph.g;
	if (!ctx->app_state->current_graph.graph_initialized) {
		fprintf(stderr, "[%s] Graph not initialized\n", __func__);
		return NULL;
	}
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_vector_t *result = IGRAPH_MALLOC(sizeof(igraph_vector_t));
	if (igraph_vector_init(result, vcount) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		return NULL;
	}

	igraph_vector_t weights;
	bool has_weights = graph_build_edge_weights(graph, &weights);

	igraph_error_t code = igraph_betweenness(graph, has_weights ? &weights : NULL, result, igraph_vss_all(), igraph_is_directed(graph) ? IGRAPH_DIRECTED : IGRAPH_UNDIRECTED, 1);

	if (has_weights)
		igraph_vector_destroy(&weights);

	if (code != IGRAPH_SUCCESS) {
		igraph_vector_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}
	return result;
}

// Eigenvector centrality
void *compute_igraph_eigenvector_centrality(ExecutionContext *ctx)
{
	igraph_t *graph = &ctx->app_state->current_graph.g;
	if (!ctx->app_state->current_graph.graph_initialized) {
		fprintf(stderr, "[%s] Graph not initialized\n", __func__);
		return NULL;
	}
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_vector_t *result = IGRAPH_MALLOC(sizeof(igraph_vector_t));
	if (igraph_vector_init(result, vcount) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		return NULL;
	}

	igraph_vector_t weights;
	bool has_weights = graph_build_edge_weights(graph, &weights);

	igraph_error_t code = igraph_eigenvector_centrality(graph, result, NULL, IGRAPH_ALL, has_weights ? &weights : NULL, NULL);

	if (has_weights)
		igraph_vector_destroy(&weights);

	if (code != IGRAPH_SUCCESS) {
		igraph_vector_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}
	return result;
}

// PageRank
void *compute_igraph_pagerank(ExecutionContext *ctx)
{
	igraph_t *graph = &ctx->app_state->current_graph.g;
	if (!ctx->app_state->current_graph.graph_initialized) {
		fprintf(stderr, "[%s] Graph not initialized\n", __func__);
		return NULL;
	}
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_vector_t *result = IGRAPH_MALLOC(sizeof(igraph_vector_t));
	if (igraph_vector_init(result, vcount) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		return NULL;
	}

	igraph_vector_t weights;
	bool has_weights = graph_build_edge_weights(graph, &weights);

	igraph_error_t code = igraph_pagerank(graph, has_weights ? &weights : NULL, result, NULL, 0.85, igraph_is_directed(graph) ? IGRAPH_DIRECTED : IGRAPH_UNDIRECTED, igraph_vss_all(), IGRAPH_PAGERANK_ALGO_PRPACK, NULL);

	if (has_weights)
		igraph_vector_destroy(&weights);

	if (code != IGRAPH_SUCCESS) {
		igraph_vector_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}
	return result;
}

// HITS (Hub and Authority scores)
// Returns a combined score: average of hub and authority, or just hub?
// For simplicity, we'll return hub scores as the centrality measure
void *compute_igraph_hub_and_authority_scores(ExecutionContext *ctx)
{
	igraph_t *graph = &ctx->app_state->current_graph.g;
	if (!ctx->app_state->current_graph.graph_initialized) {
		fprintf(stderr, "[%s] Graph not initialized\n", __func__);
		return NULL;
	}
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_vector_t *result = IGRAPH_MALLOC(sizeof(igraph_vector_t));
	if (igraph_vector_init(result, vcount) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		return NULL;
	}

	igraph_vector_t weights;
	bool has_weights = graph_build_edge_weights(graph, &weights);

	if (!igraph_is_directed(graph)) {
		// For undirected graphs, HITS reduces to eigenvector centrality
		igraph_error_t code = igraph_eigenvector_centrality(graph, result, NULL, IGRAPH_ALL, has_weights ? &weights : NULL, NULL);
		if (has_weights)
			igraph_vector_destroy(&weights);
		if (code != IGRAPH_SUCCESS) {
			igraph_vector_destroy(result);
			IGRAPH_FREE(result);
			return NULL;
		}
	} else {
		igraph_vector_t hub_scores;
		igraph_vector_t authority_scores;
		igraph_vector_init(&hub_scores, vcount);
		igraph_vector_init(&authority_scores, vcount);

		igraph_error_t code = igraph_hub_and_authority_scores(graph, &hub_scores, &authority_scores, NULL, has_weights ? &weights : NULL, NULL);

		if (has_weights)
			igraph_vector_destroy(&weights);

		if (code != IGRAPH_SUCCESS) {
			igraph_vector_destroy(&hub_scores);
			igraph_vector_destroy(&authority_scores);
			igraph_vector_destroy(result);
			IGRAPH_FREE(result);
			return NULL;
		}

		// Use hub scores as the centrality measure (could also use authority or average)
		for (igraph_integer_t i = 0; i < vcount; i++) {
			VECTOR(*result)[i] = VECTOR(hub_scores)[i];
		}

		igraph_vector_destroy(&hub_scores);
		igraph_vector_destroy(&authority_scores);
	}

	return result;
}

// Harmonic centrality
void *compute_igraph_harmonic_centrality(ExecutionContext *ctx)
{
	igraph_t *graph = &ctx->app_state->current_graph.g;
	if (!ctx->app_state->current_graph.graph_initialized) {
		fprintf(stderr, "[%s] Graph not initialized\n", __func__);
		return NULL;
	}
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_vector_t *result = IGRAPH_MALLOC(sizeof(igraph_vector_t));
	if (igraph_vector_init(result, vcount) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		return NULL;
	}

	igraph_vector_t weights;
	bool has_weights = graph_build_edge_weights(graph, &weights);

	igraph_error_t code = igraph_harmonic_centrality(graph, result, igraph_vss_all(), IGRAPH_ALL, has_weights ? &weights : NULL, 1);

	if (has_weights)
		igraph_vector_destroy(&weights);

	if (code != IGRAPH_SUCCESS) {
		igraph_vector_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}
	return result;
}

// Strength (weighted degree sum)
void *compute_igraph_strength(ExecutionContext *ctx)
{
	igraph_t *graph = &ctx->app_state->current_graph.g;
	if (!ctx->app_state->current_graph.graph_initialized) {
		fprintf(stderr, "[%s] Graph not initialized\n", __func__);
		return NULL;
	}
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_vector_t *result = IGRAPH_MALLOC(sizeof(igraph_vector_t));
	if (igraph_vector_init(result, vcount) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		return NULL;
	}

	igraph_vector_t weights;
	bool has_weights = graph_build_edge_weights(graph, &weights);

	igraph_error_t code = igraph_strength(graph, result, igraph_vss_all(), IGRAPH_ALL, IGRAPH_LOOPS, has_weights ? &weights : NULL);

	if (has_weights)
		igraph_vector_destroy(&weights);

	if (code != IGRAPH_SUCCESS) {
		igraph_vector_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}
	return result;
}

// Coreness (k-core decomposition)
void *compute_igraph_coreness(ExecutionContext *ctx)
{
	igraph_t *graph = &ctx->app_state->current_graph.g;
	if (!ctx->app_state->current_graph.graph_initialized) {
		fprintf(stderr, "[%s] Graph not initialized\n", __func__);
		return NULL;
	}
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_vector_t *result = IGRAPH_MALLOC(sizeof(igraph_vector_t));
	if (igraph_vector_init(result, vcount) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		return NULL;
	}

	igraph_vector_int_t coreness;
	if (igraph_vector_int_init(&coreness, vcount) != IGRAPH_SUCCESS) {
		igraph_vector_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}
	igraph_error_t code = igraph_coreness(graph, &coreness, IGRAPH_ALL);

	if (code != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(&coreness);
		igraph_vector_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}

	for (igraph_integer_t i = 0; i < vcount; i++) {
		VECTOR(*result)[i] = (igraph_real_t)VECTOR(coreness)[i];
	}

	igraph_vector_int_destroy(&coreness);
	return result;
}

// Constraint (structural holes)
void *compute_igraph_constraint(ExecutionContext *ctx)
{
	igraph_t *graph = &ctx->app_state->current_graph.g;
	if (!ctx->app_state->current_graph.graph_initialized) {
		fprintf(stderr, "[%s] Graph not initialized\n", __func__);
		return NULL;
	}
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_vector_t *result = IGRAPH_MALLOC(sizeof(igraph_vector_t));
	if (igraph_vector_init(result, vcount) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		return NULL;
	}

	igraph_vector_t weights;
	bool has_weights = graph_build_edge_weights(graph, &weights);

	igraph_error_t code = igraph_constraint(graph, result, igraph_vss_all(), has_weights ? &weights : NULL);

	if (has_weights)
		igraph_vector_destroy(&weights);

	if (code != IGRAPH_SUCCESS) {
		igraph_vector_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}
	return result;
}

// ============================================================================
// Apply and Free Functions
// ============================================================================

void apply_centrality_scores(ExecutionContext *ctx, void *result_data)
{
	if (!ctx || !ctx->app_state || !result_data) {
		fprintf(stderr, "[apply_centrality_scores] Error: Invalid parameters\n");
		return;
	}

	AppState *state = ctx->app_state;
	GraphData *data = &state->current_graph;
	Renderer *renderer = &state->renderer;
	igraph_vector_t *scores = (igraph_vector_t *)result_data;

	if (!data->graph_initialized) {
		fprintf(stderr, "[apply_centrality_scores] Error: Graph not initialized\n");
		return;
	}

	if (igraph_vector_size(scores) != data->node_count) {
		fprintf(stderr, "[apply_centrality_scores] Error: Scores size doesn't match node count\n");
		return;
	}

	// Find min/max for normalization
	igraph_real_t min_v, max_v;
	igraph_vector_minmax(scores, &min_v, &max_v);
	igraph_real_t range = max_v - min_v;

	// Map centrality scores to node size and glow
	for (int i = 0; i < data->node_count; i++) {
		igraph_real_t val = VECTOR(*scores)[i];
		float normalized = (range > 0) ? (float)((val - min_v) / range) : 1.0f;

		// Apply to node size (scale between NODE_SIZE_MIN and NODE_SIZE_MAX)
		data->nodes[i].size = NODE_SIZE_MIN + normalized * (NODE_SIZE_MAX - NODE_SIZE_MIN);
	}

	// Refresh renderer
	renderer->needsAttributeUpload = VK_TRUE;
	renderer_update_graph(renderer, data);

	printf("[apply_centrality_scores] Centrality applied\n");
}

void centrality_scores_free(void *result_data)
{
	if (result_data) {
		igraph_vector_destroy((igraph_vector_t *)result_data);
		IGRAPH_FREE(result_data);
	}
}
