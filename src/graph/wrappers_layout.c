#include "graph/wrappers_layout.h"
#include "app_state.h"
#include "interaction/state.h"
#include "vulkan/renderer.h"
#include <float.h>
#include <igraph.h>
#include <igraph_progress.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

// Pure worker function - no UI or state dependencies
void *compute_lay_force_fr(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = malloc(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 3) != IGRAPH_SUCCESS) {
		free(result);
		return NULL;
	}

	igraph_error_t code = igraph_layout_fruchterman_reingold_3d(graph, result, 1, 300, (igraph_real_t)vcount, NULL, NULL, NULL, NULL, NULL, NULL, NULL);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		free(result);
		return NULL;
	}
	return result;
}

// Kamada-Kawai layout
void *compute_lay_force_kk(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = malloc(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 3) != IGRAPH_SUCCESS) {
		free(result);
		return NULL;
	}

	igraph_error_t code = igraph_layout_kamada_kawai_3d(graph, result, 0, vcount * 10, 0.0, (igraph_real_t)vcount, NULL, NULL, NULL, NULL, NULL, NULL, NULL);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		free(result);
		return NULL;
	}
	return result;
}

// DRL layout
void *compute_lay_force_drl(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = malloc(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 3) != IGRAPH_SUCCESS) {
		free(result);
		return NULL;
	}

	igraph_layout_drl_options_t options;
	igraph_layout_drl_options_init(&options, IGRAPH_LAYOUT_DRL_DEFAULT);

	igraph_error_t code = igraph_layout_drl_3d(graph, result, 0, &options, NULL);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		free(result);
		return NULL;
	}
	return result;
}

// Davidson-Harel layout
void *compute_lay_force_dh(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_integer_t ecount = igraph_ecount(graph);
	igraph_matrix_t *result = malloc(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 3) != IGRAPH_SUCCESS) {
		free(result);
		return NULL;
	}

	igraph_real_t density = (vcount > 1) ? (2.0 * ecount) / ((igraph_real_t)(vcount * (vcount - 1))) : 0.0;
	igraph_int_t fineiter = (igraph_int_t)fmax(10.0, (igraph_real_t)log2((igraph_real_t)vcount));
	igraph_real_t coolfact = 0.75;
	igraph_real_t w_dist = 1.0;
	igraph_real_t w_border = 0.5;
	igraph_real_t w_edge_len = density / 10.0;
	igraph_real_t w_edge_cross = 1.0 - sqrt(density);
	igraph_real_t w_node_edge = (1.0 - density) / 5.0;

	igraph_error_t code = igraph_layout_davidson_harel(graph, result, 0, 10, fineiter, coolfact, w_dist, w_border, w_edge_len, w_edge_cross, w_node_edge);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		free(result);
		return NULL;
	}
	return result;
}

// Reingold-Tilford tree layout
void *compute_lay_tree_rt(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = malloc(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 3) != IGRAPH_SUCCESS) {
		free(result);
		return NULL;
	}

	igraph_vector_int_t roots;
	igraph_vector_int_init(&roots, vcount > 0 ? 1 : 0);
	if (vcount > 0) {
		igraph_vector_int_set(&roots, 0, 0);
	}

	igraph_error_t code = igraph_layout_reingold_tilford(graph, result, IGRAPH_ALL, vcount > 0 ? &roots : NULL, NULL);

	igraph_vector_int_destroy(&roots);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		free(result);
		return NULL;
	}

	// Convert 2D to 3D if needed
	if (result->ncol < 3) {
		igraph_matrix_resize(result, vcount, 3);
		for (igraph_integer_t i = 0; i < vcount; i++) {
			igraph_matrix_set(result, i, 2, 0.0);
		}
	}
	return result;
}

// Sugiyama layout
void *compute_lay_tree_sug(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = malloc(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 3) != IGRAPH_SUCCESS) {
		free(result);
		return NULL;
	}

	igraph_matrix_list_t routing;
	igraph_matrix_list_init(&routing, 0);

	igraph_vector_int_t layers;
	igraph_vector_int_init(&layers, vcount);

	igraph_error_t code = igraph_layout_sugiyama(graph, result, &routing, &layers, 1.0, 1.0, 100, NULL);

	igraph_matrix_list_destroy(&routing);
	igraph_vector_int_destroy(&layers);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		free(result);
		return NULL;
	}
	return result;
}

