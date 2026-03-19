#define _GNU_SOURCE

#include "graph/wrappers_layout.h"
#include "app_state.h"
#include "interaction/state.h"
#include "vulkan/renderer.h"
#include <float.h>
#include <igraph.h>
#include <igraph_constants.h>
#include <igraph_progress.h>
#include <math.h>
#include <omp.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/**
 * ============================================================================
 * LAYOUT WRAPPER FUNCTIONS
 * ============================================================================
 * Each wrapper provides intelligent defaults based on graph properties.
 * Parameters are documented with their purpose and typical values.
 *
 * References:
 * - Fruchterman & Reingold (1991): Graph Drawing by Force-directed Placement
 * - Kamada & Kawai (1989): An Algorithm for Drawing General Undirected Graphs
 * - Davidson & Harel (1996): Drawing Graphs Nicely Using Simulated Annealing
 * - Reingold & Tilford (1981): Tidier drawing of trees
 * - Sugiyama et al. (1981): Methods for Visual Understanding of Hierarchical Systems
 * - GEM: Frick, Ludwig, Mehldau (1994): A Fast Adaptive Layout Algorithm
 * - ForceAtlas2: Jacomy et al. (2014): ForceAtlas2, a Continuous Graph Layout
 * - Yifan Hu (2005): Efficient and High-Quality Force-Directed Graph Drawing
 * - LGL: Large Graph Layout algorithm
 * ============================================================================
 */

// ============================================================================
// FRUCHTERMAN-REINGOLD LAYOUT (2D)
// ============================================================================
// Reference: Fruchterman, T.M.J. and Reingold, E.M.:
// "Graph Drawing by Force-directed Placement."
// Software -- Practice and Experience, 21/11, 1129--1164, 1991.
// https://doi.org/10.1002/spe.4380211102
//
// Force-directed layout using attractive forces between connected vertices
// and repulsive forces between all vertex pairs.
void *compute_igraph_layout_fruchterman_reingold(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = IGRAPH_MALLOC(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 2) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		return NULL;
	}

	/*
	 * igraph_layout_fruchterman_reingold() parameters:
	 * @param graph           The graph to layout
	 * @param res             Output matrix (will be resized)
	 * @param use_seed        Whether to use initial positions (0 = random start)
	 * @param niter           Number of iterations (default: 500) - we scale with sqrt(vcount)
	 * @param start_temp      Initial temperature - max displacement per iteration (default: vcount)
	 *                       We use 10 + sqrt(vcount) to allow exploration for larger graphs
	 * @param grid            Grid optimization: IGRAPH_LAYOUT_AUTOGRID uses grid for >1000 vertices
	 *                       Recommended by igraph docs for large graphs
	 * @param weights         Edge weights (NULL = unit weight, edges with higher weight
	 *                       are pulled closer together)
	 * @param minx/maxx       Bounding box constraints (NULL = unconstrained)
	 * @param miny/maxy       Bounding box constraints (NULL = unconstrained)
	 */
	igraph_real_t start_temp = (igraph_real_t)(10.0 + sqrt((double)vcount));
	igraph_int_t niter = (igraph_int_t)(300 + 10 * sqrt((double)vcount));
	igraph_layout_grid_t grid = (vcount > 1000) ? IGRAPH_LAYOUT_GRID : IGRAPH_LAYOUT_AUTOGRID;

	igraph_error_t code = igraph_layout_fruchterman_reingold(graph, result,
															 0,			 // use_seed: start from random positions
															 niter,		 // niter: more iterations for larger graphs
															 start_temp, // start_temp: scaled to graph size
															 grid,		 // grid: use grid optimization for large graphs
															 NULL,		 // weights: NULL = all edges have weight 1
															 NULL,		 // minx: no constraint
															 NULL,		 // maxx: no constraint
															 NULL,		 // miny: no constraint
															 NULL		 // maxy: no constraint
	);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}
	return result;
}

