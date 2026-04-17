#define _GNU_SOURCE

#include "graph/wrappers_layout.h"
#include <igraph.h>
#include <igraph_constants.h>
#include <stdio.h>
#include <stdlib.h>

// ============================================================================
// SUGIYAMA LAYOUT (Hierarchical DAG Layout)
// ============================================================================
// Reference: Sugiyama, K., Tagawa, S., and Toda, M.:
// "Methods for Visual Understanding of Hierarchical Systems."
// IEEE Transactions on Systems, Man and Cybernetics 11(2):109-125, 1981.
void *compute_igraph_layout_sugiyama(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = IGRAPH_MALLOC(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 3) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		return NULL;
	}

	/*
	 * igraph_layout_sugiyama() parameters:
	 * @param graph           The graph to layout (will convert cycles to DAG if needed)
	 * @param res             Output matrix (will be resized to vcount x 2)
	 * @param routing         Edge routing waypoints output (can be NULL)
	 * @param layers          Vertex layer assignments (NULL = auto-calculate via BFS)
	 * @param hgap            Horizontal gap between vertices in same layer
	 * @param vgap            Vertical gap between layers
	 * @param maxiter         Crossing minimization iterations (default: 100)
	 * @param weights         Edge weights for cycle breaking (lower = more likely to reverse)
	 *
	 * Works best for DAGs; automatically handles cycles by reversing edges
	 * Minimizes edge crossings while maintaining hierarchy
	 */
	igraph_matrix_list_t routing;
	igraph_matrix_list_init(&routing, 0);

	igraph_vector_int_t layers;
	igraph_vector_int_init(&layers, vcount);

	if (vcount > 0) {
		igraph_vector_int_t queue;
		igraph_vector_int_init(&queue, vcount);
		igraph_vector_bool_t visited;
		igraph_vector_bool_init(&visited, vcount);
		igraph_vector_bool_fill(&visited, 0);

		igraph_vector_int_push_back(&queue, 0);
		igraph_vector_bool_set(&visited, 0, 1);
		igraph_vector_int_set(&layers, 0, 0);

		for (igraph_integer_t read_idx = 0; read_idx < igraph_vector_int_size(&queue); read_idx++) {
			igraph_integer_t v = VECTOR(queue)[read_idx];
			igraph_vector_int_t neigh;
			igraph_vector_int_init(&neigh, 0);
			igraph_neighbors(graph, &neigh, v, IGRAPH_ALL, 0, 0);
			for (igraph_integer_t i = 0; i < igraph_vector_int_size(&neigh); i++) {
				igraph_integer_t u = VECTOR(neigh)[i];
				if (!igraph_vector_bool_get(&visited, u)) {
					igraph_vector_bool_set(&visited, u, 1);
					igraph_vector_int_set(&layers, u, VECTOR(layers)[v] + 1);
					igraph_vector_int_push_back(&queue, u);
				}
			}
			igraph_vector_int_destroy(&neigh);
		}

		igraph_vector_int_destroy(&queue);
		igraph_vector_bool_destroy(&visited);
	}

	igraph_error_t code = igraph_layout_sugiyama(graph, result,
												 &routing, // routing: store edge waypoints
												 &layers,  // layers: BFS-computed layers
												 1.0,	   // hgap: horizontal spacing
												 1.0,	   // vgap: vertical spacing
												 100,	   // maxiter: crossing minimization iterations
												 NULL	   // weights: unweighted
	);

	igraph_matrix_list_destroy(&routing);
	igraph_vector_int_destroy(&layers);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}
	return result;
}

// ============================================================================
// SUGIYAMA RADIAL LAYOUT
// ============================================================================
// Reference: Bachmaier, C. (2007):
// "A Radial Adaptation of the Sugiyama Framework for Visualizing Hierarchical Information."
// IEEE Transactions on Visualization and Computer Graphics 13(3):583-594.
void *compute_igraph_layout_sugiyama_radial(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = IGRAPH_MALLOC(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 3) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		return NULL;
	}

	/*
	 * igraph_layout_sugiyama_radial() parameters: identical to Sugiyama
	 * but maps horizontal coords to concentric circles
	 */
	igraph_matrix_list_t routing;
	igraph_matrix_list_init(&routing, 0);

	igraph_vector_int_t layers;
	igraph_vector_int_init(&layers, vcount);

	if (vcount > 0) {
		igraph_vector_int_t queue;
		igraph_vector_int_init(&queue, vcount);
		igraph_vector_bool_t visited;
		igraph_vector_bool_init(&visited, vcount);
		igraph_vector_bool_fill(&visited, 0);

		igraph_vector_int_push_back(&queue, 0);
		igraph_vector_bool_set(&visited, 0, 1);
		igraph_vector_int_set(&layers, 0, 0);

		for (igraph_integer_t read_idx = 0; read_idx < igraph_vector_int_size(&queue); read_idx++) {
			igraph_integer_t v = VECTOR(queue)[read_idx];
			igraph_vector_int_t neigh;
			igraph_vector_int_init(&neigh, 0);
			igraph_neighbors(graph, &neigh, v, IGRAPH_ALL, 0, 0);
			for (igraph_integer_t i = 0; i < igraph_vector_int_size(&neigh); i++) {
				igraph_integer_t u = VECTOR(neigh)[i];
				if (!igraph_vector_bool_get(&visited, u)) {
					igraph_vector_bool_set(&visited, u, 1);
					igraph_vector_int_set(&layers, u, VECTOR(layers)[v] + 1);
					igraph_vector_int_push_back(&queue, u);
				}
			}
			igraph_vector_int_destroy(&neigh);
		}

		igraph_vector_int_destroy(&queue);
		igraph_vector_bool_destroy(&visited);
	}

	igraph_error_t code = igraph_layout_sugiyama_radial(graph, result, &routing, &layers, 1.0, 1.0, 100, NULL);

	igraph_matrix_list_destroy(&routing);
	igraph_vector_int_destroy(&layers);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}
	return result;
}