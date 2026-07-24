/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "graph/wrappers_structural.h"
#include "app_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *compute_graph_properties(ExecutionContext *ctx)
{
	igraph_t *graph = &ctx->app_state->current_graph.g;
	if (!ctx->app_state->current_graph.graph_initialized) {
		fprintf(stderr, "[%s] Graph not initialized\n", __func__);
		return NULL;
	}
	InfoCardData *data = (InfoCardData *)malloc(sizeof(InfoCardData));
	if (!data)
		return NULL;
	memset(data, 0, sizeof(InfoCardData));
	strncpy(data->title, "Graph Properties", sizeof(data->title) - 1);

	if (!graph || igraph_vcount(graph) == 0) {
		data->num_pairs = 0;
		return data;
	}

	igraph_bool_t directed = igraph_is_directed(graph);
	igraph_integer_t n = igraph_vcount(graph);
	igraph_integer_t e = igraph_ecount(graph);

	// 1. Vertices
	data->num_pairs = 0;
	strncpy(data->pairs[0].key, "Vertices", 31);
	snprintf(data->pairs[0].value, 63, "%d", (int)n);
	data->num_pairs++;

	// 2. Edges
	strncpy(data->pairs[1].key, "Edges", 31);
	snprintf(data->pairs[1].value, 63, "%d", (int)e);
	data->num_pairs++;

	// 3. Directed
	strncpy(data->pairs[2].key, "Directed", 31);
	strncpy(data->pairs[2].value, directed ? "Yes" : "No", 63);
	data->num_pairs++;

	// 4. DAG / Acyclic
	igraph_bool_t bool_res = false;
	strncpy(data->pairs[3].key, "DAG", 31);
	if (directed) {
		igraph_is_dag(graph, &bool_res);
		strncpy(data->pairs[3].value, bool_res ? "Yes" : "No", 63);
	} else {
		igraph_is_acyclic(graph, &bool_res);
		strncpy(data->pairs[3].value, bool_res ? "Yes" : "No", 63);
	}
	data->num_pairs++;

	// 5. Connected
	strncpy(data->pairs[4].key, "Connected", 31);
	if (directed) {
		igraph_is_connected(graph, &bool_res, IGRAPH_STRONG);
	} else {
		igraph_is_connected(graph, &bool_res, IGRAPH_WEAK);
	}
	strncpy(data->pairs[4].value, bool_res ? "Yes" : "No", 63);
	data->num_pairs++;

	// 6. Simple
	igraph_is_simple(graph, &bool_res, IGRAPH_DIRECTED);
	strncpy(data->pairs[5].key, "Simple", 31);
	strncpy(data->pairs[5].value, bool_res ? "Yes" : "No", 63);
	data->num_pairs++;

	// 7. Weighted
	strncpy(data->pairs[6].key, "Weighted", 31);
	strncpy(data->pairs[6].value, igraph_cattribute_has_attr(graph, IGRAPH_ATTRIBUTE_EDGE, "weight") ? "Yes" : "No", 63);
	data->num_pairs++;

	return data;
}

void *compute_igraph_density(ExecutionContext *ctx)
{
	igraph_t *graph = &ctx->app_state->current_graph.g;
	if (!ctx->app_state->current_graph.graph_initialized) {
		fprintf(stderr, "[%s] Graph not initialized\n", __func__);
		return NULL;
	}
	InfoCardData *data = (InfoCardData *)malloc(sizeof(InfoCardData));
	if (!data)
		return NULL;
	memset(data, 0, sizeof(InfoCardData));
	strncpy(data->title, "Density Results", sizeof(data->title) - 1);

	if (graph && igraph_vcount(graph) > 0) {
		igraph_real_t density = 0.0;

		if (igraph_density(graph, NULL, &density, false) != IGRAPH_SUCCESS) {
			free(data);
			return NULL;
		}

		data->num_pairs = 1;
		strncpy(data->pairs[0].key, "Density", 31);
		snprintf(data->pairs[0].value, 63, "%.4f", density);
	}

	return data;
}

void *compute_igraph_transitivity_undirected(ExecutionContext *ctx)
{
	igraph_t *graph = &ctx->app_state->current_graph.g;
	if (!ctx->app_state->current_graph.graph_initialized) {
		fprintf(stderr, "[%s] Graph not initialized\n", __func__);
		return NULL;
	}
	InfoCardData *data = (InfoCardData *)malloc(sizeof(InfoCardData));
	if (!data)
		return NULL;
	memset(data, 0, sizeof(InfoCardData));
	strncpy(data->title, "Transitivity Results", sizeof(data->title) - 1);

	if (graph && igraph_vcount(graph) > 0) {
		igraph_real_t trans = 0.0;

		if (igraph_transitivity_undirected(graph, &trans, IGRAPH_TRANSITIVITY_NAN) != IGRAPH_SUCCESS) {
			free(data);
			return NULL;
		}

		data->num_pairs = 1;
		strncpy(data->pairs[0].key, "Transitivity (undirected)", 31);
		snprintf(data->pairs[0].value, 63, "%.4f", trans);
	}

	return data;
}

void *compute_igraph_assortativity_degree(ExecutionContext *ctx)
{
	igraph_t *graph = &ctx->app_state->current_graph.g;
	if (!ctx->app_state->current_graph.graph_initialized) {
		fprintf(stderr, "[%s] Graph not initialized\n", __func__);
		return NULL;
	}
	InfoCardData *data = (InfoCardData *)malloc(sizeof(InfoCardData));
	if (!data)
		return NULL;
	memset(data, 0, sizeof(InfoCardData));
	strncpy(data->title, "Assortativity Results", sizeof(data->title) - 1);

	if (graph && igraph_vcount(graph) > 0) {
		igraph_real_t assort = 0.0;
		igraph_bool_t directed = igraph_is_directed(graph);

		if (igraph_assortativity_degree(graph, &assort, directed) != IGRAPH_SUCCESS) {
			free(data);
			return NULL;
		}

		data->num_pairs = 1;
		strncpy(data->pairs[0].key, "Degree Assortativity", 31);
		snprintf(data->pairs[0].value, 63, "%.4f", assort);
	}

	return data;
}