// ============================================================================
// FRUCHTERMAN-REINGOLD LAYOUT (3D)
// ============================================================================
void *compute_igraph_layout_fruchterman_reingold_3d(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = IGRAPH_MALLOC(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 3) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		return NULL;
	}

	/*
	 * igraph_layout_fruchterman_reingold_3d() parameters:
	 * @param graph           The graph to layout
	 * @param res             Output matrix (will be resized to vcount x 3)
	 * @param use_seed        Whether to use initial positions (0 = random start)
	 * @param niter           Number of iterations - we scale with sqrt(vcount)
	 * @param start_temp      Initial temperature - max displacement per axis
	 * @param weights         Edge weights (NULL = unit weight)
	 * @param minx/maxx/miny/maxy/minz/maxz  Bounding box constraints
	 */
	igraph_real_t start_temp = (igraph_real_t)(10.0 + sqrt((double)vcount));
	igraph_int_t niter = (igraph_int_t)(300 + 10 * sqrt((double)vcount));

	igraph_error_t code = igraph_layout_fruchterman_reingold_3d(graph, result,
																0,								   // use_seed
																niter,							   // niter: scaled to graph size
																start_temp,						   // start_temp: allows exploration
																NULL,							   // weights: NULL = unit weight
																NULL, NULL, NULL, NULL, NULL, NULL // bounds: unconstrained
	);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}
	return result;
}

// ============================================================================
// KAMADA-KAWAI LAYOUT (2D)
// ============================================================================
// Reference: Kamada, T. and Kawai, S.:
// "An Algorithm for Drawing General Undirected Graphs."
// Information Processing Letters, 31/1, 7--15, 1989.
// https://doi.org/10.1016/0020-0190(89)90102-6
void *compute_igraph_layout_kamada_kawai(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = IGRAPH_MALLOC(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 2) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		return NULL;
	}

	/*
	 * igraph_layout_kamada_kawai() parameters:
	 * @param graph           The graph to layout
	 * @param res             Output matrix (will be resized to vcount x 2)
	 * @param use_seed        Whether to use initial positions (0 = circle layout start)
	 * @param maxiter        Maximum iterations (at least 10*vcount recommended)
	 *                       We use vcount*10 as recommended in igraph docs
	 * @param epsilon        Convergence threshold (0.0 = run all iterations)
	 * @param kkconst        Kamada-Kawai spring constant K (typical: vcount)
	 *                       Higher = stronger springs between all vertex pairs
	 * @param weights        Edge weights for shortest path calculation
	 *                       Higher weights = longer rest lengths = vertices farther apart
	 * @param minx/maxx/miny/maxy  Bounding box constraints
	 *
	 * Note: This algorithm requires O(|V|^2) memory and is unsuitable for large graphs
	 *       Weights are used as LENGTHS in shortest path calculation
	 */
	igraph_int_t maxiter = vcount * 10;
	igraph_real_t epsilon = 0.0;
	igraph_real_t kkconst = (igraph_real_t)vcount;

	igraph_error_t code = igraph_layout_kamada_kawai(graph, result,
													 0,						// use_seed: start with circle layout
													 maxiter,				// maxiter: 10*vcount as per docs
													 epsilon,				// epsilon: run all iterations
													 kkconst,				// kkconst: typical value = vcount
													 NULL,					// weights: NULL = unit weights in distance calc
													 NULL, NULL, NULL, NULL // bounds: unconstrained
	);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}
	return result;
}

// ============================================================================
// KAMADA-KAWAI LAYOUT (3D)
// ============================================================================
void *compute_igraph_layout_kamada_kawai_3d(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = IGRAPH_MALLOC(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 3) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		return NULL;
	}

	/*
	 * igraph_layout_kamada_kawai_3d() parameters: same as 2D version but with:
	 * @param res             Output matrix (will be resized to vcount x 3)
	 * @param minz/maxz       Additional Z-axis bounds
	 */
	igraph_int_t maxiter = vcount * 10;
	igraph_real_t epsilon = 0.0;
	igraph_real_t kkconst = (igraph_real_t)vcount;

	igraph_error_t code = igraph_layout_kamada_kawai_3d(graph, result,
														0,								   // use_seed
														maxiter,						   // maxiter: 10*vcount
														epsilon,						   // epsilon
														kkconst,						   // kkconst: vcount
														NULL,							   // weights
														NULL, NULL, NULL, NULL, NULL, NULL // bounds
	);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}
	return result;
}

