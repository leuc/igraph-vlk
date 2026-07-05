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

// ============================================================================
// CIRCLE LAYOUT (3D with Z=0)
// ============================================================================
void *compute_igraph_layout_circle(ExecutionContext *ctx)
{
	igraph_t *graph = &ctx->app_state->current_graph.g;
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = IGRAPH_MALLOC(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 3) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		return NULL;
	}

	/*
	 * igraph_layout_circle() parameters:
	 * @param graph           The graph to layout
	 * @param res             Output matrix (will be resized to vcount x 2)
	 * @param order           Vertex order (igraph_vss_all = by vertex ID)
	 *
	 * Places vertices uniformly on a circle in vertex ID order
	 * Simple but effective for visualizing cyclic structures
	 */
	igraph_vs_t order;
	igraph_vs_all(&order);

	igraph_error_t code = igraph_layout_circle(graph, result, order);
	igraph_vs_destroy(&order);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}

	for (igraph_integer_t i = 0; i < vcount; i++) {
		igraph_matrix_set(result, i, 2, 0.0);
	}
	return result;
}

// ============================================================================
// CIRCLE LAYOUT (2D only)
// ============================================================================
void *compute_igraph_layout_circle_2d(ExecutionContext *ctx)
{
	igraph_t *graph = &ctx->app_state->current_graph.g;
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = IGRAPH_MALLOC(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 2) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		return NULL;
	}

	igraph_vs_t order;
	igraph_vs_all(&order);

	igraph_error_t code = igraph_layout_circle(graph, result, order);
	igraph_vs_destroy(&order);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}
	return result;
}

// ============================================================================
// STAR LAYOUT
// ============================================================================
void *compute_igraph_layout_star(ExecutionContext *ctx)
{
	igraph_t *graph = &ctx->app_state->current_graph.g;
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = IGRAPH_MALLOC(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 3) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		return NULL;
	}

	/*
	 * igraph_layout_star() parameters:
	 * @param graph           The graph (edges ignored)
	 * @param res             Output matrix (will be resized to vcount x 2)
	 * @param center         Vertex ID to place at center (we select highest degree)
	 * @param order          Vertex order (NULL = by vertex ID)
	 *
	 * Places center vertex at origin, others on circle around it
	 */
	igraph_integer_t center = 0;
	if (vcount > 1) {
		igraph_vector_int_t degrees;
		if (igraph_vector_int_init(&degrees, vcount) != IGRAPH_SUCCESS) {
			igraph_matrix_destroy(result);
			IGRAPH_FREE(result);
			return NULL;
		}
		igraph_degree(graph, &degrees, igraph_vss_all(), IGRAPH_ALL, IGRAPH_NO_LOOPS);

		igraph_real_t max_deg = -1;
		for (igraph_integer_t i = 0; i < vcount; i++) {
			if (VECTOR(degrees)[i] > max_deg) {
				max_deg = VECTOR(degrees)[i];
				center = i;
			}
		}
		igraph_vector_int_destroy(&degrees);
	}

	igraph_error_t code = igraph_layout_star(graph, result,
											 center, // center: highest degree vertex
											 NULL	 // order: by vertex ID
	);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}

	for (igraph_integer_t i = 0; i < vcount; i++) {
		igraph_matrix_set(result, i, 2, 0.0);
	}
	return result;
}

// ============================================================================
// GRID LAYOUT (3D)
// ============================================================================
void *compute_igraph_layout_grid_3d(ExecutionContext *ctx)
{
	igraph_t *graph = &ctx->app_state->current_graph.g;
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = IGRAPH_MALLOC(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 3) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		return NULL;
	}

	/*
	 * igraph_layout_grid_3d() parameters:
	 * @param graph           The graph to layout
	 * @param res             Output matrix (will be resized to vcount x 3)
	 * @param width           Number of vertices along x-axis (we use cube root)
	 * @param height          Number of vertices along y-axis (we use same as width)
	 *
	 * Calculates side = ceil(vcount^(1/3)) for cubic arrangement
	 */
	int side = (int)ceil(pow((double)vcount, 1.0 / 3.0));

	igraph_error_t code = igraph_layout_grid_3d(graph, result,
												(igraph_integer_t)side, // width: cube root of vcount
												(igraph_integer_t)side	// height: same as width
	);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}
	return result;
}

// ============================================================================
// GRID LAYOUT (2D)
// ============================================================================
void *compute_igraph_layout_grid(ExecutionContext *ctx)
{
	igraph_t *graph = &ctx->app_state->current_graph.g;
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = IGRAPH_MALLOC(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 2) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		return NULL;
	}

	/*
	 * igraph_layout_grid() parameters:
	 * @param graph           The graph to layout
	 * @param res             Output matrix (will be resized to vcount x 2)
	 * @param width           Number of columns (we use sqrt(vcount))
	 *
	 * Places vertices in a grid, filling row by row
	 */
	igraph_integer_t width = (igraph_integer_t)ceil(sqrt((double)vcount));

	igraph_error_t code = igraph_layout_grid(graph, result,
											 width // width: sqrt(vcount)
	);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}
	return result;
}

// ============================================================================
// SPHERE LAYOUT (Fibonacci Sphere)
// ============================================================================
// Reference: Saff, E.B. and Kuijlaars, A.B.J. (1997):
// "Distributing many points on a sphere."
// Mathematical Intelligencer 19(1):5-11.
void *compute_igraph_layout_sphere(ExecutionContext *ctx)
{
	igraph_t *graph = &ctx->app_state->current_graph.g;
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = IGRAPH_MALLOC(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 3) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		return NULL;
	}

	/*
	 * igraph_layout_sphere() parameters:
	 * @param graph           The graph to layout
	 * @param res             Output matrix (will be resized to vcount x 3)
	 *
	 * Uses golden spiral method for approximately equal spacing
	 * Consecutive vertex IDs are placed near each other
	 */
	igraph_error_t code = igraph_layout_sphere(graph, result);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}
	return result;
}

// ============================================================================
// RANDOM LAYOUT (3D)
// ============================================================================
void *compute_igraph_layout_random_3d(ExecutionContext *ctx)
{
	igraph_t *graph = &ctx->app_state->current_graph.g;
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = IGRAPH_MALLOC(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 3) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		return NULL;
	}

	/*
	 * igraph_layout_random_3d() parameters:
	 * @param graph           The graph to layout
	 * @param res             Output matrix (will be resized to vcount x 3)
	 *
	 * Places vertices uniformly at random in unit cube
	 * Useful as starting point for other layouts
	 */
	igraph_error_t code = igraph_layout_random_3d(graph, result);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}
	return result;
}

// ============================================================================
// RANDOM LAYOUT (2D)
// ============================================================================
void *compute_igraph_layout_random(ExecutionContext *ctx)
{
	igraph_t *graph = &ctx->app_state->current_graph.g;
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = IGRAPH_MALLOC(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 2) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		return NULL;
	}

	igraph_error_t code = igraph_layout_random(graph, result);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}
	return result;
}