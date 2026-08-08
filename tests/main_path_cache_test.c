/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "graph/main_path_cache.h"

#include <math.h>
#include <stdio.h>

static int store_method(igraph_t *graph, const char *method, double weight, double strength, const double basket_values[2], const double global_values[2])
{
	igraph_vector_t edge;
	igraph_vector_t basket;
	igraph_vector_t global;
	if (igraph_vector_init(&edge, 1) != IGRAPH_SUCCESS)
		return 1;
	if (igraph_vector_init_array(&basket, basket_values, 2) != IGRAPH_SUCCESS) {
		igraph_vector_destroy(&edge);
		return 1;
	}
	if (igraph_vector_init_array(&global, global_values, 2) != IGRAPH_SUCCESS) {
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
	snprintf(name, sizeof(name), "main-path-global-%s", method);
	failed |= SETVANV(graph, name, &global) != IGRAPH_SUCCESS;
	igraph_vector_destroy(&global);
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
	const double global_values[2] = {0, 1};
	int failures = store_method(&graph, "splc", 3.0, 1.5, basket_values, global_values) || store_method(&graph, "spe", 0.0, 4.5, basket_values, global_values) || store_method(&graph, "nppc", 10.0, 2.5, basket_values, global_values);
	MainPathSelectionResult *basket = main_path_cache_load_selection(&graph, "splc", "basket", 2, 1);
	MainPathSelectionResult *global = main_path_cache_load_selection(&graph, "splc", "global", 2, 1);
	MainPathSelectionResult *spe = main_path_cache_load_selection(&graph, "spe", "basket", 2, 1);
	MainPathSelectionResult *nppc = main_path_cache_load_selection(&graph, "nppc", "global", 2, 1);
	if (!basket || !global || !spe || !nppc || fabsf(basket->strengths[0] - 1.5f) > 1e-6f || fabsf(global->strengths[0] - 1.5f) > 1e-6f || fabsf(spe->strengths[0] - 4.5f) > 1e-6f || fabsf(nppc->strengths[0] - 2.5f) > 1e-6f || basket->flags[0] != 1 || global->flags[0] != 0)
		failures++;
	main_path_cache_selection_free(basket);
	main_path_cache_selection_free(global);
	main_path_cache_selection_free(spe);
	main_path_cache_selection_free(nppc);
	if (main_path_cache_load_selection(&graph, "splc", "basket", 2, 2) != NULL)
		failures++;
	igraph_cattribute_remove_v(&graph, "main-path-global-splc");
	if (main_path_cache_load_selection(&graph, "splc", "global", 2, 1) != NULL)
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
	if (igraph_cattribute_has_attr(&graph, IGRAPH_ATTRIBUTE_EDGE, "main-path-weight-spe") || igraph_cattribute_has_attr(&graph, IGRAPH_ATTRIBUTE_EDGE, "main-path-strength-spe") || igraph_cattribute_has_attr(&graph, IGRAPH_ATTRIBUTE_VERTEX, "main-path-basket-spe") || igraph_cattribute_has_attr(&graph, IGRAPH_ATTRIBUTE_VERTEX, "main-path-global-spe"))
		failures++;
	main_path_cache_remove_method(&graph, "nppc");
	if (igraph_cattribute_has_attr(&graph, IGRAPH_ATTRIBUTE_EDGE, "main-path-weight-nppc") || igraph_cattribute_has_attr(&graph, IGRAPH_ATTRIBUTE_EDGE, "main-path-strength-nppc") || igraph_cattribute_has_attr(&graph, IGRAPH_ATTRIBUTE_VERTEX, "main-path-basket-nppc") || igraph_cattribute_has_attr(&graph, IGRAPH_ATTRIBUTE_VERTEX, "main-path-global-nppc"))
		failures++;
	igraph_destroy(&graph);
	if (failures != 0) {
		fprintf(stderr, "main_path_cache_test: %d failures\n", failures);
		return 1;
	}
	printf("main_path_cache_test: all checks passed\n");
	return 0;
}