// ============================================================================
// DRL LAYOUT (2D)
// ============================================================================
void *compute_igraph_layout_drl(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = IGRAPH_MALLOC(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 2) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		return NULL;
	}

	/*
	 * igraph_layout_drl() parameters:
	 * @param graph           The graph to layout
	 * @param res             Output matrix (will be resized to vcount x 2)
	 * @param use_seed        Whether to use initial positions
	 * @param options         DrL configuration struct (initialized with template)
	 *                       IGRAPH_LAYOUT_DRL_DEFAULT: good balance for general graphs
	 *                       Other options: COARSEN, COARSEST, REFINE, FINAL
	 * @param weights         Edge weights (NULL = unweighted)
	 *
	 * DrL uses a multi-phase approach: liquid -> expansion -> cooldown -> crunch -> simmer
	 * Excellent for medium to large graphs with natural clusters
	 */
	igraph_layout_drl_options_t options;
	igraph_layout_drl_options_init(&options, IGRAPH_LAYOUT_DRL_DEFAULT);

	igraph_error_t code = igraph_layout_drl(graph, result,
											0,		  // use_seed
											&options, // options: default template
											NULL	  // weights: NULL = unweighted
	);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}
	return result;
}

// ============================================================================
// DRL LAYOUT (3D)
// ============================================================================
void *compute_igraph_layout_drl_3d(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = IGRAPH_MALLOC(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 3) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		return NULL;
	}

	/*
	 * igraph_layout_drl_3d() parameters: same as 2D but res is vcount x 3
	 */
	igraph_layout_drl_options_t options;
	igraph_layout_drl_options_init(&options, IGRAPH_LAYOUT_DRL_DEFAULT);

	igraph_error_t code = igraph_layout_drl_3d(graph, result,
											   0,		 // use_seed
											   &options, // options
											   NULL		 // weights
	);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}
	return result;
}

// ============================================================================
// DAVIDSON-HAREL LAYOUT
// ============================================================================
// Reference: Davidson, R. and Harel, D.:
// "Drawing Graphs Nicely Using Simulated Annealing."
// ACM Transactions on Graphics 15(4), pp. 301-331, 1996.
// https://doi.org/10.1145/234535.234538
void *compute_igraph_layout_davidson_harel(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_integer_t ecount = igraph_ecount(graph);
	igraph_matrix_t *result = IGRAPH_MALLOC(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 3) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		return NULL;
	}

	/*
	 * igraph_layout_davidson_harel() parameters:
	 * @param graph           The graph to layout (edge directions ignored)
	 * @param res             Output matrix (will be resized to vcount x 2)
	 * @param use_seed        Whether to use initial positions
	 * @param maxiter         Annealing iterations (reasonable: 10 for small graphs)
	 * @param fineiter        Fine-tuning iterations (reasonable: max(10, log2(n)))
	 * @param cool_fact       Cooling factor (reasonable: 0.75)
	 *                       Each iteration multiplies temp by cool_fact
	 * @param weight_node_dist    Energy weight: node-node repulsion (reasonable: 1.0)
	 * @param weight_border      Energy weight: border attraction (reasonable: 0.5)
	 * @param weight_edge_lengths Energy weight: edge length minimization
	 *                           (reasonable: density/10 - less important for sparse graphs)
	 * @param weight_edge_crossings Energy weight: edge crossing minimization
	 *                           (reasonable: 1-sqrt(density) - critical for dense graphs)
	 * @param weight_node_edge_dist Energy weight: node-edge distance
	 *                           (reasonable: (1-density)/5 - less important for dense)
	 *
	 * The original paper did not disclose parameter values; above are from igraph docs
	 */
	igraph_real_t density = 0.0;
	if (vcount > 1) {
		density = (2.0 * ecount) / ((igraph_real_t)(vcount * (vcount - 1)));
	}

	igraph_int_t maxiter = 10;
	igraph_int_t fineiter = (igraph_int_t)fmax(10.0, log2((double)vcount));
	igraph_real_t coolfact = 0.75;
	igraph_real_t w_dist = 1.0;
	igraph_real_t w_border = 0.5;
	igraph_real_t w_edge_len = density / 10.0;
	igraph_real_t w_edge_cross = 1.0 - sqrt(density);
	igraph_real_t w_node_edge = (1.0 - density) / 5.0;

	igraph_error_t code = igraph_layout_davidson_harel(graph, result,
													   0,			 // use_seed
													   maxiter,		 // maxiter: 10 annealing iterations
													   fineiter,	 // fineiter: log2(vcount) minimum 10
													   coolfact,	 // coolfact: 0.75 as recommended
													   w_dist,		 // w_dist: node-node repulsion
													   w_border,	 // w_border: border distance
													   w_edge_len,	 // w_edge_len: scaled by density
													   w_edge_cross, // w_edge_cross: 1-sqrt(density) per docs
													   w_node_edge	 // w_node_edge: (1-density)/5 per docs
	);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}
	return result;
}

