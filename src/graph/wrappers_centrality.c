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
void *compute_igraph_degree(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_vector_t *result = malloc(sizeof(igraph_vector_t));
	if (igraph_vector_init(result, vcount) != IGRAPH_SUCCESS) {
		free(result);
		return NULL;
	}

	igraph_vector_int_t degrees;
	igraph_vector_int_init(&degrees, vcount);
	igraph_degree(graph, &degrees, igraph_vss_all(), IGRAPH_ALL, IGRAPH_LOOPS);

	for (igraph_integer_t i = 0; i < vcount; i++) {
		VECTOR(*result)[i] = (igraph_real_t)VECTOR(degrees)[i];
	}

	igraph_vector_int_destroy(&degrees);
	return result;
}

// Closeness centrality
void *compute_igraph_closeness_cutoff(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_vector_t *result = malloc(sizeof(igraph_vector_t));
	if (igraph_vector_init(result, vcount) != IGRAPH_SUCCESS) {
		free(result);
		return NULL;
	}

	igraph_bool_t all_reach;
	igraph_error_t code = igraph_closeness_cutoff(graph, result, NULL, &all_reach, igraph_vss_all(), IGRAPH_ALL, NULL, 1, 0);

	if (code != IGRAPH_SUCCESS) {
		igraph_vector_destroy(result);
		free(result);
		return NULL;
	}
	return result;
}

// Betweenness centrality
void *compute_igraph_betweenness(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_vector_t *result = malloc(sizeof(igraph_vector_t));
	if (igraph_vector_init(result, vcount) != IGRAPH_SUCCESS) {
		free(result);
		return NULL;
	}

	igraph_error_t code = igraph_betweenness(graph, NULL, result, igraph_vss_all(), igraph_is_directed(graph) ? IGRAPH_DIRECTED : IGRAPH_UNDIRECTED, 1);

	if (code != IGRAPH_SUCCESS) {
		igraph_vector_destroy(result);
		free(result);
		return NULL;
	}
	return result;
}

// Eigenvector centrality
void *compute_igraph_eigenvector_centrality(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_vector_t *result = malloc(sizeof(igraph_vector_t));
	if (igraph_vector_init(result, vcount) != IGRAPH_SUCCESS) {
		free(result);
		return NULL;
	}

	igraph_error_t code = igraph_eigenvector_centrality(graph, result, NULL, IGRAPH_ALL, NULL, NULL);

	if (code != IGRAPH_SUCCESS) {
		igraph_vector_destroy(result);
		free(result);
		return NULL;
	}
	return result;
}

// PageRank
void *compute_igraph_pagerank(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_vector_t *result = malloc(sizeof(igraph_vector_t));
	if (igraph_vector_init(result, vcount) != IGRAPH_SUCCESS) {
		free(result);
		return NULL;
	}

	igraph_error_t code = igraph_pagerank(graph, NULL, result, NULL, 0.85, igraph_is_directed(graph) ? IGRAPH_DIRECTED : IGRAPH_UNDIRECTED, igraph_vss_all(), IGRAPH_PAGERANK_ALGO_PRPACK, NULL);

	if (code != IGRAPH_SUCCESS) {
		igraph_vector_destroy(result);
		free(result);
		return NULL;
	}
	return result;
}

// HITS (Hub and Authority scores)
// Returns a combined score: average of hub and authority, or just hub?
// For simplicity, we'll return hub scores as the centrality measure
void *compute_igraph_hub_and_authority_scores(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_vector_t *result = malloc(sizeof(igraph_vector_t));
	if (igraph_vector_init(result, vcount) != IGRAPH_SUCCESS) {
		free(result);
		return NULL;
	}

	if (!igraph_is_directed(graph)) {
		// For undirected graphs, HITS reduces to eigenvector centrality
		igraph_error_t code = igraph_eigenvector_centrality(graph, result, NULL, IGRAPH_ALL, NULL, NULL);
		if (code != IGRAPH_SUCCESS) {
			igraph_vector_destroy(result);
			free(result);
			return NULL;
		}
	} else {
		igraph_vector_t hub_scores;
		igraph_vector_t authority_scores;
		igraph_vector_init(&hub_scores, vcount);
		igraph_vector_init(&authority_scores, vcount);

		igraph_error_t code = igraph_hub_and_authority_scores(graph, &hub_scores, &authority_scores, NULL, NULL, NULL);

		if (code != IGRAPH_SUCCESS) {
			igraph_vector_destroy(&hub_scores);
			igraph_vector_destroy(&authority_scores);
			igraph_vector_destroy(result);
			free(result);
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
void *compute_igraph_harmonic_centrality(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_vector_t *result = malloc(sizeof(igraph_vector_t));
	if (igraph_vector_init(result, vcount) != IGRAPH_SUCCESS) {
		free(result);
		return NULL;
	}

	igraph_error_t code = igraph_harmonic_centrality(graph, result, igraph_vss_all(), IGRAPH_ALL, NULL, 1);

	if (code != IGRAPH_SUCCESS) {
		igraph_vector_destroy(result);
		free(result);
		return NULL;
	}
	return result;
}

// Strength (weighted degree sum)
void *compute_igraph_strength(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_vector_t *result = malloc(sizeof(igraph_vector_t));
	if (igraph_vector_init(result, vcount) != IGRAPH_SUCCESS) {
		free(result);
		return NULL;
	}

	igraph_error_t code = igraph_strength(graph, result, igraph_vss_all(), IGRAPH_ALL, IGRAPH_LOOPS, NULL);

	if (code != IGRAPH_SUCCESS) {
		igraph_vector_destroy(result);
		free(result);
		return NULL;
	}
	return result;
}

// Constraint (structural holes)
void *compute_igraph_constraint(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_vector_t *result = malloc(sizeof(igraph_vector_t));
	if (igraph_vector_init(result, vcount) != IGRAPH_SUCCESS) {
		free(result);
		return NULL;
	}

	igraph_error_t code = igraph_constraint(graph, result, igraph_vss_all(), NULL);

	if (code != IGRAPH_SUCCESS) {
		igraph_vector_destroy(result);
		free(result);
		return NULL;
	}
	return result;
}

// ============================================================================
// Apply and Free Functions
// ============================================================================

void apply_centrality_scores(ExecutionContext *ctx, void *result_data)
{
	if (!ctx || !ctx->app_state || !ctx->current_graph || !result_data) {
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

		// Apply to node size (scale between 0.5 and 2.0)
		data->nodes[i].size = 0.5f + normalized * 1.5f;

		// Apply to glow (same as size-based glow)
		data->nodes[i].glow = normalized;
	}

	// Refresh renderer
	renderer_update_graph(renderer, data);

	printf("[apply_centrality_scores] Centrality applied\n");
}

void free_centrality_scores(void *result_data)
{
	if (result_data) {
		igraph_vector_destroy((igraph_vector_t *)result_data);
		free(result_data);
	}
}
