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
	// Per-node connected-component id among the surviving ties, or -1. NULL for every variant
	// except main_path_search_valued_network (Hummon & Carley 1993) -- apply_main_path_selection
	// colors flagged nodes by this id when present, leaving every other selection's plain-dim-only
	// behavior unchanged.
	int *component_id;
	uint32_t node_count;
	uint32_t edge_count;
	// Identity of the weighting method/selection that produced this result, e.g. "splc"/"local".
	// Always a string literal owned by the caller (never freed) -- NULL for callers that don't
	// need main-path-{selection}-{method} persisted (there are none today; every compute_main_path_*
	// wrapper sets both, so apply_main_path_selection can persist consistently for every
	// selection, not just the GPU-cached Basket/Global Path ones).
	const char *method;
	const char *selection;
} MainPathSelectionResult;

void main_path_cache_remove_method(igraph_t *graph, const char *method);
MainPathSelectionResult *main_path_cache_load_selection(const igraph_t *graph, const char *method, const char *selection, uint32_t node_count, uint32_t edge_count);
void main_path_cache_selection_free(MainPathSelectionResult *result);

// Loads and validates main-path-weight-{method}/main-path-strength-{method} into
// caller-initialized vectors (igraph_vector_init'd, sized 0). Shared by the cached
// Basket/Global Path loader above and by main_path_search's uncached selections, which
// both need the same "Run Weighting first" precondition and finite/non-negative checks.
bool main_path_cache_load_weight_and_strength(const igraph_t *graph, const char *method, uint32_t edge_count, igraph_vector_t *weights, igraph_vector_t *strengths);

#endif