// Circle layout
void *compute_lay_geo_circle(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = malloc(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 3) != IGRAPH_SUCCESS) {
		free(result);
		return NULL;
	}

	igraph_vs_t order;
	igraph_vs_all(&order);

	igraph_error_t code = igraph_layout_circle(graph, result, order);
	igraph_vs_destroy(&order);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		free(result);
		return NULL;
	}

	// Convert 2D to 3D
	for (igraph_integer_t i = 0; i < vcount; i++) {
		igraph_matrix_set(result, i, 2, 0.0);
	}
	return result;
}

// Star layout
void *compute_lay_geo_star(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = malloc(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 3) != IGRAPH_SUCCESS) {
		free(result);
		return NULL;
	}

	igraph_error_t code = igraph_layout_star(graph, result, 0, NULL);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		free(result);
		return NULL;
	}

	// Convert 2D to 3D
	for (igraph_integer_t i = 0; i < vcount; i++) {
		igraph_matrix_set(result, i, 2, 0.0);
	}
	return result;
}

// Grid layout
void *compute_lay_geo_grid(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = malloc(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 3) != IGRAPH_SUCCESS) {
		free(result);
		return NULL;
	}

	int side = (int)ceil(pow((double)vcount, 1.0 / 3.0));

	igraph_error_t code = igraph_layout_grid_3d(graph, result, (igraph_integer_t)side, (igraph_integer_t)side);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		free(result);
		return NULL;
	}
	return result;
}

// Sphere layout
void *compute_lay_geo_sphere(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = malloc(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 3) != IGRAPH_SUCCESS) {
		free(result);
		return NULL;
	}

	igraph_error_t code = igraph_layout_sphere(graph, result);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		free(result);
		return NULL;
	}
	return result;
}

// Random layout
void *compute_lay_geo_rand(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = malloc(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 3) != IGRAPH_SUCCESS) {
		free(result);
		return NULL;
	}

	igraph_error_t code = igraph_layout_random_3d(graph, result);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		free(result);
		return NULL;
	}
	return result;
}

// MDS layout (2D)
void *compute_lay_bip_mds(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = malloc(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 3) != IGRAPH_SUCCESS) {
		free(result);
		return NULL;
	}

	// Compute distances first
	igraph_matrix_t dist_matrix;
	if (igraph_matrix_init(&dist_matrix, vcount, vcount) != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		free(result);
		return NULL;
	}

	igraph_vs_t all_vs;
	igraph_vs_all(&all_vs);

	igraph_error_t dist_result = igraph_distances_dijkstra(graph, &dist_matrix, all_vs, all_vs, NULL, IGRAPH_ALL);

	igraph_vs_destroy(&all_vs);

	if (dist_result != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(&dist_matrix);
		igraph_matrix_destroy(result);
		free(result);
		return NULL;
	}

	// Compute MDS in 2D (fills only first 2 columns of the 3D matrix)
	igraph_error_t code = igraph_layout_mds(graph, result, &dist_matrix, 2);
	igraph_matrix_destroy(&dist_matrix);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		free(result);
		return NULL;
	}

	// Set Z coordinate to 0 to convert 2D to 3D
	for (igraph_integer_t i = 0; i < vcount; i++) {
		igraph_matrix_set(result, i, 2, 0.0);
	}

	return result;
}

// Bipartite layout
void *compute_lay_bip_sug(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = malloc(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 3) != IGRAPH_SUCCESS) {
		free(result);
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
		free(result);
		return NULL;
	}

	// Ensure 3D with Z=0 if needed
	if (result->ncol == 2) {
		igraph_matrix_resize(result, vcount, 3);
		for (igraph_integer_t i = 0; i < vcount; i++) {
			igraph_matrix_set(result, i, 2, 0.0);
		}
	}
	return result;
}

// UMAP layout
void *compute_lay_umap(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = malloc(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 3) != IGRAPH_SUCCESS) {
		free(result);
		return NULL;
	}

	igraph_error_t code = igraph_layout_umap_3d(graph, result, 0, NULL, 0.5, 500, 0);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		free(result);
		return NULL;
	}
	return result;
}

// Fruchterman-Reingold layout (2D)
void *compute_lay_force_fr_2d(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = malloc(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 2) != IGRAPH_SUCCESS) {
		free(result);
		return NULL;
	}

	igraph_error_t code = igraph_layout_fruchterman_reingold(graph, result, 0, 500, 50.0, IGRAPH_LAYOUT_AUTOGRID, NULL, NULL, NULL, NULL, NULL);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		free(result);
		return NULL;
	}
	return result;
}

