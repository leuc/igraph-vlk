/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "graph/wrappers_centrality.h"
#include "app_state.h"
#include "graph/graph_animation.h"
#include "graph/graph_color.h"
#include "graph/graph_core.h"
#include "interaction/state.h"
#include "ui/menu.h"
#include "vulkan/renderer.h"
#include <igraph.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

// Worker Functions
// Each returns igraph_vector_t* (centrality scores) on success, NULL on failure

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

	if (graph_cache_load_vertex_attr(graph, "degree", result))
		return result;

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
	graph_cache_store_vertex_attr(graph, "degree", result);
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

	if (graph_cache_load_vertex_attr(graph, "closeness", result))
		return result;

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
	graph_cache_store_vertex_attr(graph, "closeness", result);
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

	if (graph_cache_load_vertex_attr(graph, "betweenness", result))
		return result;

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
	graph_cache_store_vertex_attr(graph, "betweenness", result);
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

	if (graph_cache_load_vertex_attr(graph, "eigenvector-centrality", result))
		return result;

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
	graph_cache_store_vertex_attr(graph, "eigenvector-centrality", result);
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

	if (graph_cache_load_vertex_attr(graph, "pagerank", result))
		return result;

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
	graph_cache_store_vertex_attr(graph, "pagerank", result);
	return result;
}

// HITS: runs igraph_hub_and_authority_scores once for both scores and caches
// both as vertex attributes, so whichever of hub/authority is requested
// first computes both, and the other becomes a cache hit. Caller must
// igraph_vector_destroy() both out params on success.
static igraph_error_t hits_compute_both(igraph_t *graph, igraph_vector_t *hub_out, igraph_vector_t *authority_out)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	if (igraph_vector_init(hub_out, vcount) != IGRAPH_SUCCESS)
		return IGRAPH_ENOMEM;
	if (igraph_vector_init(authority_out, vcount) != IGRAPH_SUCCESS) {
		igraph_vector_destroy(hub_out);
		return IGRAPH_ENOMEM;
	}

	igraph_vector_t weights;
	bool has_weights = graph_build_edge_weights(graph, &weights);

	igraph_error_t code;
	if (!igraph_is_directed(graph)) {
		// For undirected graphs, HITS reduces to eigenvector centrality (hub == authority)
		code = igraph_eigenvector_centrality(graph, hub_out, NULL, IGRAPH_ALL, has_weights ? &weights : NULL, NULL);
		if (code == IGRAPH_SUCCESS)
			igraph_vector_update(authority_out, hub_out);
	} else {
		code = igraph_hub_and_authority_scores(graph, hub_out, authority_out, NULL, has_weights ? &weights : NULL, NULL);
	}

	if (has_weights)
		igraph_vector_destroy(&weights);

	if (code != IGRAPH_SUCCESS) {
		igraph_vector_destroy(hub_out);
		igraph_vector_destroy(authority_out);
		return code;
	}

	graph_cache_store_vertex_attr(graph, "hub", hub_out);
	graph_cache_store_vertex_attr(graph, "authority", authority_out);
	return IGRAPH_SUCCESS;
}

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

	if (graph_cache_load_vertex_attr(graph, "hub", result))
		return result;

	igraph_vector_t hub_scores, authority_scores;
	if (hits_compute_both(graph, &hub_scores, &authority_scores) != IGRAPH_SUCCESS) {
		igraph_vector_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}
	igraph_vector_update(result, &hub_scores);
	igraph_vector_destroy(&hub_scores);
	igraph_vector_destroy(&authority_scores);

	return result;
}

