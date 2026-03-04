#include "graph/wrappers.h"
#include "app_state.h"
#include "graph/graph_core.h"
#include "interaction/state.h"
#include "vulkan/renderer.h"
#include <igraph_progress.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

// Custom progress handler for our application
static igraph_error_t igraph_vlk_progress_handler(const char *message, igraph_real_t percent, void *data)
{
	printf("[Progress] %s: %.1f%%\n", message, percent);
	return IGRAPH_SUCCESS;
}


// Helper to remove progress handler
static void cleanup_progress_handler(void)
{
	igraph_set_progress_handler(NULL);
}

// Helper function to apply a computed layout matrix to the graph and update visualization
static void apply_layout_to_graph(ExecutionContext *ctx, igraph_matrix_t *layout)
{
	if (!ctx || !ctx->app_state || !ctx->current_graph)
		return;


	// Check if layout dimensions match
	if (layout->nrow != data->node_count || layout->ncol < 2) {
		fprintf(stderr, "[Wrapper] Error: Layout dimensions don't match node count\n");
		return;
	}

	// Destroy old layout and replace with new one
	igraph_matrix_destroy(&data->current_layout);
	igraph_matrix_init_copy(&data->current_layout, layout);

	// Sync node positions from the layout matrix
	if (data->nodes) {
		for (igraph_integer_t i = 0; i < data->node_count; i++) {
			data->nodes[i].position[0] = (float)MATRIX(data->current_layout, i, 0);
			data->nodes[i].position[1] = (float)MATRIX(data->current_layout, i, 1);
			data->nodes[i].position[2] = (igraph_matrix_ncol(&data->current_layout) > 2) ? (float)MATRIX(data->current_layout, i, 2) : 0.0f;
		}
	}

	// Trigger renderer update to display new layout
	renderer_update_graph(renderer, data);

	printf("[Wrapper] Layout updated and renderer refreshed\n");
}

// ============================================================================
// Force-Directed Layout Wrappers
// ============================================================================

void wrapper_lay_force_fr(ExecutionContext *ctx)
{
	if (!ctx || !ctx->current_graph) {
		fprintf(stderr, "[Wrapper] Error: Invalid context for Fruchterman-Reingold\n");
		return;
	}

	igraph_integer_t vcount = igraph_vcount(ctx->current_graph);

	igraph_matrix_t layout;
	if (igraph_matrix_init(&layout, vcount, 3) != IGRAPH_SUCCESS) {
		fprintf(stderr, "[Wrapper] Error: Failed to initialize layout matrix\n");
		return;
	}

	igraph_error_t result = igraph_layout_fruchterman_reingold_3d(ctx->current_graph, &layout, 1, /* use_seed */
																  300,							  /* iterations */
																  (igraph_real_t)vcount,		  /* start_temp */
																  NULL,							  /* weights */
																  NULL,							  /* minx */
																  NULL,							  /* maxx */
																  NULL,							  /* miny */
																  NULL,							  /* maxy */
																  NULL,							  /* minz */
																  NULL							  /* maxz */
	);

	if (result != IGRAPH_SUCCESS) {
		fprintf(stderr, "[Wrapper] Error: igraph_layout_fruchterman_reingold_3d failed\n");
		igraph_matrix_destroy(&layout);
		return;
	}

	apply_layout_to_graph(ctx, &layout);
	igraph_matrix_destroy(&layout);
}
void wrapper_lay_force_drl(ExecutionContext *ctx)
{
	if (!ctx || !ctx->current_graph) {
		fprintf(stderr, "[Wrapper] Error: Invalid context for DRL\n");
		return;
	}

	igraph_integer_t vcount = igraph_vcount(ctx->current_graph);

	igraph_matrix_t layout;
	if (igraph_matrix_init(&layout, vcount, 3) != IGRAPH_SUCCESS) {
		fprintf(stderr, "[Wrapper] Error: Failed to initialize layout matrix\n");
		return;
	}

	igraph_layout_drl_options_t options;
	igraph_layout_drl_options_init(&options, IGRAPH_LAYOUT_DRL_DEFAULT);

	// Set up progress handler for DRL layout
	setup_progress_handler();

	igraph_error_t result = igraph_layout_drl_3d(ctx->current_graph, &layout, 0, /* use_seed */
												 &options,						 /* options */
												 NULL							 /* weights */
	);

	// Clean up progress handler
	cleanup_progress_handler();

	if (result != IGRAPH_SUCCESS) {
		fprintf(stderr, "[Wrapper] Error: igraph_layout_drl_3d failed\n");
		igraph_matrix_destroy(&layout);
		return;
	}

	apply_layout_to_graph(ctx, &layout);
	igraph_matrix_destroy(&layout);
}