// Kamada-Kawai layout (2D)
void *compute_lay_force_kk_2d(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = malloc(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 2) != IGRAPH_SUCCESS) {
		free(result);
		return NULL;
	}

	igraph_error_t code = igraph_layout_kamada_kawai(graph, result, 0, vcount * 10, 0.0, (igraph_real_t)vcount, NULL, NULL, NULL, NULL, NULL);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		free(result);
		return NULL;
	}
	return result;
}

// DRL layout (2D)
void *compute_lay_force_drl_2d(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = malloc(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 2) != IGRAPH_SUCCESS) {
		free(result);
		return NULL;
	}

	igraph_layout_drl_options_t options;
	igraph_layout_drl_options_init(&options, IGRAPH_LAYOUT_DRL_DEFAULT);

	igraph_error_t code = igraph_layout_drl(graph, result, 0, &options, NULL);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		free(result);
		return NULL;
	}
	return result;
}

// Random layout (2D)
void *compute_lay_geo_rand_2d(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = malloc(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 2) != IGRAPH_SUCCESS) {
		free(result);
		return NULL;
	}

	igraph_error_t code = igraph_layout_random(graph, result);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		free(result);
		return NULL;
	}
	return result;
}

// Grid layout (2D)
void *compute_lay_geo_grid_2d(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = malloc(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 2) != IGRAPH_SUCCESS) {
		free(result);
		return NULL;
	}

	igraph_error_t code = igraph_layout_grid(graph, result, 0);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		free(result);
		return NULL;
	}
	return result;
}

// Circle layout (2D)
void *compute_lay_geo_circle_2d(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = malloc(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 2) != IGRAPH_SUCCESS) {
		free(result);
		return NULL;
	}

	igraph_vs_t order;
	igraph_vs_all(&order);

	igraph_error_t code = igraph_layout_circle(graph, result, order);
	igraph_vs_destroy(&order);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		free(result);
		return NULL;
	}
	return result;
}

// UMAP layout (2D)
void *compute_lay_umap_2d(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = malloc(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 2) != IGRAPH_SUCCESS) {
		free(result);
		return NULL;
	}

	igraph_error_t code = igraph_layout_umap(graph, result, 0, NULL, 0.5, 500, 0);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		free(result);
		return NULL;
	}
	return result;
}

// GraphOpt layout (2D)
void *compute_lay_force_go(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = malloc(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 2) != IGRAPH_SUCCESS) {
		free(result);
		return NULL;
	}

	igraph_error_t code = igraph_layout_graphopt(graph, result, 500, 0.001, 30.0, 0.0, 1.0, 5.0, 0);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		free(result);
		return NULL;
	}
	return result;
}

// Large Graph Layout (LGL)
void *compute_lay_force_lgl(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = malloc(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 2) != IGRAPH_SUCCESS) {
		free(result);
		return NULL;
	}

	igraph_int_t maxiter = 500;
	igraph_real_t maxdelta = 1.0;
	igraph_real_t area = (igraph_real_t)vcount * 10.0;
	igraph_real_t coolexp = 0.5;
	igraph_real_t repulserad = area / 50.0;
	igraph_real_t cellsize = area / 100.0;
	igraph_int_t root = -1;

	igraph_error_t code = igraph_layout_lgl(graph, result, maxiter, maxdelta, area, coolexp, repulserad, cellsize, root);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		free(result);
		return NULL;
	}
	return result;
}

// GEM layout
void *compute_lay_force_gem(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = malloc(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 2) != IGRAPH_SUCCESS) {
		free(result);
		return NULL;
	}

	// Default parameters according to igraph documentation:
	// maxiter: 40 * n * n (as per paper, but can be lowered for speed)
	// temp_max: number of vertices
	// temp_min: 0.1
	// temp_init: sqrt(number of vertices)
	igraph_int_t maxiter = 40 * vcount * vcount;
	igraph_real_t temp_max = (igraph_real_t)vcount;
	igraph_real_t temp_min = 0.1;
	igraph_real_t temp_init = sqrt((igraph_real_t)vcount);

	igraph_error_t code = igraph_layout_gem(graph, result, /*use_seed=*/0, maxiter, temp_max, temp_min, temp_init);

	if (code != IGRAPH_SUCCESS) {
		igraph_matrix_destroy(result);
		free(result);
		return NULL;
	}

	return result;
}

// Simple bipartite layout
void *compute_lay_bip_simple(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = malloc(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 2) != IGRAPH_SUCCESS) {
		free(result);
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
		free(result);
		return NULL;
	}

	// This returns 2D; apply_layout_matrix will handle Z=0 if needed.
	return result;
}

void free_layout_matrix(void *result_data)
{
	if (result_data) {
		igraph_matrix_destroy((igraph_matrix_t *)result_data);
		free(result_data);
	}
}

