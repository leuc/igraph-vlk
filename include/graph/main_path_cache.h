/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GRAPH_MAIN_PATH_CACHE_H
#define GRAPH_MAIN_PATH_CACHE_H

#include <igraph.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct
{
	float *strengths;
	int *flags;
	uint32_t node_count;
	uint32_t edge_count;
} MainPathSelectionResult;

void main_path_cache_remove_method(igraph_t *graph, const char *method);
MainPathSelectionResult *main_path_cache_load_selection(const igraph_t *graph, const char *method, const char *selection, uint32_t node_count, uint32_t edge_count);
void main_path_cache_selection_free(MainPathSelectionResult *result);

// Loads and validates main-path-weight-{method}/main-path-strength-{method} into
// caller-initialized vectors (igraph_vector_init'd, sized 0). Shared by the cached
// Basket/Optimal Path loader above and by main_path_search's uncached selections, which
// both need the same "Run Weighting first" precondition and finite/non-negative checks.
bool main_path_cache_load_weight_and_strength(const igraph_t *graph, const char *method, uint32_t edge_count, igraph_vector_t *weights, igraph_vector_t *strengths);

#endif
