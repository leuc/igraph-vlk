/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "graph/wrappers_centrality.h"
#include "app_state.h"
#include "graph/graph_core.h"
#include "interaction/state.h"
#include "ui/menu.h"
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

// CD index (citation disruption). Time window fixed to 5 years (1825 days),
// matching "CD5", the window most commonly reported in the citation-disruption
// literature (Funk & Owen-Smith 2017).
#define CD_INDEX_DATE_ATTR "date"
#define CD_INDEX_TIME_WINDOW_DAYS 1825

// Days since 1970-01-01 for a proleptic Gregorian y/m/d (Howard Hinnant's
// days_from_civil; plain integer arithmetic sidesteps struct tm/timegm's
// timezone and year-range pitfalls).
static long long cd_index_days_from_civil(int y, int m, int d)
{
	y -= m <= 2;
	long long era = (y >= 0 ? y : y - 399) / 400;
	unsigned yoe = (unsigned)(y - era * 400);
	unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + (unsigned)d - 1;
	unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + (long long)doe - 719468;
}

static bool cd_index_parse_date(const char *s, igraph_integer_t *out_days)
{
	int y, m, d;
	if (!s || sscanf(s, "%d-%d-%d", &y, &m, &d) != 3)
		return false;
	if (m < 1 || m > 12 || d < 1 || d > 31)
		return false;
	*out_days = (igraph_integer_t)cd_index_days_from_civil(y, m, d);
	return true;
}

void *compute_igraph_cd_index(ExecutionContext *ctx)
{
	igraph_t *graph = &ctx->app_state->current_graph.g;
	if (!ctx->app_state->current_graph.graph_initialized) {
		fprintf(stderr, "[%s] Graph not initialized\n", __func__);
		return NULL;
	}
	if (!graph || igraph_vcount(graph) == 0)
		return NULL;

	if (!igraph_is_directed(graph)) {
		fprintf(stderr, "CD index requires a directed graph\n");
		return NULL;
	}

	igraph_bool_t has_loops;
	igraph_has_loop(graph, &has_loops);
	if (has_loops) {
		fprintf(stderr, "CD index does not support self-loops (run Simplify first)\n");
		return NULL;
	}

	if (!igraph_cattribute_has_attr(graph, IGRAPH_ATTRIBUTE_VERTEX, CD_INDEX_DATE_ATTR)) {
		fprintf(stderr, "CD index requires a '%s' vertex attribute (ISO 8601, e.g. \"1999-07-05\")\n", CD_INDEX_DATE_ATTR);
		return NULL;
	}

	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_vector_int_t timestamps;
	if (igraph_vector_int_init(&timestamps, vcount) != IGRAPH_SUCCESS)
		return NULL;

	for (igraph_integer_t i = 0; i < vcount; i++) {
		const char *s = igraph_cattribute_VAS(graph, CD_INDEX_DATE_ATTR, i);
		igraph_integer_t days;
		if (!cd_index_parse_date(s, &days)) {
			fprintf(stderr, "CD index: vertex %lld has invalid '%s' value \"%s\" (expected ISO 8601, e.g. \"1999-07-05\")\n", (long long)i, CD_INDEX_DATE_ATTR, s ? s : "");
			igraph_vector_int_destroy(&timestamps);
			return NULL;
		}
		VECTOR(timestamps)[i] = days;
	}

	igraph_vector_t *result = IGRAPH_MALLOC(sizeof(igraph_vector_t));
	if (!result) {
		igraph_vector_int_destroy(&timestamps);
		return NULL;
	}
	if (igraph_vector_init(result, 0) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		igraph_vector_int_destroy(&timestamps);
		return NULL;
	}

	igraph_error_t code = igraph_cd_index(graph, &timestamps, result, NULL, NULL, igraph_vss_all(), CD_INDEX_TIME_WINDOW_DAYS);

	igraph_vector_int_destroy(&timestamps);

	if (code != IGRAPH_SUCCESS) {
		fprintf(stderr, "igraph_cd_index failed\n");
		igraph_vector_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}

	// Store the raw (signed, possibly NaN) score, but hand apply_centrality_scores
	// |score| instead: CD index is bipolar (-1 disruptive .. 0 .. +1 consolidating)
	// and NaN for vertices with no relevant future citations, neither of which
	// suits a size mapping — magnitude of disruption drives size regardless of
	// direction, NaN reads as "no signal" (smallest size), same as 0.
	// "cd-index-type" buckets the sign into a low-cardinality string attribute
	// (Node > Filter only works on those): "nan" for no relevant future
	// citations, else "disruptive"/"consolidating" by sign (0 counts as
	// consolidating).
	for (igraph_integer_t i = 0; i < vcount; i++) {
		igraph_real_t v = VECTOR(*result)[i];
		bool is_nan = isnan(v);
		if (SETVAN(graph, "cd-index", i, v) != IGRAPH_SUCCESS) {
			fprintf(stderr, "CD index: SETVAN failed for vertex %lld\n", (long long)i);
			break;
		}
		const char *type = is_nan ? "nan" : (v > 0.0 ? "disruptive" : "consolidating");
		if (SETVAS(graph, "cd-index-type", i, type) != IGRAPH_SUCCESS) {
			fprintf(stderr, "CD index: SETVAS failed for vertex %lld\n", (long long)i);
			break;
		}
		VECTOR(*result)[i] = is_nan ? 0.0 : fabs(v);
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

// ============================================================================
// Apply: CD index writes new 'cd-index'/'cd-index-type' vertex attributes as
// a side effect (unlike every other Rank command), so on top of the shared
// sizing logic this refreshes GraphData.filterable_attrs and repopulates the
// Node > Filter menu so 'cd-index-type' shows up there right away.
// ============================================================================
void apply_cd_index(ExecutionContext *ctx, void *result_data)
{
	apply_centrality_scores(ctx, result_data);
	if (!ctx || !ctx->app_state)
		return;

	AppState *state = ctx->app_state;
	graph_detect_filterable_attrs(&state->current_graph);
	menu_populate_attribute_filters(state->app_ctx.menu.root, &state->current_graph);
}

void centrality_scores_free(void *result_data)
{
	if (result_data) {
		igraph_vector_destroy((igraph_vector_t *)result_data);
		IGRAPH_FREE(result_data);
	}
}
