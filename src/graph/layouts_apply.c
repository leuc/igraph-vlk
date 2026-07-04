/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#define _GNU_SOURCE

#include "app_state.h"
#include "graph/wrappers_layout.h"
#include "interaction/state.h"
#include "vulkan/renderer.h"
#include <float.h>
#include <igraph.h>
#include <igraph_constants.h>
#include <igraph_progress.h>
#include <stdio.h>
#include <stdlib.h>

// ============================================================================
// CENTER LAYOUT + AUTO-SCALE (no rotation)
// ============================================================================
void layout_center_and_autoscale(igraph_matrix_t *mat)
{
	igraph_integer_t n = igraph_matrix_nrow(mat);
	igraph_integer_t dim = igraph_matrix_ncol(mat);
	if (n == 0 || dim < 2)
		return;

	double mean_x = 0.0, mean_y = 0.0, mean_z = 0.0;
	for (igraph_integer_t i = 0; i < n; i++) {
		mean_x += MATRIX(*mat, i, 0);
		mean_y += MATRIX(*mat, i, 1);
		if (dim > 2)
			mean_z += MATRIX(*mat, i, 2);
	}
	mean_x /= n;
	mean_y /= n;
	if (dim > 2)
		mean_z /= n;

	double max_radius_sq = 0.0;
	for (igraph_integer_t i = 0; i < n; i++) {
		double x = MATRIX(*mat, i, 0) - mean_x;
		double y = MATRIX(*mat, i, 1) - mean_y;
		double z = (dim > 2) ? (MATRIX(*mat, i, 2) - mean_z) : 0.0;
		MATRIX(*mat, i, 0) = x;
		MATRIX(*mat, i, 1) = y;
		if (dim > 2)
			MATRIX(*mat, i, 2) = z;
		double r2 = x * x + y * y + z * z;
		if (r2 > max_radius_sq)
			max_radius_sq = r2;
	}

	double max_radius = sqrt(max_radius_sq);
	if (max_radius < 1e-12)
		return;

	double target_radius = 3.0 * pow((double)n, 1.0 / 3.0);
	double scale = target_radius / max_radius;

	for (igraph_integer_t i = 0; i < n; i++) {
		MATRIX(*mat, i, 0) *= scale;
		MATRIX(*mat, i, 1) *= scale;
		if (dim > 2)
			MATRIX(*mat, i, 2) *= scale;
	}
}

// ============================================================================
// FREE LAYOUT DATA
// ============================================================================
void free_layout_matrix(void *result_data)
{
	if (result_data) {
		igraph_matrix_destroy((igraph_matrix_t *)result_data);
		IGRAPH_FREE(result_data);
	}
}

// ============================================================================
// APPLY LAYOUT TO GRAPH STATE
// ============================================================================
void apply_layout_matrix(ExecutionContext *ctx, void *result_data)
{
	if (!ctx || !ctx->app_state || !result_data) {
		fprintf(stderr, "[apply_layout_matrix] Error: Invalid parameters\n");
		return;
	}

	AppState *state = ctx->app_state;
	GraphData *data = &state->current_graph;
	Renderer *renderer = &state->renderer;
	igraph_matrix_t *layout = (igraph_matrix_t *)result_data;

	if (!data->graph_initialized) {
		fprintf(stderr, "[apply_layout_matrix] Error: Graph not initialized\n");
		return;
	}

	if (layout->nrow != data->node_count || layout->ncol < 2) {
		fprintf(stderr, "[apply_layout_matrix] Error: Layout dimensions don't match node count\n");
		return;
	}

	igraph_matrix_destroy(&data->current_layout);
	igraph_matrix_init_copy(&data->current_layout, layout);

	layout_center_and_autoscale(&data->current_layout);

	if (data->nodes) {
		for (igraph_integer_t i = 0; i < data->node_count; i++) {
			data->nodes[i].position[0] = (float)MATRIX(data->current_layout, i, 0);
			data->nodes[i].position[1] = (float)MATRIX(data->current_layout, i, 1);
			data->nodes[i].position[2] = (igraph_matrix_ncol(&data->current_layout) > 2) ? (float)MATRIX(data->current_layout, i, 2) : 0.0f;
		}

		float min_x = FLT_MAX, max_x = -FLT_MAX;
		float min_y = FLT_MAX, max_y = -FLT_MAX;
		float min_z = FLT_MAX, max_z = -FLT_MAX;
		for (igraph_integer_t i = 0; i < data->node_count; i++) {
			float x = data->nodes[i].position[0];
			float y = data->nodes[i].position[1];
			float z = data->nodes[i].position[2];
			if (x < min_x)
				min_x = x;
			if (x > max_x)
				max_x = x;
			if (y < min_y)
				min_y = y;
			if (y > max_y)
				max_y = y;
			if (z < min_z)
				min_z = z;
			if (z > max_z)
				max_z = z;
		}
		// printf("[Layout Bounds] X: [%.3f, %.3f] Y: [%.3f, %.3f] Z: [%.3f, %.3f]\n", min_x, max_x, min_y, max_y, min_z, max_z);
	}

	renderer_update_graph(renderer, data);
	// printf("[apply_layout_matrix] Layout applied and renderer refreshed\n");
}