// ============================================================================
// REINGOLD-TILFORD TREE LAYOUT
// ============================================================================
// Reference: Reingold, E. and Tilford, J.:
// "Tidier drawing of trees."
// IEEE Trans. Softw. Eng., SE-7(2):223-228, 1981.
// https://doi.org/10.1109/TSE.1981.234519
void *compute_igraph_layout_reingold_tilford(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = IGRAPH_MALLOC(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 3) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		return NULL;
	}

	/*
	 * igraph_layout_reingold_tilford() parameters:
	 * @param graph           The graph to layout (will find spanning tree if not tree)
	 * @param res             Output matrix (will be resized to vcount x 2)
	 * @param mode            Edge traversal mode:
	 *                       IGRAPH_OUT: outgoing edges (out-tree)
	 *                       IGRAPH_IN: incoming edges (in-tree)
	 *                       IGRAPH_ALL: all edges (treat as undirected)
	 * @param roots          Root vertices (NULL = auto-select using heuristic)
	 * @param rootlevel      Level offsets for multiple roots (NULL = all level 0)
	 *
	 * Uses BFS to determine levels, centers parents above children
	 * igraph_roots_for_tree_layout() auto-selects roots based on degree
	 */
	igraph_neimode_t mode = IGRAPH_ALL;

	igraph_vector_int_t roots;
	igraph_vector_int_init(&roots, 0);
	igraph_roots_for_tree_layout(graph, mode, &roots, IGRAPH_ROOT_CHOICE_DEGREE);

	igraph_error_t code = igraph_layout_reingold_tilford(graph, result,
														 mode,						 // mode: treat as undirected
														 vcount > 0 ? &roots : NULL, // roots: auto-selected
														 NULL						 // rootlevel: all start at 0
	);

	igraph_vector_int_destroy(&roots);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}

	if (result->ncol < 3) {
		igraph_matrix_resize(result, vcount, 3);
		for (igraph_integer_t i = 0; i < vcount; i++) {
			igraph_matrix_set(result, i, 2, 0.0);
		}
	}
	return result;
}

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

