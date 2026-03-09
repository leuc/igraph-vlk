#include "graph/wrappers_paths.h"
#include "app_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *compute_igraph_diameter(igraph_t *graph)
{
	InfoCardData *data = (InfoCardData *)malloc(sizeof(InfoCardData));
	if (!data)
		return NULL;
	memset(data, 0, sizeof(InfoCardData));
	strncpy(data->title, "Diameter Results", sizeof(data->title) - 1);

	if (graph && igraph_vcount(graph) > 0) {
		igraph_real_t diam = 0.0;
		igraph_diameter(graph, NULL, &diam, NULL, NULL, NULL, NULL, 0, 1);

		data->num_pairs = 1;
		strncpy(data->pairs[0].key, "Diameter", 31);
		snprintf(data->pairs[0].value, 63, "%d", (int)diam);
	}

	return data;
}

void *compute_igraph_radius(igraph_t *graph)
{
	InfoCardData *data = (InfoCardData *)malloc(sizeof(InfoCardData));
	if (!data)
		return NULL;
	memset(data, 0, sizeof(InfoCardData));
	strncpy(data->title, "Radius Results", sizeof(data->title) - 1);

	if (graph && igraph_vcount(graph) > 0) {
		igraph_real_t radius = 0.0;
		igraph_bool_t directed = igraph_is_directed(graph);
		igraph_neimode_t mode = directed ? IGRAPH_OUT : IGRAPH_ALL;

		if (igraph_radius(graph, NULL, &radius, mode) != IGRAPH_SUCCESS) {
			free(data);
			return NULL;
		}

		data->num_pairs = 1;
		strncpy(data->pairs[0].key, "Radius", 31);
		snprintf(data->pairs[0].value, 63, "%.2f", radius);
	}

	return data;
}

void *compute_igraph_average_path_length(igraph_t *graph)
{
	InfoCardData *data = (InfoCardData *)malloc(sizeof(InfoCardData));
	if (!data)
		return NULL;
	memset(data, 0, sizeof(InfoCardData));
	strncpy(data->title, "Average Path Length Results", sizeof(data->title) - 1);

	if (graph && igraph_vcount(graph) > 0) {
		igraph_real_t apl = 0.0;
		igraph_bool_t directed = igraph_is_directed(graph);

		if (igraph_average_path_length(graph, NULL, &apl, NULL, directed, true) != IGRAPH_SUCCESS) {
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

	app_ctx->info_card.is_visible = true;
	strncpy(app_ctx->info_card.title, data->title, 63);
	app_ctx->info_card.num_pairs = data->num_pairs;
	for (int i = 0; i < data->num_pairs; i++) {
		strncpy(app_ctx->info_card.pairs[i].key, data->pairs[i].key, 31);
		strncpy(app_ctx->info_card.pairs[i].value, data->pairs[i].value, 63);
	}
}

void free_info_card(void *result_data)
{
	if (result_data)
		free(result_data);
}