void *compute_igraph_authority_scores(ExecutionContext *ctx)
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

	if (graph_cache_load_vertex_attr(graph, "authority", result))
		return result;

	igraph_vector_t hub_scores, authority_scores;
	if (hits_compute_both(graph, &hub_scores, &authority_scores) != IGRAPH_SUCCESS) {
		igraph_vector_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}
	igraph_vector_update(result, &authority_scores);
	igraph_vector_destroy(&hub_scores);
	igraph_vector_destroy(&authority_scores);

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

	if (graph_cache_load_vertex_attr(graph, "harmonic-centrality", result))
		return result;

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
	graph_cache_store_vertex_attr(graph, "harmonic-centrality", result);
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

	if (graph_cache_load_vertex_attr(graph, "strength", result))
		return result;

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
	graph_cache_store_vertex_attr(graph, "strength", result);
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

	if (graph_cache_load_vertex_attr(graph, "coreness", result))
		return result;

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
	graph_cache_store_vertex_attr(graph, "coreness", result);
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

	if (graph_cache_load_vertex_attr(graph, "constraint", result))
		return result;

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
	graph_cache_store_vertex_attr(graph, "constraint", result);
	return result;
}

// CD index (citation disruption). Time window fixed to 6 months (182 days).
// The citation-disruption literature (Funk & Owen-Smith 2017) most commonly
// reports "CD5" (a 5-year window); 182 days is used here instead for a
// shorter-horizon signal.
#define CD_INDEX_DATE_ATTR "date"
#define CD_INDEX_TIME_WINDOW_DAYS 182

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

// CD index is bipolar (-1 disruptive .. 0 .. +1 consolidating) and NaN for
// vertices with no relevant future citations, neither of which suits a size
// mapping — magnitude of disruption drives size regardless of direction, NaN
// reads as "no signal" (smallest size), same as 0. Applied identically after
// a fresh compute and after loading the cached 'cd-index' attribute.
static void cd_index_apply_display_transform(igraph_vector_t *result, igraph_integer_t vcount)
{
	for (igraph_integer_t i = 0; i < vcount; i++) {
		igraph_real_t v = VECTOR(*result)[i];
		VECTOR(*result)[i] = isnan(v) ? 0.0 : fabs(v);
	}
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
	igraph_integer_t vcount = igraph_vcount(graph);

	igraph_vector_t *result = IGRAPH_MALLOC(sizeof(igraph_vector_t));
	if (!result)
		return NULL;
	if (igraph_vector_init(result, 0) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		return NULL;
	}

	if (graph_cache_load_vertex_attr(graph, "cd-index", result)) {
		cd_index_apply_display_transform(result, vcount);
		return result;
	}

	if (!igraph_is_directed(graph)) {
		fprintf(stderr, "CD index requires a directed graph\n");
		igraph_vector_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}

	igraph_bool_t has_loops;
	igraph_has_loop(graph, &has_loops);
	if (has_loops) {
		fprintf(stderr, "CD index does not support self-loops (run Simplify first)\n");
		igraph_vector_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}

	if (!igraph_cattribute_has_attr(graph, IGRAPH_ATTRIBUTE_VERTEX, CD_INDEX_DATE_ATTR)) {
		fprintf(stderr, "CD index requires a '%s' vertex attribute (ISO 8601, e.g. \"1999-07-05\")\n", CD_INDEX_DATE_ATTR);
		igraph_vector_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}

	igraph_vector_int_t timestamps;
	if (igraph_vector_int_init(&timestamps, vcount) != IGRAPH_SUCCESS) {
		igraph_vector_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}

	for (igraph_integer_t i = 0; i < vcount; i++) {
		const char *s = igraph_cattribute_VAS(graph, CD_INDEX_DATE_ATTR, i);
		igraph_integer_t days;
		if (!cd_index_parse_date(s, &days)) {
			fprintf(stderr, "CD index: vertex %lld has invalid '%s' value \"%s\" (expected ISO 8601, e.g. \"1999-07-05\")\n", (long long)i, CD_INDEX_DATE_ATTR, s ? s : "");
			igraph_vector_int_destroy(&timestamps);
			igraph_vector_destroy(result);
			IGRAPH_FREE(result);
			return NULL;
		}
		VECTOR(timestamps)[i] = days;
	}

	igraph_vector_t mcd_result;
	if (igraph_vector_init(&mcd_result, 0) != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(&timestamps);
		igraph_vector_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}

	igraph_error_t code = igraph_cd_index(graph, &timestamps, result, NULL, &mcd_result, igraph_vss_all(), CD_INDEX_TIME_WINDOW_DAYS);

	igraph_vector_int_destroy(&timestamps);

	if (code != IGRAPH_SUCCESS) {
		fprintf(stderr, "igraph_cd_index failed\n");
		igraph_vector_destroy(&mcd_result);
		igraph_vector_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}

	// "cd-index-type" buckets the sign into a low-cardinality string attribute
	// (Filter > Node only works on those): "nan" for no relevant future
	// citations, else "disruptive"/"consolidating" by sign (0 counts as
	// consolidating). Computed from the still-raw result, before the display
	// transform below overwrites it.
	for (igraph_integer_t i = 0; i < vcount; i++) {
		igraph_real_t v = VECTOR(*result)[i];
		const char *type = isnan(v) ? "nan" : (v > 0.0 ? "disruptive" : "consolidating");
		if (SETVAS(graph, "cd-index-type", i, type) != IGRAPH_SUCCESS) {
			fprintf(stderr, "CD index: SETVAS failed for vertex %lld\n", (long long)i);
			break;
		}
	}
	graph_cache_store_vertex_attr(graph, "cd-index", result);
	// mCD index (CD index * I-index, the impact-weighted variant from Funk &
	// Owen-Smith 2017 Eq. 4) — persisted for downstream/export use even
	// though nothing in the app displays it yet.
	graph_cache_store_vertex_attr(graph, "mcd-index", &mcd_result);
	igraph_vector_destroy(&mcd_result);
	cd_index_apply_display_transform(result, vcount);

	return result;
}

