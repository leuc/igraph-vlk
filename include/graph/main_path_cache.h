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

#endif
