/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#define _GNU_SOURCE

#include "app_state.h"
#include "graph/wrappers_layout.h"
#include <igraph.h>
#include <igraph_constants.h>
#include <stdio.h>
#include <stdlib.h>

void *compute_igraph_layout_graphopt(ExecutionContext *ctx)
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
												 use_seed // use_seed
	);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		IGRAPH_FREE(result);
		return NULL;
	}
	return result;
}

// LGL LAYOUT (Large Graph Layout)
void *compute_igraph_layout_lgl(ExecutionContext *ctx)
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