// Edge betweenness centrality
void *compute_igraph_edge_betweenness(ExecutionContext *ctx)
{
	igraph_t *graph = &ctx->app_state->current_graph.g;
	if (!ctx->app_state->current_graph.graph_initialized) {
		fprintf(stderr, "[%s] Graph not initialized\n", __func__);
		return NULL;
	}
	igraph_integer_t ecount = igraph_ecount(graph);
	igraph_vector_t *result = IGRAPH_MALLOC(sizeof(igraph_vector_t));
	if (igraph_vector_init(result, ecount) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		return NULL;
	}

	if (graph_cache_load_edge_attr(graph, "edge-betweenness", result))
		return result;

	igraph_vector_t weights;
	bool has_weights = graph_build_edge_weights(graph, &weights);

	igraph_error_t code = igraph_edge_betweenness(graph, has_weights ? &weights : NULL, result, igraph_ess_all(IGRAPH_EDGEORDER_ID), igraph_is_directed(graph) ? IGRAPH_DIRECTED : IGRAPH_UNDIRECTED, 1);

	if (has_weights)
		igraph_vector_destroy(&weights);

	if (code != IGRAPH_SUCCESS) {
		igraph_vector_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}
	graph_cache_store_edge_attr(graph, "edge-betweenness", result);
	return result;
}

// Convergence degree — writes 'convergence'/'convergence-degree' edge attributes as a
// side effect, then hands apply_edge_centrality_scores |degree| instead of the signed
// value: visual intensity should track how convergent/divergent an edge is, not its
// sign, same rationale as compute_igraph_cd_index for the vertex case.
// Magnitude drives visual intensity regardless of sign — applied identically
// after a fresh compute and after loading the cached 'convergence-degree' attribute.
static void convergence_degree_apply_display_transform(igraph_vector_t *result, igraph_integer_t ecount)
{
	for (igraph_integer_t i = 0; i < ecount; i++)
		VECTOR(*result)[i] = fabs(VECTOR(*result)[i]);
}