// ============================================================================
// CIRCLE LAYOUT (3D with Z=0)
// ============================================================================
void *compute_igraph_layout_circle(igraph_t *graph)
{
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
void *compute_igraph_layout_circle_2d(igraph_t *graph)
{
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
void *compute_igraph_layout_star(igraph_t *graph)
{
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
		igraph_vector_int_init(&degrees, vcount);
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
void *compute_igraph_layout_grid_3d(igraph_t *graph)
{
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
void *compute_igraph_layout_grid(igraph_t *graph)
{
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
void *compute_igraph_layout_sphere(igraph_t *graph)
{
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
void *compute_igraph_layout_random_3d(igraph_t *graph)
{
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
void *compute_igraph_layout_random(igraph_t *graph)
{
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

// ============================================================================
// BIPARTITE LAYOUT (3D)
// ============================================================================
void *compute_igraph_layout_bipartite(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = IGRAPH_MALLOC(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 3) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		return NULL;
	}

	/*
	 * igraph_layout_bipartite() parameters:
	 * @param graph           The graph to layout
	 * @param types          Vertex type vector (true=top row, false=bottom row)
	 * @param res             Output matrix (will be resized to vcount x 2)
	 * @param hgap            Horizontal gap between vertices
	 * @param vgap            Vertical gap between rows
	 * @param maxiter        Maximum iterations (default: 100)
	 *
	 * We use alternating pattern as fallback when no type attribute available
	 */
	igraph_vector_bool_t types;
	igraph_vector_bool_init(&types, vcount);

	for (igraph_integer_t i = 0; i < vcount; i++) {
		igraph_vector_bool_set(&types, i, (i % 2 == 0));
	}

	igraph_error_t code = igraph_layout_bipartite(graph, &types, result,
												  1.0, // hgap
												  1.0, // vgap
												  100  // maxiter
	);
	igraph_vector_bool_destroy(&types);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}

	if (result->ncol == 2) {
		igraph_matrix_resize(result, vcount, 3);
		for (igraph_integer_t i = 0; i < vcount; i++) {
			igraph_matrix_set(result, i, 2, 0.0);
		}
	}
	return result;
}

// ============================================================================
// BIPARTITE LAYOUT (2D)
// ============================================================================
void *compute_igraph_layout_bipartite_simple(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = IGRAPH_MALLOC(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 2) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		return NULL;
	}

	igraph_vector_bool_t types;
	igraph_vector_bool_init(&types, vcount);
	for (igraph_integer_t i = 0; i < vcount; i++) {
		igraph_vector_bool_set(&types, i, (i % 2 == 0));
	}

	igraph_error_t code = igraph_layout_bipartite(graph, &types, result, 1.0, 1.0, 100);
	igraph_vector_bool_destroy(&types);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}
	return result;
}

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

// ============================================================================
// GRAPHOPT LAYOUT
// ============================================================================
void *compute_igraph_layout_graphopt(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = IGRAPH_MALLOC(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 2) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		return NULL;
	}

	/*
	 * igraph_layout_graphopt() parameters:
	 * @param graph           The graph to layout
	 * @param res             Output matrix (will be resized to vcount x 2)
	 * @param niter           Number of iterations (default: 500)
	 * @param node_charge     Electrical repulsion constant (default: 30)
	 *                       Higher = more repulsion between nodes
	 * @param node_mass       Node mass for force calculation (default: 30)
	 * @param spring_length   Ideal edge length (default: 30)
	 * @param spring_constant Spring constant (default: 1.0)
	 * @param max_sa_movement Max movement in simulated annealing (default: 5.0)
	 * @param use_seed        Whether to use initial positions
	 *
	 * Force-directed using electrical-springs model
	 */
	igraph_int_t niter = 500;
	igraph_real_t node_charge = 30.0;
	igraph_real_t node_mass = 30.0;
	igraph_real_t spring_length = 30.0;
	igraph_real_t spring_constant = 1.0;
	igraph_real_t max_sa_movement = 5.0;

	igraph_error_t code = igraph_layout_graphopt(graph, result, niter, node_charge, node_mass, spring_length, spring_constant, max_sa_movement,
												 0 // use_seed
	);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}
	return result;
}

// ============================================================================
// LGL LAYOUT (Large Graph Layout)
// ============================================================================
void *compute_igraph_layout_lgl(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = IGRAPH_MALLOC(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 2) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		return NULL;
	}

	/*
	 * igraph_layout_lgl() parameters:
	 * @param graph           The graph to layout (must be connected)
	 * @param res             Output matrix (will be resized to vcount x 2)
	 * @param maxit           Maximum iterations per layer (default: 150)
	 * @param maxdelta        Max vertex movement per iteration (default: vcount)
	 * @param area            Drawing area size (default: vcount^2)
	 * @param coolexp         Cooling exponent (default: 1.5)
	 * @param repulserad      Repulsion radius (default: area * vcount)
	 * @param cellsize        Grid cell size (default: sqrt(area/vcount))
	 * @param proot           Root vertex (-1 = random)
	 *
	 * Fruchterman-Reingold style with grid optimization
	 * Good for large graphs with natural hierarchy
	 */
	igraph_int_t maxiter = 150;
	igraph_real_t maxdelta = (igraph_real_t)vcount;
	igraph_real_t area = (igraph_real_t)(vcount * vcount);
	igraph_real_t coolexp = 1.5;
	igraph_real_t repulserad = area / 50.0;
	igraph_real_t cellsize = sqrt(area / M_PI) / 4.0;
	igraph_int_t root = -1;

	igraph_error_t code = igraph_layout_lgl(graph, result,
											maxiter,	// maxit
											maxdelta,	// maxdelta: vcount
											area,		// area: vcount^2
											coolexp,	// coolexp: 1.5
											repulserad, // repulserad
											cellsize,	// cellsize
											root		// proot: random
	);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}
	return result;
}