void wrapper_lay_force_dh(ExecutionContext *ctx)
{
	if (!ctx || !ctx->current_graph) {
		fprintf(stderr, "[Wrapper] Error: Invalid context for Davidson-Harel\n");
		return;
	}

	igraph_integer_t vcount = igraph_vcount(ctx->current_graph);
	igraph_integer_t ecount = igraph_ecount(ctx->current_graph);

	igraph_matrix_t layout;
	if (igraph_matrix_init(&layout, vcount, 3) != IGRAPH_SUCCESS) {
		fprintf(stderr, "[Wrapper] Error: Failed to initialize layout matrix\n");
		return;
	}

	// Calculate graph density for weight parameters
	igraph_real_t density = 0.0;
	if (vcount > 1) {
		density = (2.0 * ecount) / ((vcount * (vcount - 1)));
	}

	// Reasonable default values based on documentation
	double coolfact = 0.75;						// Cooling factor
	double w_dist = 1.0;						// Node-node distances
	double w_border = 0.5;						// Distance from border (non-zero to keep nodes inside)
	double w_edge_len = density / 10.0;			// Edge length weight
	double w_edge_cross = 1.0 - sqrt(density);	// Edge crossing weight
	double w_node_edge = (1.0 - density) / 5.0; // Node-edge distance weight

	// Fine tuning iterations: max(10, log2(n))
	igraph_int_t fineiter = (igraph_int_t)fmax(10, (double)log2((double)vcount));

	igraph_error_t result = igraph_layout_davidson_harel(ctx->current_graph, &layout,
														 0,		   // use_seed - start from spherical/init configuration
														 10,	   // maxiter - reasonable value for smaller graphs
														 fineiter, // fineiter - fine tuning phase
														 coolfact, w_dist, w_border, w_edge_len, w_edge_cross, w_node_edge);

	if (result != IGRAPH_SUCCESS) {
		fprintf(stderr, "[Wrapper] Error: igraph_layout_davidson_haret failed\n");
		igraph_matrix_destroy(&layout);
		return;
	}

	apply_layout_to_graph(ctx, &layout);
	igraph_matrix_destroy(&layout);
}

// ============================================================================
// Tree & Hierarchical Layout Wrappers
// ============================================================================

void wrapper_lay_tree_rt(ExecutionContext *ctx)
{
	if (!ctx || !ctx->current_graph) {
		fprintf(stderr, "[Wrapper] Error: Invalid context for Reingold-Tilford\n");
		return;
	}

	igraph_integer_t vcount = igraph_vcount(ctx->current_graph);

	igraph_matrix_t layout;
	if (igraph_matrix_init(&layout, vcount, 3) != IGRAPH_SUCCESS) {
		fprintf(stderr, "[Wrapper] Error: Failed to initialize layout matrix\n");
		return;
	}

	igraph_vector_int_t roots;
	igraph_vector_int_init(&roots, vcount > 0 ? 1 : 0);
	if (vcount > 0) {
		igraph_vector_int_set(&roots, 0, 0);
	}

	// Set up progress handler for Reingold-Tilford layout
	setup_progress_handler();

	igraph_error_t result = igraph_layout_reingold_tilford(ctx->current_graph, &layout, IGRAPH_ALL, /* mode */
														   vcount > 0 ? &roots : NULL,				/* roots */
														   NULL										/* rootlevel */
	);

	// Clean up progress handler
	cleanup_progress_handler();

	igraph_vector_int_destroy(&roots);

	if (result != IGRAPH_SUCCESS) {
		fprintf(stderr, "[Wrapper] Error: igraph_layout_reingold_tilford failed\n");
		igraph_matrix_destroy(&layout);
		return;
	}

	// Convert 2D to 3D
	for (igraph_integer_t i = 0; i < vcount; i++) {
		igraph_matrix_set(&layout, i, 2, 0.0);
	}

	apply_layout_to_graph(ctx, &layout);
	igraph_matrix_destroy(&layout);
}

