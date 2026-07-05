/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#define _GNU_SOURCE

#include "app_state.h"
#include "graph/wrappers_layout.h"
#include "ui/menu.h"
#include <igraph.h>
#include <igraph_constants.h>
#include <igraph_random.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// USE CURRENT POSITIONS AS SEED (TOGGLE)
// ============================================================================
void *compute_use_current_positions_as_seed(ExecutionContext *ctx)
{
	(void)ctx;
	return (void *)(uintptr_t)1;
}

void apply_use_current_positions_as_seed(ExecutionContext *ctx, void *result_data)
{
	(void)result_data;
	if (!ctx || !ctx->app_state)
		return;
	GraphData *data = &ctx->app_state->current_graph;
	data->use_as_seed = !data->use_as_seed;

	// Update menu label to reflect toggle state
	MenuNode *node = menu_find_node_by_command_id(ctx->app_state->app_ctx.root_menu, "use_current_positions_as_seed");
	if (node) {
		free((void *)node->label);
		node->label = strdup(data->use_as_seed ? "[x] Use current positions as seed" : "[ ] Use current positions as seed");
		if (node->command) {
			free((void *)node->command->display_name);
			node->command->display_name = strdup(node->label);
		}
	}
}

// ============================================================================
// SEED FILL HELPER
// ============================================================================
// Copies current layout positions into result matrix when use_as_seed is set.
// Returns the igraph_bool_t value to pass as the use_seed parameter.
igraph_bool_t layout_fill_seed(ExecutionContext *ctx, igraph_matrix_t *result, igraph_integer_t vcount)
{
	if (!ctx || !ctx->app_state)
		return false;
	GraphData *data = &ctx->app_state->current_graph;
	if (!data->use_as_seed)
		return false;
	igraph_integer_t layout_nrow = igraph_matrix_nrow(&data->current_layout);
	igraph_integer_t layout_ncol = igraph_matrix_ncol(&data->current_layout);
	if (layout_nrow != vcount || layout_ncol < 2)
		return false;
	for (igraph_integer_t i = 0; i < vcount; i++) {
		MATRIX(*result, i, 0) = MATRIX(data->current_layout, i, 0);
		MATRIX(*result, i, 1) = MATRIX(data->current_layout, i, 1);
		MATRIX(*result, i, 2) = (layout_ncol > 2) ? MATRIX(data->current_layout, i, 2) : 0.0;
	}
	return true;
}

// ============================================================================
// RANDOM UNIFORM SEED: RNG_UNIF(-1, 1)
// Replicates igraph_layout_random() initialization used by Graphopt, UMAP.
// ============================================================================
void *compute_seed_random_uniform(ExecutionContext *ctx)
{
	igraph_t *graph = &ctx->app_state->current_graph.g;
	if (!ctx->app_state->current_graph.graph_initialized) {
		fprintf(stderr, "[%s] Graph not initialized\n", __func__);
		return NULL;
	}

	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = IGRAPH_MALLOC(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 3) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		return NULL;
	}

	igraph_rng_t *rng = igraph_rng_default();
	for (igraph_integer_t i = 0; i < vcount; i++) {
		MATRIX(*result, i, 0) = igraph_rng_get_unif(rng, -1.0, 1.0);
		MATRIX(*result, i, 1) = igraph_rng_get_unif(rng, -1.0, 1.0);
		MATRIX(*result, i, 2) = igraph_rng_get_unif(rng, -1.0, 1.0);
	}

	return result;
}

// ============================================================================
// RANDOM BOUNDED SEED: RNG_UNIF(-sqrt(n)/2, sqrt(n)/2)
// Replicates igraph_i_layout_random_bounded() used by FR, KK, Yifan Hu 2D.
// ============================================================================
void *compute_seed_random_bounded(ExecutionContext *ctx)
{
	igraph_t *graph = &ctx->app_state->current_graph.g;
	if (!ctx->app_state->current_graph.graph_initialized) {
		fprintf(stderr, "[%s] Graph not initialized\n", __func__);
		return NULL;
	}

	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = IGRAPH_MALLOC(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 3) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		return NULL;
	}

	igraph_real_t half = sqrt((igraph_real_t)vcount) / 2.0;
	igraph_rng_t *rng = igraph_rng_default();
	for (igraph_integer_t i = 0; i < vcount; i++) {
		MATRIX(*result, i, 0) = igraph_rng_get_unif(rng, -half, half);
		MATRIX(*result, i, 1) = igraph_rng_get_unif(rng, -half, half);
		MATRIX(*result, i, 2) = igraph_rng_get_unif(rng, -half, half);
	}

	return result;
}

// ============================================================================
// NORMAL SEED: N(0, 0.01)
// Replicates igraph_rng_get_normal(rng, 0.0, 0.01) used by Barnes-Hut t-SNE.
// ============================================================================
void *compute_seed_random_normal(ExecutionContext *ctx)
{
	igraph_t *graph = &ctx->app_state->current_graph.g;
	if (!ctx->app_state->current_graph.graph_initialized) {
		fprintf(stderr, "[%s] Graph not initialized\n", __func__);
		return NULL;
	}

	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = IGRAPH_MALLOC(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 3) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		return NULL;
	}

	igraph_rng_t *rng = igraph_rng_default();
	for (igraph_integer_t i = 0; i < vcount; i++) {
		MATRIX(*result, i, 0) = igraph_rng_get_normal(rng, 0.0, 0.01);
		MATRIX(*result, i, 1) = igraph_rng_get_normal(rng, 0.0, 0.01);
		MATRIX(*result, i, 2) = igraph_rng_get_normal(rng, 0.0, 0.01);
	}

	return result;
}
