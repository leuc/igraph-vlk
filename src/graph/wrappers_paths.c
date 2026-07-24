/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "graph/wrappers_paths.h"
#include "app_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *compute_igraph_diameter(ExecutionContext *ctx)
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
	strncpy(data->title, "Diameter Results", sizeof(data->title) - 1);

	if (graph && igraph_vcount(graph) > 0) {
		igraph_real_t diam = 0.0;
		igraph_vector_t weights;
		bool has_weights = graph_build_edge_weights(graph, &weights);
		igraph_diameter(graph, has_weights ? &weights : NULL, &diam, NULL, NULL, NULL, NULL, 0, 1);
		if (has_weights)
			igraph_vector_destroy(&weights);

		data->num_pairs = 1;
		strncpy(data->pairs[0].key, "Diameter", 31);
		if (has_weights)
			snprintf(data->pairs[0].value, 63, "%.4f", diam);
		else
			snprintf(data->pairs[0].value, 63, "%d", (int)diam);
	}

	return data;
}

void *compute_igraph_radius(ExecutionContext *ctx)
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
	strncpy(data->title, "Radius Results", sizeof(data->title) - 1);

	if (graph && igraph_vcount(graph) > 0) {
		igraph_real_t radius = 0.0;
		igraph_bool_t directed = igraph_is_directed(graph);
		igraph_neimode_t mode = directed ? IGRAPH_OUT : IGRAPH_ALL;

		igraph_vector_t weights;
		bool has_weights = graph_build_edge_weights(graph, &weights);
		igraph_error_t code = igraph_radius(graph, has_weights ? &weights : NULL, &radius, mode);
		if (has_weights)
			igraph_vector_destroy(&weights);

		if (code != IGRAPH_SUCCESS) {
			free(data);
			return NULL;
		}

		data->num_pairs = 1;
		strncpy(data->pairs[0].key, "Radius", 31);
		snprintf(data->pairs[0].value, 63, "%.2f", radius);
	}

	return data;
}

void *compute_igraph_average_path_length(ExecutionContext *ctx)
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
	strncpy(data->title, "Average Path Length Results", sizeof(data->title) - 1);

	if (graph && igraph_vcount(graph) > 0) {
		igraph_real_t apl = 0.0;
		igraph_bool_t directed = igraph_is_directed(graph);

		igraph_vector_t weights;
		bool has_weights = graph_build_edge_weights(graph, &weights);
		igraph_error_t code = igraph_average_path_length(graph, has_weights ? &weights : NULL, &apl, NULL, directed, true);
		if (has_weights)
			igraph_vector_destroy(&weights);

		if (code != IGRAPH_SUCCESS) {
			free(data);
			return NULL;
		}

		data->num_pairs = 1;
		strncpy(data->pairs[0].key, "Average Path Length", 31);
		snprintf(data->pairs[0].value, 63, "%.4f", apl);
	}

	return data;
}

void apply_info_card(ExecutionContext *ctx, void *result_data)
{
	if (!ctx || !result_data || !ctx->app_state)
		return;
	InfoCardData *data = (InfoCardData *)result_data;
	AppContext *app_ctx = &ctx->app_state->app_ctx;

	app_ctx->menu.info_card.is_visible = true;
	strncpy(app_ctx->menu.info_card.title, data->title, 63);
	app_ctx->menu.info_card.num_pairs = data->num_pairs;
	for (int i = 0; i < data->num_pairs; i++) {
		strncpy(app_ctx->menu.info_card.pairs[i].key, data->pairs[i].key, 31);
		strncpy(app_ctx->menu.info_card.pairs[i].value, data->pairs[i].value, 63);
	}
}

void info_card_free(void *result_data)
{
	if (result_data)
		free(result_data);
}