void *compute_igraph_convergence_degree(ExecutionContext *ctx)
{
	igraph_t *graph = &ctx->app_state->current_graph.g;
	if (!ctx->app_state->current_graph.graph_initialized) {
		fprintf(stderr, "[%s] Graph not initialized\n", __func__);
		return NULL;
	}
	igraph_integer_t ecount = igraph_ecount(graph);
	igraph_vector_t *result = IGRAPH_MALLOC(sizeof(igraph_vector_t));
	if (igraph_vector_init(result, ecount) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		return NULL;
	}

	if (graph_cache_load_edge_attr(graph, "convergence-degree", result)) {
		convergence_degree_apply_display_transform(result, ecount);
		return result;
	}

	if (igraph_convergence_degree(graph, result, NULL, NULL) != IGRAPH_SUCCESS) {
		fprintf(stderr, "igraph_convergence_degree failed\n");
		igraph_vector_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}

	// 'convergence' buckets the sign into a low-cardinality string attribute
	// (Filter > Edge only works on those). Computed from the still-raw result,
	// before the display transform below overwrites it.
	for (igraph_integer_t i = 0; i < ecount; i++) {
		igraph_real_t v = VECTOR(*result)[i];
		const char *label = (v > 0.0) ? "convergent" : (v < 0.0) ? "divergent" : "neutral";
		if (SETEAS(graph, "convergence", i, label) != IGRAPH_SUCCESS) {
			fprintf(stderr, "Convergence degree: SETEAS failed for edge %lld\n", (long long)i);
			break;
		}
	}
	graph_cache_store_edge_attr(graph, "convergence-degree", result);
	convergence_degree_apply_display_transform(result, ecount);

	return result;
}

// Edge trussness — highest k-truss each edge belongs to. igraph_trussness itself
// rejects multigraphs (and directed graphs with mutual edge pairs) with a clear
// error; just propagate it rather than duplicating that detection here.
void *compute_igraph_trussness(ExecutionContext *ctx)
{
	igraph_t *graph = &ctx->app_state->current_graph.g;
	if (!ctx->app_state->current_graph.graph_initialized) {
		fprintf(stderr, "[%s] Graph not initialized\n", __func__);
		return NULL;
	}
	igraph_integer_t ecount = igraph_ecount(graph);
	igraph_vector_t *result = IGRAPH_MALLOC(sizeof(igraph_vector_t));
	if (igraph_vector_init(result, ecount) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		return NULL;
	}

	if (graph_cache_load_edge_attr(graph, "trussness", result))
		return result;

	igraph_vector_int_t truss;
	if (igraph_vector_int_init(&truss, 0) != IGRAPH_SUCCESS) {
		igraph_vector_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}

	igraph_error_t code = igraph_trussness(graph, &truss);
	if (code != IGRAPH_SUCCESS) {
		fprintf(stderr, "igraph_trussness failed: %s\n", igraph_strerror(code));
		igraph_vector_int_destroy(&truss);
		igraph_vector_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}
	for (igraph_integer_t i = 0; i < ecount; i++)
		VECTOR(*result)[i] = (igraph_real_t)VECTOR(truss)[i];

	igraph_vector_int_destroy(&truss);
	graph_cache_store_edge_attr(graph, "trussness", result);
	return result;
}

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

	// Rank never wants leftover dimming/reveal state from a prior Follow command
	graph_reset_emphasis(data);
	graph_animation_clear(renderer, data);

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

// Apply: CD index writes new 'cd-index'/'cd-index-type' vertex attributes as
// a side effect (unlike every other Rank command), so on top of the shared
// sizing logic this refreshes GraphData.filterable_attrs and repopulates the
// Filter > Node menu so 'cd-index-type' shows up there right away.
void apply_cd_index(ExecutionContext *ctx, void *result_data)
{
	apply_centrality_scores(ctx, result_data);
	if (!ctx || !ctx->app_state)
		return;

	AppState *state = ctx->app_state;
	graph_detect_filterable_attrs(&state->current_graph);
	menu_populate_attribute_filters(&state->app_ctx.menu, &state->current_graph);
}