// ============================================================================
// GEM LAYOUT
// ============================================================================
// Reference: Frick, A., Ludwig, A., and Mehldau, H. (1994):
// "A Fast Adaptive Layout Algorithm for Undirected Graphs."
void *compute_igraph_layout_gem(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = IGRAPH_MALLOC(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 2) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		return NULL;
	}

	/*
	 * igraph_layout_gem() parameters:
	 * @param graph           The graph to layout (edge directions ignored)
	 * @param res             Output matrix (will be resized to vcount x 2)
	 * @param use_seed        Whether to use initial positions
	 * @param maxiter         Maximum iterations (default: 40*vcount*vcount)
	 *                       Paper suggests 4*vcount*vcount, but 40*vcount*vcount more reliable
	 * @param temp_max        Maximum temperature (default: vcount)
	 * @param temp_min        Termination temperature (default: 0.1)
	 * @param temp_init       Initial temperature (default: sqrt(vcount))
	 *
	 * Fast adaptive layout using simulated annealing
	 * O(t * n * (n+e)) time complexity
	 */
	igraph_int_t maxiter = 40 * vcount * vcount;
	igraph_real_t temp_max = (igraph_real_t)vcount;
	igraph_real_t temp_min = 0.1;
	igraph_real_t temp_init = sqrt((igraph_real_t)vcount);

	igraph_error_t code = igraph_layout_gem(graph, result,
											0,		  // use_seed
											maxiter,  // maxiter: 40*vcount^2
											temp_max, // temp_max: vcount
											temp_min, // temp_min: 0.1
											temp_init // temp_init: sqrt(vcount)
	);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}
	return result;
}

// ============================================================================
// YIFAN HU LAYOUT (2D)
// ============================================================================
void *compute_igraph_layout_yifan_hu(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = IGRAPH_MALLOC(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 2) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		return NULL;
	}

	/*
	 * igraph_layout_yifan_hu() parameters:
	 * @param graph           The graph to layout
	 * @param res             Output matrix (will be resized to vcount x 2)
	 * @param use_seed        Whether to use initial positions
	 * @param maxiter         Maximum iterations (default: 500)
	 * @param repulsive_exponent  Repulsion exponent (-1 = auto-calculate)
	 * @param natural_length   Natural edge length (-1 = auto-calculate)
	 * @param step            Initial step size (default: 0.1)
	 * @param adaptive_cooling  Use adaptive cooling (default: true)
	 * @param tolerance       Convergence tolerance (default: 0.001)
	 * @param quadtree_scheme Barnes-Hut quadtree scheme
	 * @param max_qtree_level Maximum quadtree depth
	 * @param beautify_leaves Make leaf nodes more uniform
	 * @param weights         Edge weights (NULL = unit weight)
	 * @param minx/maxx/miny/maxy  Bounding box constraints
	 *
	 * Uses Barnes-Hut optimization for O(n log n) performance
	 * Efficient for large graphs
	 */
	igraph_int_t maxiter = 500;
	igraph_real_t repulsive_exponent = -1.0;
	igraph_real_t natural_length = -1.0;
	igraph_real_t step = 0.1;
	igraph_bool_t adaptive_cooling = 1;
	igraph_real_t tolerance = 0.001;
	igraph_quadtree_scheme_t quadtree_scheme = IGRAPH_QUADTREE_NORMAL;
	igraph_int_t max_qtree_level = 10;
	igraph_bool_t beautify_leaves = 0;

	igraph_error_t code = igraph_layout_yifan_hu(graph, result,
												 0,						// use_seed
												 maxiter,				// maxiter: 500
												 repulsive_exponent,	// -1 = auto
												 natural_length,		// -1 = auto
												 step,					// step: 0.1
												 adaptive_cooling,		// adaptive cooling
												 tolerance,				// tolerance
												 quadtree_scheme,		// quadtree
												 max_qtree_level,		// max depth
												 beautify_leaves,		// beautify
												 NULL,					// weights
												 NULL, NULL, NULL, NULL // bounds
	);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}
	return result;
}

// ============================================================================
// YIFAN HU LAYOUT (3D)
// ============================================================================
void *compute_igraph_layout_yifan_hu_3d(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = IGRAPH_MALLOC(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 3) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		return NULL;
	}

	/*
	 * igraph_layout_yifan_hu_3d() parameters: same as 2D with:
	 * @param res             Output matrix (vcount x 3)
	 * @param weights         Only parameter (others inherited)
	 */
	igraph_int_t maxiter = 500;
	igraph_real_t repulsive_exponent = -1.0;
	igraph_real_t natural_length = -1.0;
	igraph_real_t step = 0.1;
	igraph_bool_t adaptive_cooling = 1;
	igraph_real_t tolerance = 0.001;
	igraph_quadtree_scheme_t quadtree_scheme = IGRAPH_QUADTREE_NORMAL;
	igraph_int_t max_qtree_level = 10;
	igraph_bool_t beautify_leaves = 0;

	igraph_error_t code = igraph_layout_yifan_hu_3d(graph, result, 0, maxiter, repulsive_exponent, natural_length, step, adaptive_cooling, tolerance, quadtree_scheme, max_qtree_level, beautify_leaves,
													NULL // weights
	);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}
	return result;
}

