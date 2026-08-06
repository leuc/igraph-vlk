/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "graph/main_path_cache.h"

#include <math.h>
#include <stdio.h>

static int store_method(igraph_t *graph, const char *method, double weight, double strength, const double basket_values[2], const double path_values[2])
{
	igraph_vector_t edge;
	igraph_vector_t basket;
	igraph_vector_t path;
	if (igraph_vector_init(&edge, 1) != IGRAPH_SUCCESS)
		return 1;
	if (igraph_vector_init_array(&basket, basket_values, 2) != IGRAPH_SUCCESS) {
		igraph_vector_destroy(&edge);
		return 1;
	}
	if (igraph_vector_init_array(&path, path_values, 2) != IGRAPH_SUCCESS) {
		igraph_vector_destroy(&basket);
		igraph_vector_destroy(&edge);
		return 1;
	}
	char name[64];
	VECTOR(edge)[0] = weight;
	snprintf(name, sizeof(name), "main-path-weight-%s", method);
	int failed = SETEANV(graph, name, &edge) != IGRAPH_SUCCESS;
	VECTOR(edge)[0] = strength;
	snprintf(name, sizeof(name), "main-path-strength-%s", method);
	failed |= SETEANV(graph, name, &edge) != IGRAPH_SUCCESS;
	snprintf(name, sizeof(name), "main-path-basket-%s", method);
	failed |= SETVANV(graph, name, &basket) != IGRAPH_SUCCESS;
	snprintf(name, sizeof(name), "main-path-path-%s", method);
	failed |= SETVANV(graph, name, &path) != IGRAPH_SUCCESS;
	igraph_vector_destroy(&path);
	igraph_vector_destroy(&basket);
	igraph_vector_destroy(&edge);
	return failed;
}

int main(void)
{
	igraph_set_attribute_table(&igraph_cattribute_table);
	igraph_t graph;
	if (igraph_small(&graph, 2, IGRAPH_DIRECTED, 0, 1, -1) != IGRAPH_SUCCESS)
		return 1;
	const double basket_values[2] = {1, 0};
	const double path_values[2] = {0, 1};
	int failures = store_method(&graph, "splc", 3.0, 1.5, basket_values, path_values) || store_method(&graph, "spe", 0.0, 4.5, basket_values, path_values);
	MainPathSelectionResult *basket = main_path_cache_load_selection(&graph, "splc", "basket", 2, 1);
	MainPathSelectionResult *path = main_path_cache_load_selection(&graph, "splc", "path", 2, 1);
	MainPathSelectionResult *spe = main_path_cache_load_selection(&graph, "spe", "basket", 2, 1);
	if (!basket || !path || !spe || fabsf(basket->strengths[0] - 1.5f) > 1e-6f || fabsf(path->strengths[0] - 1.5f) > 1e-6f || fabsf(spe->strengths[0] - 4.5f) > 1e-6f || basket->flags[0] != 1 || path->flags[0] != 0)
		failures++;
	main_path_cache_selection_free(basket);
	main_path_cache_selection_free(path);
	main_path_cache_selection_free(spe);
	if (main_path_cache_load_selection(&graph, "splc", "basket", 2, 2) != NULL)
		failures++;
	igraph_cattribute_remove_v(&graph, "main-path-path-splc");
	if (main_path_cache_load_selection(&graph, "splc", "path", 2, 1) != NULL)
		failures++;
	igraph_vector_t overflow;
	if (igraph_vector_init(&overflow, 1) != IGRAPH_SUCCESS) {
		igraph_destroy(&graph);
		return 1;
	}
	VECTOR(overflow)[0] = INFINITY;
	if (SETEANV(&graph, "main-path-weight-splc", &overflow) != IGRAPH_SUCCESS || main_path_cache_load_selection(&graph, "splc", "basket", 2, 1) != NULL)
		failures++;
	igraph_vector_destroy(&overflow);
	main_path_cache_remove_method(&graph, "spe");
	if (igraph_cattribute_has_attr(&graph, IGRAPH_ATTRIBUTE_EDGE, "main-path-weight-spe") || igraph_cattribute_has_attr(&graph, IGRAPH_ATTRIBUTE_EDGE, "main-path-strength-spe") || igraph_cattribute_has_attr(&graph, IGRAPH_ATTRIBUTE_VERTEX, "main-path-basket-spe") || igraph_cattribute_has_attr(&graph, IGRAPH_ATTRIBUTE_VERTEX, "main-path-path-spe"))
		failures++;
	igraph_destroy(&graph);
	if (failures != 0) {
		fprintf(stderr, "main_path_cache_test: %d failures\n", failures);
		return 1;
	}
	printf("main_path_cache_test: all checks passed\n");
	return 0;
}