// Apply: edge-side twin of apply_centrality_scores — min/max-normalizes the
// score vector onto Edge.weight, which drives edge alpha in the renderer.
void apply_edge_centrality_scores(ExecutionContext *ctx, void *result_data)
{
	if (!ctx || !ctx->app_state || !result_data) {
		fprintf(stderr, "[apply_edge_centrality_scores] Error: Invalid parameters\n");
		return;
	}

	AppState *state = ctx->app_state;
	GraphData *data = &state->current_graph;
	Renderer *renderer = &state->renderer;
	igraph_vector_t *scores = (igraph_vector_t *)result_data;

	if (!data->graph_initialized) {
		fprintf(stderr, "[apply_edge_centrality_scores] Error: Graph not initialized\n");
		return;
	}

	if (igraph_vector_size(scores) != data->edge_count) {
		fprintf(stderr, "[apply_edge_centrality_scores] Error: Scores size doesn't match edge count\n");
		return;
	}

	// Rank never wants leftover dimming/reveal state from a prior Follow command
	graph_reset_emphasis(data);
	graph_animation_clear(renderer, data);

	// Find min/max for normalization
	igraph_real_t min_v, max_v;
	igraph_vector_minmax(scores, &min_v, &max_v);
	igraph_real_t range = max_v - min_v;

	// Map scores to the shared transient presentation-strength buffer.
	float *edge_values = malloc(sizeof(float) * data->edge_count);
	if (!edge_values)
		return;
	for (uint32_t i = 0; i < data->edge_count; i++) {
		igraph_real_t val = VECTOR(*scores)[i];
		float normalized = (range > 0) ? (float)((val - min_v) / range) : 1.0f;
		edge_values[i] = normalized;
	}
	GraphAnimationRequest request = {.edge_values = edge_values, .duration = 0.0f};
	graph_animation_play(renderer, data, &request);
	free(edge_values);

	// Refresh renderer
	renderer->needsAttributeUpload = VK_TRUE;
	renderer_update_graph(renderer, data);

	printf("[apply_edge_centrality_scores] Edge centrality applied\n");
}

// Apply: Convergence degree writes new 'convergence'/'convergence-degree' edge
// attributes as a side effect, so on top of the shared sizing logic this
// refreshes GraphData.filterable_edge_attrs and repopulates the Filter > Edge
// menu so 'convergence' shows up there right away.
void apply_convergence_degree(ExecutionContext *ctx, void *result_data)
{
	apply_edge_centrality_scores(ctx, result_data);
	if (!ctx || !ctx->app_state)
		return;

	AppState *state = ctx->app_state;
	graph_detect_filterable_edge_attrs(&state->current_graph);
	menu_populate_attribute_edge_filters(&state->app_ctx.menu, &state->current_graph);
}

void centrality_scores_free(void *result_data)
{
	if (result_data) {
		igraph_vector_destroy((igraph_vector_t *)result_data);
		IGRAPH_FREE(result_data);
	}
}

// Every attribute name a Rank command may have cached, so an in-place graph
// edit (Alter menu) can wipe them all and force the next Rank click to
// recompute rather than reapply now-stale values.
static const struct
{
	const char *name;
	bool is_edge;
} RANK_CACHED_ATTRS[] = {
	{"degree", false}, {"closeness", false}, {"betweenness", false}, {"eigenvector-centrality", false}, {"pagerank", false}, {"hits-hub", false}, {"harmonic-centrality", false}, {"strength", false}, {"constraint", false}, {"coreness", false}, {"cd-index", false}, {"cd-index-type", false}, {"edge-betweenness", true}, {"trussness", true}, {"convergence-degree", true}, {"convergence", true},
};

void centrality_clear_cached_attrs(igraph_t *graph)
{
	if (!graph)
		return;
	for (size_t i = 0; i < sizeof(RANK_CACHED_ATTRS) / sizeof(RANK_CACHED_ATTRS[0]); i++) {
		const char *name = RANK_CACHED_ATTRS[i].name;
		igraph_attribute_elemtype_t kind = RANK_CACHED_ATTRS[i].is_edge ? IGRAPH_ATTRIBUTE_EDGE : IGRAPH_ATTRIBUTE_VERTEX;
		if (!igraph_cattribute_has_attr(graph, kind, name))
			continue;
		if (RANK_CACHED_ATTRS[i].is_edge)
			DELEA(graph, name);
		else
			DELVA(graph, name);
	}
}