// ============================================================================
// FORCEATLAS2 LAYOUT (3D)
// ============================================================================
// Reference: Jacomy, M., Venturini, T., Heymann, S., and Bastian, M. (2014):
// "ForceAtlas2, a Continuous Graph Layout Algorithm for Handy Network Visualization"
void *compute_igraph_layout_forceatlas2_3d(igraph_t *graph)
{
	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	for (int i = 0; i < 24; i++) {
		CPU_SET(i, &cpuset);
	}
	sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);

	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_integer_t ecount = igraph_ecount(graph);
	igraph_matrix_t *result = IGRAPH_MALLOC(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 3) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		return NULL;
	}

	/*
	 * igraph_layout_forceatlas2_3d() parameters:
	 * @param graph                   The graph to layout
	 * @param res                     Output matrix (will be resized to vcount x 3)
	 * @param iterations             Number of iterations (we use 50+2*sqrt(vcount), max 1500)
	 * @param outbound_attraction_distribution  Distribute attraction along edges (default: true)
	 * @param edge_weight_influence    How edge weights affect attraction (1.0 = linear)
	 * @param jitter_tolerance        Random jitter to break ties (default: 1.0)
	 * @param barnes_hut_optimize       Use Barnes-Hut approximation (default: true)
	 * @param barnes_hut_theta        BH theta parameter (default: 1.2)
	 *                               Lower = more accurate but slower
	 * @param scaling_ratio           Scaling ratio (we use 1 + log(1+ecount)/10)
	 *                               Higher = more spread out
	 * @param strong_gravity_mode     Use strong gravity (default: false)
	 * @param gravity                 Gravity strength (we use 1 + sqrt(vcount)/100)
	 *                               Higher = more central attraction
	 * @param weights                Edge weights (NULL = unit weight)
	 *
	 * Continuous force-directed layout optimized for visualization
	 * Widely used in Gephi and other network tools
	 */
	igraph_integer_t iterations = (igraph_integer_t)(50 + sqrt((double)vcount) * 2);
	//if (iterations > 1500) {
	//	iterations = 1500;
	//}

	igraph_real_t scaling_ratio = (igraph_real_t)(1.0 + log1p((double)ecount) / 10.0);
	igraph_real_t gravity = (igraph_real_t)(1.0 + sqrt((double)vcount) / 100.0);

	printf("[ForceAtlas2] Starting: vcount=%d, ecount=%d, iterations=%d, scaling=%.2f, gravity=%.2f\n", (int)vcount, (int)ecount, (int)iterations, scaling_ratio, gravity);
	fflush(stdout);

#pragma omp parallel
	{
		if (omp_get_thread_num() == 0) {
			printf("[ForceAtlas2] OpenMP report: using %d threads\n", omp_get_num_threads());
		}
	}

	igraph_error_t code = igraph_layout_forceatlas2_3d(graph, result,
													   500,	         // iterations
													   0,             // LinLog
													   0,			  // outbound_attraction_distribution
													   0,			  // edge_weight_influence
													   1.0,			  // jitter_tolerance
													   1,			  // barnes_hut_optimize
													   1.2,			  // barnes_hut_theta
													   1.0,           // scaling_ratio
													   1,			  // strong_gravity_mode
													   1.0,		      // gravity
													   NULL			  // weights
	);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}

	/*
	 * igraph_layout_align() centers and optionally scales the layout
	 * @param graph   The graph (used for vertex count)
	 * @param layout  Layout matrix to align
	 */
	igraph_layout_align(graph, result);
	return result;
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
	if (!ctx || !ctx->app_state || !ctx->current_graph || !result_data) {
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

	igraph_layout_align(&data->g, &data->current_layout);

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
		printf("[Layout Bounds] X: [%.3f, %.3f] Y: [%.3f, %.3f] Z: [%.3f, %.3f]\n", min_x, max_x, min_y, max_y, min_z, max_z);
	}

	renderer_update_graph(renderer, data);
	printf("[apply_layout_matrix] Layout applied and renderer refreshed\n");
}
