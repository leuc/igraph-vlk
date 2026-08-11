/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#define _GNU_SOURCE

#include "app_state.h"
#include "graph/wrappers_layout.h"
#include <igraph.h>
#include <igraph_constants.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

// BCGL LAYOUT (2D) - Binary Classification-Based Graph Layout
// Yan, Zhao & Yang (2022) — IEICE Trans. Inf. & Syst. E105.D(9), 1610-1619
// https://doi.org/10.1587/transinf.2021EDP7260
void *compute_igraph_layout_bcgl(ExecutionContext *ctx)
{
	igraph_t *graph = &ctx->app_state->current_graph.g;
	if (!ctx->app_state->current_graph.graph_initialized) {
		fprintf(stderr, "[%s] Graph not initialized\n", __func__);
		return NULL;
	}

	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = IGRAPH_MALLOC(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 2) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		return NULL;
	}

	/*
	 * igraph_layout_bcgl() parameters:
	 * @param graph           The graph to layout
	 * @param res             Output matrix (will be resized to vcount x 2)
	 * @param use_seed        If true, use initial positions from res
	 * @param niter           Number of SGD iterations (500)
	 * @param learning_rate   Step size for gradient descent (0.01)
	 * @param momentum        Momentum factor for SGD (0.9)
	 * @param lambda_compact  Compactness regularization (0.005)
	 * @param lambda_length   Edge length regularization (0.2)
	 * @param distribution    Probability distribution (Student's t)
	 *
	 * BCGL frames graph layout as a binary classification problem.
	 * Uses Student's t-distribution for edge probability model.
	 */
	igraph_error_t code = igraph_layout_bcgl(graph, result,
											 0,		// use_seed: false (random init)
											 200,	// niter
											 0.05,	// learning_rate
											 0.9,	// momentum
											 0.001, // lambda_compact
											 0.2,	// lambda_length
											 IGRAPH_LAYOUT_BCGL_DISTRIBUTION_STUDENT_T,
											 1); // use_bh: true (Barnes-Hut approximation)

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}
	return result;
}

// BCGL LAYOUT (3D) - Binary Classification-Based Graph Layout
// Yan, Zhao & Yang (2022) — IEICE Trans. Inf. & Syst. E105.D(9), 1610-1619
// https://doi.org/10.1587/transinf.2021EDP7260
void *compute_igraph_layout_bcgl_3d(ExecutionContext *ctx)
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

	/*
	 * igraph_layout_bcgl_3d() parameters:
	 * @param graph           The graph to layout
	 * @param res             Output matrix (will be resized to vcount x 3)
	 * @param use_seed        If true, use initial positions from res
	 * @param niter           Number of SGD iterations (500)
	 * @param learning_rate   Step size for gradient descent (0.01)
	 * @param momentum        Momentum factor for SGD (0.9)
	 * @param lambda_compact  Compactness regularization (0.005)
	 * @param lambda_length   Edge length regularization (0.2)
	 * @param distribution    Probability distribution (Student's t)
	 *
	 * 3D version of BCGL layout algorithm.
	 */
	igraph_error_t code = igraph_layout_bcgl_3d(graph, result,
												0,	  // use_seed: false (random init)
												250,  // niter
												0.05, // learning_rate
												0.9,  // momentum
												0.01, // lambda_compact
												0.01, // lambda_length
												IGRAPH_LAYOUT_BCGL_DISTRIBUTION_STUDENT_T,
												1); // use_bh: true (Barnes-Hut approximation)

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}
	return result;
}