void wrapper_lay_tree_sug(ExecutionContext *ctx)
{
	if (!ctx || !ctx->current_graph) {
		fprintf(stderr, "[Wrapper] Error: Invalid context for Sugiyama\n");
		return;
	}

	igraph_integer_t vcount = igraph_vcount(ctx->current_graph);

	igraph_matrix_t layout;
	if (igraph_matrix_init(&layout, vcount, 3) != IGRAPH_SUCCESS) {
		fprintf(stderr, "[Wrapper] Error: Failed to initialize layout matrix\n");
		return;
	}

	igraph_matrix_list_t routing;
	if (igraph_matrix_list_init(&routing, 0) != IGRAPH_SUCCESS) {
		fprintf(stderr, "[Wrapper] Error: Failed to initialize routing matrix list\n");
		igraph_matrix_destroy(&layout);
		return;
	}

	igraph_vector_int_t layers;
	igraph_vector_int_init(&layers, vcount);

	igraph_error_t result = igraph_layout_sugiyama(ctx->current_graph, &layout, &routing, /* routing */
												   &layers,								  /* layers */
												   1.0,									  /* hgap */
												   1.0,									  /* vgap */
												   100,									  /* maxiter */
												   NULL									  /* weights */
	);

	igraph_matrix_list_destroy(&routing);
	igraph_vector_int_destroy(&layers);

	if (result != IGRAPH_SUCCESS) {
		fprintf(stderr, "[Wrapper] Error: igraph_layout_sugiyama failed\n");
		igraph_matrix_destroy(&layout);
		return;
	}

	// Convert 2D to 3D
	for (igraph_integer_t i = 0; i < vcount; i++) {
		igraph_matrix_set(&layout, i, 2, 0.0);
	}

	apply_layout_to_graph(ctx, &layout);
	igraph_matrix_destroy(&layout);
}

// ============================================================================
// Geometric Layout Wrappers
// ============================================================================

void wrapper_lay_geo_circle(ExecutionContext *ctx)
{
	if (!ctx || !ctx->current_graph) {
		fprintf(stderr, "[Wrapper] Error: Invalid context for Circle\n");
		return;
	}

	igraph_integer_t vcount = igraph_vcount(ctx->current_graph);

	igraph_matrix_t layout;
	if (igraph_matrix_init(&layout, vcount, 3) != IGRAPH_SUCCESS) {
		fprintf(stderr, "[Wrapper] Error: Failed to initialize layout matrix\n");
		return;
	}

	igraph_vs_t order;
	igraph_vs_all(&order);

	igraph_error_t result = igraph_layout_circle(ctx->current_graph, &layout, order /* order - all vertices */
	);

	if (result != IGRAPH_SUCCESS) {
		fprintf(stderr, "[Wrapper] Error: igraph_layout_circle failed\n");
		igraph_matrix_destroy(&layout);
		return;
	}

	// Convert 2D to 3D
	for (igraph_integer_t i = 0; i < vcount; i++) {
		igraph_matrix_set(&layout, i, 2, 0.0);
	}

	apply_layout_to_graph(ctx, &layout);
	igraph_matrix_destroy(&layout);
}

void wrapper_lay_geo_star(ExecutionContext *ctx)
{
	if (!ctx || !ctx->current_graph) {
		fprintf(stderr, "[Wrapper] Error: Invalid context for Star\n");
		return;
	}

	igraph_integer_t vcount = igraph_vcount(ctx->current_graph);

	igraph_matrix_t layout;
	if (igraph_matrix_init(&layout, vcount, 3) != IGRAPH_SUCCESS) {
		fprintf(stderr, "[Wrapper] Error: Failed to initialize layout matrix\n");
		return;
	}

	// Use NULL for order to get default vertex ID ordering
	// (empty vector would cause "Invalid order vector length" error)
	igraph_error_t result = igraph_layout_star(ctx->current_graph, &layout, 0, /* center vertex */
											   NULL							   /* order - use default increasing vertex ID order */
	);

	if (result != IGRAPH_SUCCESS) {
		fprintf(stderr, "[Wrapper] Error: igraph_layout_star failed\n");
		igraph_matrix_destroy(&layout);
		return;
	}

	// Convert 2D to 3D
	for (igraph_integer_t i = 0; i < vcount; i++) {
		igraph_matrix_set(&layout, i, 2, 0.0);
	}

	apply_layout_to_graph(ctx, &layout);
	igraph_matrix_destroy(&layout);
}

