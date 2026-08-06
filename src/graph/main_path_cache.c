/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "graph/main_path_cache.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

void main_path_cache_remove_method(igraph_t *graph, const char *method)
{
	char name[64];
	snprintf(name, sizeof(name), "main-path-weight-%s", method);
	igraph_cattribute_remove_e(graph, name);
	snprintf(name, sizeof(name), "main-path-strength-%s", method);
	igraph_cattribute_remove_e(graph, name);
	snprintf(name, sizeof(name), "main-path-basket-%s", method);
	igraph_cattribute_remove_v(graph, name);
	snprintf(name, sizeof(name), "main-path-path-%s", method);
	igraph_cattribute_remove_v(graph, name);
}

static bool main_path_cache_load_edge(const igraph_t *graph, const char *name, igraph_vector_t *values)
{
	return igraph_cattribute_has_attr(graph, IGRAPH_ATTRIBUTE_EDGE, name) && igraph_cattribute_EANV(graph, name, igraph_ess_all(IGRAPH_EDGEORDER_ID), values) == IGRAPH_SUCCESS;
}

static bool main_path_cache_load_vertex(const igraph_t *graph, const char *name, igraph_vector_t *values)
{
	return igraph_cattribute_has_attr(graph, IGRAPH_ATTRIBUTE_VERTEX, name) && igraph_cattribute_VANV(graph, name, igraph_vss_all(), values) == IGRAPH_SUCCESS;
}

MainPathSelectionResult *main_path_cache_load_selection(const igraph_t *graph, const char *method, const char *selection, uint32_t node_count, uint32_t edge_count)
{
	char weight_name[64];
	char strength_name[64];
	char flag_name[64];
	snprintf(weight_name, sizeof(weight_name), "main-path-weight-%s", method);
	snprintf(strength_name, sizeof(strength_name), "main-path-strength-%s", method);
	snprintf(flag_name, sizeof(flag_name), "main-path-%s-%s", selection, method);
	igraph_vector_t weights;
	igraph_vector_t strengths;
	igraph_vector_t flags;
	if (igraph_vector_init(&weights, 0) != IGRAPH_SUCCESS)
		return NULL;
	if (igraph_vector_init(&strengths, 0) != IGRAPH_SUCCESS) {
		igraph_vector_destroy(&weights);
		return NULL;
	}
	if (igraph_vector_init(&flags, 0) != IGRAPH_SUCCESS) {
		igraph_vector_destroy(&strengths);
		igraph_vector_destroy(&weights);
		return NULL;
	}
	if (!main_path_cache_load_edge(graph, weight_name, &weights) || igraph_vector_size(&weights) != edge_count) {
		fprintf(stderr, "[Main Path] Run Weighting first\n");
		goto fail;
	}
	for (uint32_t e = 0; e < edge_count; e++)
		if (!isfinite(VECTOR(weights)[e])) {
			fprintf(stderr, "[Main Path] %s weighting overflowed; use SPE\n", method);
			goto fail;
		}
	if (!main_path_cache_load_edge(graph, strength_name, &strengths) || igraph_vector_size(&strengths) != edge_count || !main_path_cache_load_vertex(graph, flag_name, &flags) || igraph_vector_size(&flags) != node_count) {
		fprintf(stderr, "[Main Path] Run Weighting first\n");
		goto fail;
	}
	for (uint32_t e = 0; e < edge_count; e++)
		if (!isfinite(VECTOR(strengths)[e]) || VECTOR(strengths)[e] < 0.0) {
			fprintf(stderr, "[Main Path] cached presentation strengths are invalid\n");
			goto fail;
		}
	MainPathSelectionResult *result = calloc(1, sizeof(*result));
	if (result) {
		result->strengths = malloc(sizeof(float) * (edge_count > 0 ? edge_count : 1));
		result->flags = malloc(sizeof(int) * (node_count > 0 ? node_count : 1));
	}
	if (!result || !result->strengths || !result->flags) {
		main_path_cache_selection_free(result);
		goto fail;
	}
	result->node_count = node_count;
	result->edge_count = edge_count;
	for (uint32_t e = 0; e < edge_count; e++)
		result->strengths[e] = (float)VECTOR(strengths)[e];
	for (uint32_t v = 0; v < node_count; v++)
		result->flags[v] = VECTOR(flags)[v] != 0.0;
	igraph_vector_destroy(&flags);
	igraph_vector_destroy(&strengths);
	igraph_vector_destroy(&weights);
	return result;

fail:
	igraph_vector_destroy(&flags);
	igraph_vector_destroy(&strengths);
	igraph_vector_destroy(&weights);
	return NULL;
}

void main_path_cache_selection_free(MainPathSelectionResult *result)
{
	if (!result)
		return;
	free(result->strengths);
	free(result->flags);
	free(result);
}
