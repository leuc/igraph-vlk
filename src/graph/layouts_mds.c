/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#define _GNU_SOURCE

#include "graph/wrappers_layout.h"
#include <igraph.h>
#include <igraph_constants.h>
#include <stdio.h>
#include <stdlib.h>

// ============================================================================
// MDS LAYOUT (Multidimensional Scaling)
// ============================================================================
void *compute_igraph_layout_mds(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = IGRAPH_MALLOC(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 3) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		return NULL;
	}

	/*
	 * igraph_layout_mds() parameters:
	 * @param graph           The graph to layout
	 * @param res             Output matrix (will be resized to vcount x dim)
	 * @param dist            Distance matrix (computed via Dijkstra shortest paths)
	 * @param dim             Output dimensions (2 for 2D)
	 *
	 * Uses graph distances to preserve global structure
	 * Good for maintaining topological relationships
	 */
	igraph_matrix_t dist_matrix;
	if (igraph_matrix_init(&dist_matrix, vcount, vcount) != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}

	igraph_vs_t all_vs;
	igraph_vs_all(&all_vs);

	igraph_error_t dist_result = igraph_distances_dijkstra(graph, &dist_matrix, all_vs, all_vs, NULL, IGRAPH_ALL);
	igraph_vs_destroy(&all_vs);

	if (dist_result != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(&dist_matrix);
		igraph_matrix_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}

	igraph_error_t code = igraph_layout_mds(graph, result,
											&dist_matrix, // dist: all-pairs shortest path distances
											2			  // dim: 2D output
	);
	igraph_matrix_destroy(&dist_matrix);

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

void *compute_igraph_layout_mds_3d(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = IGRAPH_MALLOC(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 3) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		return NULL;
	}

	/*
	 * igraph_layout_mds() parameters:
	 * @param graph           The graph to layout
	 * @param res             Output matrix (will be resized to vcount x dim)
	 * @param dist            Distance matrix (computed via Dijkstra shortest paths)
	 * @param dim             Output dimensions (2 for 2D)
	 *
	 * Uses graph distances to preserve global structure
	 * Good for maintaining topological relationships
	 */
	igraph_matrix_t dist_matrix;
	if (igraph_matrix_init(&dist_matrix, vcount, vcount) != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}

	igraph_vs_t all_vs;
	igraph_vs_all(&all_vs);

	igraph_error_t dist_result = igraph_distances_dijkstra(graph, &dist_matrix, all_vs, all_vs, NULL, IGRAPH_ALL);
	igraph_vs_destroy(&all_vs);

	if (dist_result != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(&dist_matrix);
		igraph_matrix_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}

	igraph_error_t code = igraph_layout_mds(graph, result,
											&dist_matrix, // dist: all-pairs shortest path distances
											3			  // dim: 2D output
	);
	igraph_matrix_destroy(&dist_matrix);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}

	// for (igraph_integer_t i = 0; i < vcount; i++) {
	//	igraph_matrix_set(result, i, 2, 0.0);
	// }
	return result;
}