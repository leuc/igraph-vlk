#define _GNU_SOURCE

#include "graph/wrappers_layout.h"
#include <igraph.h>
#include <igraph_constants.h>
#include <math.h>
#include <omp.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>

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
	// if (iterations > 1500) {
	//	iterations = 1500;
	// }

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
													   1500,		  // iterations
													   0,			  // LinLog
													   0,			  // outbound_attraction_distribution
													   0,			  // edge_weight_influence
													   1.0,			  // jitter_tolerance
													   1,			  // barnes_hut_optimize
													   1.2,			  // barnes_hut_theta
													   scaling_ratio, // scaling_ratio
													   0,			  // strong_gravity_mode
													   gravity,		  // gravity
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