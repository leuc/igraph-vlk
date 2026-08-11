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
#include <math.h>
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

// Reference: Fruchterman, T.M.J. and Reingold, E.M.:
// "Graph Drawing by Force-directed Placement."
// Software -- Practice and Experience, 21/11, 1129--1164, 1991.
// https://doi.org/10.1002/spe.4380211102
// Force-directed layout using attractive forces between connected vertices
// and repulsive forces between all vertex pairs.
void *compute_igraph_layout_fruchterman_reingold(ExecutionContext *ctx)
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

	igraph_bool_t use_seed = layout_fill_seed(ctx, result, vcount);

	/*
	 * igraph_layout_fruchterman_reingold() parameters:
	 * @param graph           The graph to layout
	 * @param res             Output matrix (will be resized)
	 * @param use_seed        Whether to use initial positions (0 = random start)
	 * @param niter           Number of iterations (default: 500) - we scale with sqrt(vcount)
	 * @param start_temp      Initial temperature - max displacement per iteration (default: vcount)
	 *                       Set to 10 + sqrt(vcount)
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

	igraph_vector_t weights;
	bool has_weights = graph_build_edge_weights(graph, &weights);

	igraph_error_t code = igraph_layout_fruchterman_reingold(graph, result,
															 use_seed,						// use_seed
															 niter,							// niter: more iterations for larger graphs
															 start_temp,					// start_temp: scaled to graph size
															 grid,							// grid: use grid optimization for large graphs
															 has_weights ? &weights : NULL, // weights: NULL = all edges have weight 1
															 NULL,							// minx: no constraint
															 NULL,							// maxx: no constraint
															 NULL,							// miny: no constraint
															 NULL							// maxy: no constraint
	);

	if (has_weights)
		igraph_vector_destroy(&weights);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}
	return result;
}

void *compute_igraph_layout_fruchterman_reingold_3d(ExecutionContext *ctx)
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

	igraph_bool_t use_seed = layout_fill_seed(ctx, result, vcount);

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

	igraph_vector_t weights;
	bool has_weights = graph_build_edge_weights(graph, &weights);

	igraph_error_t code = igraph_layout_fruchterman_reingold_3d(graph, result,
																use_seed,						   // use_seed
																niter,							   // niter: scaled to graph size
																start_temp,						   // start_temp: allows exploration
																has_weights ? &weights : NULL,	   // weights: NULL = unit weight
																NULL, NULL, NULL, NULL, NULL, NULL // bounds: unconstrained
	);

	if (has_weights)
		igraph_vector_destroy(&weights);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}
	return result;
}

// Reference: Kamada, T. and Kawai, S.:
// "An Algorithm for Drawing General Undirected Graphs."
// Information Processing Letters, 31/1, 7--15, 1989.
// https://doi.org/10.1016/0020-0190(89)90102-6
void *compute_igraph_layout_kamada_kawai(ExecutionContext *ctx)
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

	igraph_bool_t use_seed = layout_fill_seed(ctx, result, vcount);

	/*
	 * igraph_layout_kamada_kawai() parameters:
	 * @param graph           The graph to layout
	 * @param res             Output matrix (will be resized to vcount x 2)
	 * @param use_seed        Whether to use initial positions (0 = circle layout start)
	 * @param maxiter        Maximum iterations (at least 10*vcount recommended)
	 *                       Set to 10 * vcount, following igraph guidance
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

	igraph_vector_t weights;
	bool has_weights = graph_build_edge_weights(graph, &weights);

	igraph_error_t code = igraph_layout_kamada_kawai(graph, result,
													 use_seed,						// use_seed
													 maxiter,						// maxiter: 10*vcount as per docs
													 epsilon,						// epsilon: run all iterations
													 kkconst,						// kkconst: typical value = vcount
													 has_weights ? &weights : NULL, // weights: NULL = unit weights in distance calc
													 NULL, NULL, NULL, NULL			// bounds: unconstrained
	);

	if (has_weights)
		igraph_vector_destroy(&weights);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}
	return result;
}

void *compute_igraph_layout_kamada_kawai_3d(ExecutionContext *ctx)
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

	igraph_bool_t use_seed = layout_fill_seed(ctx, result, vcount);

	/*
	 * igraph_layout_kamada_kawai_3d() parameters: same as 2D version but with:
	 * @param res             Output matrix (will be resized to vcount x 3)
	 * @param minz/maxz       Additional Z-axis bounds
	 */
	igraph_int_t maxiter = vcount * 10;
	igraph_real_t epsilon = 0.0;
	igraph_real_t kkconst = (igraph_real_t)vcount;

	igraph_vector_t weights;
	bool has_weights = graph_build_edge_weights(graph, &weights);

	igraph_error_t code = igraph_layout_kamada_kawai_3d(graph, result,
														use_seed,						   // use_seed
														maxiter,						   // maxiter: 10*vcount
														epsilon,						   // epsilon
														kkconst,						   // kkconst: vcount
														has_weights ? &weights : NULL,	   // weights
														NULL, NULL, NULL, NULL, NULL, NULL // bounds
	);

	if (has_weights)
		igraph_vector_destroy(&weights);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}
	return result;
}
