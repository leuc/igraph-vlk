#define _GNU_SOURCE

#include "graph/wrappers_layout.h"
#include <igraph.h>
#include <igraph_constants.h>
#include <stdio.h>
#include <stdlib.h>

// ============================================================================
// UMAP LAYOUT (2D)
// ============================================================================
void *compute_igraph_layout_umap(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = IGRAPH_MALLOC(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 2) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		return NULL;
	}

	/*
	 * igraph_layout_umap() parameters:
	 * @param graph           The graph to layout
	 * @param res             Output matrix (will be resized to vcount x 2)
	 * @param use_seed        Whether to use initial positions
	 * @param distances      Precomputed distances (NULL = compute from graph)
	 * @param min_dist        Minimum distance between points (default: 0.5)
	 *                       Higher = more spread, lower = more compact
	 * @param epochs         Training epochs (default: 500)
	 * @param distances_are_weights  If true, treat distances as edge weights
	 *
	 * Uniform Manifold Approximation and Projection
	 * Non-linear dimensionality reduction good for clusters
	 * EXPERIMENTAL in igraph
	 */
	igraph_real_t min_dist = 0.5;
	igraph_int_t epochs = 500;

	igraph_error_t code = igraph_layout_umap(graph, result,
											 0,		   // use_seed
											 NULL,	   // distances: compute from graph
											 min_dist, // min_dist: 0.5 default
											 epochs,   // epochs: 500 default
											 0		   // distances_are_weights: false
	);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}
	return result;
}

// ============================================================================
// UMAP LAYOUT (3D)
// ============================================================================
void *compute_igraph_layout_umap_3d(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = IGRAPH_MALLOC(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 3) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		return NULL;
	}

	igraph_real_t min_dist = 0.5;
	igraph_int_t epochs = 500;

	igraph_error_t code = igraph_layout_umap_3d(graph, result, 0, NULL, min_dist, epochs, 0);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}
	return result;
}