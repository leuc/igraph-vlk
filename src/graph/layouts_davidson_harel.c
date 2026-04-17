#define _GNU_SOURCE

#include "graph/wrappers_layout.h"
#include <igraph.h>
#include <igraph_constants.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

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