// Apply function - bridge to update graph state
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

	// Check if layout dimensions match
	if (layout->nrow != data->node_count || layout->ncol < 2) {
		fprintf(stderr, "[apply_layout_matrix] Error: Layout dimensions don't match node count\n");
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

		// Compute and print position ranges for debugging
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

	// Trigger renderer update to display new layout
	renderer_update_graph(renderer, data);

	printf("[apply_layout_matrix] Layout applied and renderer refreshed\n");
}

// Apply function that centers and normalizes layout to unit sphere
void apply_layout_matrix_centered(ExecutionContext *ctx, void *result_data)
{
	if (!ctx || !ctx->app_state || !ctx->current_graph || !result_data) {
		fprintf(stderr, "[apply_layout_matrix_centered] Error: Invalid parameters\n");
		return;
	}

	AppState *state = ctx->app_state;
	GraphData *data = &state->current_graph;
	Renderer *renderer = &state->renderer;
	igraph_matrix_t *layout = (igraph_matrix_t *)result_data;

	if (!data->graph_initialized) {
		fprintf(stderr, "[apply_layout_matrix_centered] Error: Graph not initialized\n");
		return;
	}

	// Check if layout dimensions match
	if (layout->nrow != data->node_count || layout->ncol < 2) {
		fprintf(stderr, "[apply_layout_matrix_centered] Error: Layout dimensions don't match node count\n");
		return;
	}

	// Destroy old layout and replace with new one
	igraph_matrix_destroy(&data->current_layout);
	igraph_matrix_init_copy(&data->current_layout, layout);

	if (data->nodes) {
		// First, copy positions from layout to nodes
		for (igraph_integer_t i = 0; i < data->node_count; i++) {
			data->nodes[i].position[0] = (float)MATRIX(data->current_layout, i, 0);
			data->nodes[i].position[1] = (float)MATRIX(data->current_layout, i, 1);
			data->nodes[i].position[2] = (igraph_matrix_ncol(&data->current_layout) > 2) ? (float)MATRIX(data->current_layout, i, 2) : 0.0f;
		}

		// Compute centroid
		double sum_x = 0.0, sum_y = 0.0, sum_z = 0.0;
		for (igraph_integer_t i = 0; i < data->node_count; i++) {
			sum_x += data->nodes[i].position[0];
			sum_y += data->nodes[i].position[1];
			sum_z += data->nodes[i].position[2];
		}
		float centroid_x = (float)(sum_x / data->node_count);
		float centroid_y = (float)(sum_y / data->node_count);
		float centroid_z = (float)(sum_z / data->node_count);

		// Compute max distance from centroid
		double max_dist_sq = 0.0;
		for (igraph_integer_t i = 0; i < data->node_count; i++) {
			double dx = data->nodes[i].position[0] - centroid_x;
			double dy = data->nodes[i].position[1] - centroid_y;
			double dz = data->nodes[i].position[2] - centroid_z;
			double dist_sq = dx * dx + dy * dy + dz * dz;
			if (dist_sq > max_dist_sq)
				max_dist_sq = dist_sq;
		}
		double max_dist = sqrt(max_dist_sq);

		// Avoid division by zero; also if layout is already compact, don't scale
		float scale = (max_dist > 1e-6) ? (1.0f / (float)max_dist) : 1.0f;

		// Apply centering and scaling to nodes and update layout matrix
		for (igraph_integer_t i = 0; i < data->node_count; i++) {
			// Center and scale
			float x = (data->nodes[i].position[0] - centroid_x) * scale;
			float y = (data->nodes[i].position[1] - centroid_y) * scale;
			float z = (data->nodes[i].position[2] - centroid_z) * scale;

			data->nodes[i].position[0] = x;
			data->nodes[i].position[1] = y;
			data->nodes[i].position[2] = z;

			// Update layout matrix
			igraph_matrix_set(&data->current_layout, i, 0, x);
			igraph_matrix_set(&data->current_layout, i, 1, y);
			if (igraph_matrix_ncol(&data->current_layout) > 2) {
				igraph_matrix_set(&data->current_layout, i, 2, z);
			}
		}

		// Compute and print final bounds
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
		printf("[Layout Bounds (centered)] X: [%.3f, %.3f] Y: [%.3f, %.3f] Z: [%.3f, %.3f]\n", min_x, max_x, min_y, max_y, min_z, max_z);
	}

	// Trigger renderer update to display new layout
	renderer_update_graph(renderer, data);

	printf("[apply_layout_matrix_centered] Layout centered and scaled to unit sphere, renderer refreshed\n");
}