void wrapper_lay_geo_grid(ExecutionContext *ctx)
{
	if (!ctx || !ctx->current_graph) {
		fprintf(stderr, "[Wrapper] Error: Invalid context for Grid\n");
		return;
	}

	igraph_integer_t vcount = igraph_vcount(ctx->current_graph);

	igraph_matrix_t layout;
	if (igraph_matrix_init(&layout, vcount, 3) != IGRAPH_SUCCESS) {
		fprintf(stderr, "[Wrapper] Error: Failed to initialize layout matrix\n");
		return;
	}

	int side = (int)ceil(pow((double)vcount, 1.0 / 3.0));

	igraph_error_t result = igraph_layout_grid_3d(ctx->current_graph, &layout, (igraph_integer_t)side, /* width */
												  (igraph_integer_t)side							   /* height */
	);

	if (result != IGRAPH_SUCCESS) {
		fprintf(stderr, "[Wrapper] Error: igraph_layout_grid_3d failed\n");
		igraph_matrix_destroy(&layout);
		return;
	}

	apply_layout_to_graph(ctx, &layout);
	igraph_matrix_destroy(&layout);
}

void wrapper_lay_geo_sphere(ExecutionContext *ctx)
{
	if (!ctx || !ctx->current_graph) {
		fprintf(stderr, "[Wrapper] Error: Invalid context for Sphere\n");
		return;
	}

	igraph_integer_t vcount = igraph_vcount(ctx->current_graph);

	igraph_matrix_t layout;
	if (igraph_matrix_init(&layout, vcount, 3) != IGRAPH_SUCCESS) {
		fprintf(stderr, "[Wrapper] Error: Failed to initialize layout matrix\n");
		return;
	}

	igraph_error_t result = igraph_layout_sphere(ctx->current_graph, &layout);

	if (result != IGRAPH_SUCCESS) {
		fprintf(stderr, "[Wrapper] Error: igraph_layout_sphere failed\n");
		igraph_matrix_destroy(&layout);
		return;
	}

	apply_layout_to_graph(ctx, &layout);
	igraph_matrix_destroy(&layout);
}

void wrapper_lay_geo_rand(ExecutionContext *ctx)
{
	if (!ctx || !ctx->current_graph) {
		fprintf(stderr, "[Wrapper] Error: Invalid context for Random\n");
		return;
	}

	igraph_integer_t vcount = igraph_vcount(ctx->current_graph);

	igraph_matrix_t layout;
	if (igraph_matrix_init(&layout, vcount, 3) != IGRAPH_SUCCESS) {
		fprintf(stderr, "[Wrapper] Error: Failed to initialize layout matrix\n");
		return;
	}

	igraph_error_t result = igraph_layout_random_3d(ctx->current_graph, &layout);

	if (result != IGRAPH_SUCCESS) {
		fprintf(stderr, "[Wrapper] Error: igraph_layout_random_3d failed\n");
		igraph_matrix_destroy(&layout);
		return;
	}

	apply_layout_to_graph(ctx, &layout);
	igraph_matrix_destroy(&layout);
}

// ============================================================================
// Bipartite Layout Wrappers
// ============================================================================

void wrapper_lay_bip_mds(ExecutionContext *ctx)
{
	if (!ctx || !ctx->current_graph) {
		fprintf(stderr, "[Wrapper] Error: Invalid context for MDS\n");
		return;
	}

	igraph_integer_t vcount = igraph_vcount(ctx->current_graph);

	igraph_matrix_t dist_matrix;
	if (igraph_matrix_init(&dist_matrix, vcount, vcount) != IGRAPH_SUCCESS) {
		fprintf(stderr, "[Wrapper] Error: Failed to initialize distance matrix\n");
		return;
	}

	igraph_vs_t all_vs;
	igraph_vs_all(&all_vs);

	igraph_error_t result = igraph_distances_dijkstra(ctx->current_graph, &dist_matrix, all_vs, /* from */
													  all_vs,									/* to */
													  NULL,										/* weights */
													  IGRAPH_UNDIRECTED							/* mode */
	);

	if (result != IGRAPH_SUCCESS) {
		fprintf(stderr, "[Wrapper] Error: igraph_distances_dijkstra failed\n");
		igraph_matrix_destroy(&dist_matrix);
		return;
	}

	igraph_matrix_t layout;
	if (igraph_matrix_init(&layout, vcount, 3) != IGRAPH_SUCCESS) {
		fprintf(stderr, "[Wrapper] Error: Failed to initialize layout matrix\n");
		igraph_matrix_destroy(&dist_matrix);
		return;
	}

	result = igraph_layout_mds(ctx->current_graph, &layout, &dist_matrix, /* distance matrix */
							   3										  /* dimension */
	);

	igraph_matrix_destroy(&dist_matrix);

	if (result != IGRAPH_SUCCESS) {
		fprintf(stderr, "[Wrapper] Error: igraph_layout_mds failed\n");
		igraph_matrix_destroy(&layout);
		return;
	}

	apply_layout_to_graph(ctx, &layout);
	igraph_matrix_destroy(&layout);
}

void wrapper_lay_bip_sug(ExecutionContext *ctx)
{
	if (!ctx || !ctx->current_graph) {
		fprintf(stderr, "[Wrapper] Error: Invalid context for Bipartite Sugiyama\n");
		return;
	}

	igraph_integer_t vcount = igraph_vcount(ctx->current_graph);

	igraph_vector_bool_t types;
	igraph_vector_bool_init(&types, vcount);

	for (igraph_integer_t i = 0; i < vcount; i++) {
		igraph_vector_bool_set(&types, i, (i % 2 == 0));
	}

	igraph_matrix_t layout;
	if (igraph_matrix_init(&layout, vcount, 3) != IGRAPH_SUCCESS) {
		fprintf(stderr, "[Wrapper] Error: Failed to initialize layout matrix\n");
		igraph_vector_bool_destroy(&types);
		return;
	}

	igraph_error_t result = igraph_layout_bipartite(ctx->current_graph, &types, /* types vector */
													&layout,					/* result */
													1.0,						/* hgap */
													1.0,						/* vgap */
													100							/* maxiter */
	);

	igraph_vector_bool_destroy(&types);

	if (result != IGRAPH_SUCCESS) {
		fprintf(stderr, "[Wrapper] Error: igraph_layout_bipartite failed\n");
		igraph_matrix_destroy(&layout);
		return;
	}

	// Ensure 3D with Z=0 if needed
	if (layout.ncol == 2) {
		igraph_matrix_resize(&layout, vcount, 3);
		for (igraph_integer_t i = 0; i < vcount; i++) {
			igraph_matrix_set(&layout, i, 2, 0.0);
		}
	}

	apply_layout_to_graph(ctx, &layout);
	igraph_matrix_destroy(&layout);
}

// ============================================================================
// Dimension Reduction / Embedding Layout Wrappers
// ============================================================================

void wrapper_lay_umap(ExecutionContext *ctx)
{
	if (!ctx || !ctx->current_graph) {
		fprintf(stderr, "[Wrapper] Error: Invalid context for UMAP\n");
		return;
	}

	igraph_integer_t vcount = igraph_vcount(ctx->current_graph);
	igraph_integer_t ecount = igraph_ecount(ctx->current_graph);

	igraph_matrix_t layout;
	if (igraph_matrix_init(&layout, vcount, 3) != IGRAPH_SUCCESS) {
		fprintf(stderr, "[Wrapper] Error: Failed to initialize layout matrix\n");
		return;
	}

	// UMAP parameters based on documentation
	igraph_real_t min_dist = 0.5;			 // Typical value between 0 and 1
	igraph_int_t epochs = 300;				 // Typical value between 30 and 500
	igraph_bool_t distances_are_weights = 0; // Compute weights from distances (default)

	// For typical use case: NULL distances to assume uniform edge weights
	igraph_error_t result = igraph_layout_umap_3d(ctx->current_graph, &layout, 0, /* use_seed - random initial layout */
												  NULL,							  /* distances - all edges same distance */
												  min_dist, epochs, distances_are_weights);

	if (result != IGRAPH_SUCCESS) {
		fprintf(stderr, "[Wrapper] Error: igraph_layout_umap_3d failed\n");
		igraph_matrix_destroy(&layout);
		return;
	}

	apply_layout_to_graph(ctx, &layout);
	igraph_matrix_destroy(&layout);
}
