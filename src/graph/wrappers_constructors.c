#include "graph/wrappers_constructors.h"
#include "app_state.h"
#include "graph/graph_io.h"
#include "interaction/state.h"
#include "vulkan/animation_manager.h"
#include "vulkan/renderer.h"
#include <igraph.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// Worker Functions
// Each returns igraph_t* (new graph) on success, NULL on failure
// ============================================================================

// Ring graph
void *compute_igraph_ring(igraph_t *graph)
{
	igraph_t *new_graph = malloc(sizeof(igraph_t));
	if (!new_graph)
		return NULL;

	// Parameters: n=20 vertices, undirected, not mutual, circular (closed ring)
	igraph_error_t code = igraph_ring(new_graph, 20, 0, 0, 1);

	if (code != IGRAPH_SUCCESS) {
		igraph_destroy(new_graph);
		free(new_graph);
		return NULL;
	}
	return new_graph;
}

// Star graph
void *compute_igraph_star(igraph_t *graph)
{
	igraph_t *new_graph = malloc(sizeof(igraph_t));
	if (!new_graph)
		return NULL;

	// Parameters: n=11 vertices (1 center + 10 leaves), undirected, center at 0
	igraph_error_t code = igraph_star(new_graph, 11, IGRAPH_STAR_UNDIRECTED, 0);

	if (code != IGRAPH_SUCCESS) {
		igraph_destroy(new_graph);
		free(new_graph);
		return NULL;
	}
	return new_graph;
}

// Tree (k-ary tree, binary by default)
void *compute_igraph_kary_tree(igraph_t *graph)
{
	igraph_t *new_graph = malloc(sizeof(igraph_t));
	if (!new_graph)
		return NULL;

	// Parameters: n=15 vertices, binary tree (2 children), undirected
	igraph_error_t code = igraph_kary_tree(new_graph, 15, 2, IGRAPH_TREE_UNDIRECTED);

	if (code != IGRAPH_SUCCESS) {
		igraph_destroy(new_graph);
		free(new_graph);
		return NULL;
	}
	return new_graph;
}

// Square lattice
void *compute_igraph_square_lattice(igraph_t *graph)
{
	igraph_t *new_graph = malloc(sizeof(igraph_t));
	if (!new_graph)
		return NULL;

	// 2D lattice: 5x5 = 25 vertices
	igraph_vector_int_t dimvector;
	igraph_vector_int_init(&dimvector, 2);
	igraph_vector_int_set(&dimvector, 0, 5);
	igraph_vector_int_set(&dimvector, 1, 5);

	// Parameters: dimvector, nei=1 (nearest neighbors), undirected, not mutual, not periodic
	igraph_error_t code = igraph_square_lattice(new_graph, &dimvector, 1, 0, 0, NULL);

	igraph_vector_int_destroy(&dimvector);

	if (code != IGRAPH_SUCCESS) {
		igraph_destroy(new_graph);
		free(new_graph);
		return NULL;
	}
	return new_graph;
}

// Full graph (clique)
void *compute_igraph_full(igraph_t *graph)
{
	igraph_t *new_graph = malloc(sizeof(igraph_t));
	if (!new_graph)
		return NULL;

	// Parameters: n=10 vertices, undirected, no loops
	igraph_error_t code = igraph_full(new_graph, 10, 0, 0);

	if (code != IGRAPH_SUCCESS) {
		igraph_destroy(new_graph);
		free(new_graph);
		return NULL;
	}
	return new_graph;
}

// Cycle graph (circle)
void *compute_igraph_cycle_graph(igraph_t *graph)
{
	igraph_t *new_graph = malloc(sizeof(igraph_t));
	if (!new_graph)
		return NULL;

	// Parameters: n=12 vertices, undirected, not mutual
	igraph_error_t code = igraph_cycle_graph(new_graph, 12, 0, 0);

	if (code != IGRAPH_SUCCESS) {
		igraph_destroy(new_graph);
		free(new_graph);
		return NULL;
	}
	return new_graph;
}

// Famous graph (Zachary's karate club)
void *compute_igraph_famous(igraph_t *graph)
{
	igraph_t *new_graph = malloc(sizeof(igraph_t));
	if (!new_graph)
		return NULL;

	// Parameters: "zachary" - the famous karate club network
	igraph_error_t code = igraph_famous(new_graph, "zachary");

	if (code != IGRAPH_SUCCESS) {
		igraph_destroy(new_graph);
		free(new_graph);
		return NULL;
	}
	return new_graph;
}

// Erdős-Rényi G(n,p) random graph
void *compute_igraph_erdos_renyi_game_gnp(igraph_t *graph)
{
	igraph_t *new_graph = malloc(sizeof(igraph_t));
	if (!new_graph)
		return NULL;

	// Parameters: n=30 vertices, p=0.15 edge probability, undirected, allowed_edge_types=SIMPLE_SW, edge_labeled=0
	igraph_error_t code = igraph_erdos_renyi_game_gnp(new_graph, 30, 0.15, 0, IGRAPH_SIMPLE_SW, 0);

	if (code != IGRAPH_SUCCESS) {
		igraph_destroy(new_graph);
		free(new_graph);
		return NULL;
	}
	return new_graph;
}

// Barabási-Albert preferential attachment
void *compute_igraph_barabasi_game(igraph_t *graph)
{
	igraph_t *new_graph = malloc(sizeof(igraph_t));
	if (!new_graph)
		return NULL;

	// Parameters: n=50 vertices, m=2 edges per new vertex, power=1.0, directed=0
	igraph_error_t code = igraph_barabasi_game(new_graph, 50, 1.0, 2, NULL, 1, 1.0, 0, IGRAPH_BARABASI_PSUMTREE, NULL);

	if (code != IGRAPH_SUCCESS) {
		igraph_destroy(new_graph);
		free(new_graph);
		return NULL;
	}
	return new_graph;
}

// Watts-Strogatz small-world graph
void *compute_igraph_watts_strogatz_game(igraph_t *graph)
{
	igraph_t *new_graph = malloc(sizeof(igraph_t));
	if (!new_graph)
		return NULL;

	// Parameters: dim=1 (1D ring lattice), size=20, nei=2, p=0.1 rewiring probability
	igraph_error_t code = igraph_watts_strogatz_game(new_graph, 1, 20, 2, 0.1, IGRAPH_SIMPLE_SW);

	if (code != IGRAPH_SUCCESS) {
		igraph_destroy(new_graph);
		free(new_graph);
		return NULL;
	}
	return new_graph;
}

// Forest fire model
void *compute_igraph_forest_fire_game(igraph_t *graph)
{
	igraph_t *new_graph = malloc(sizeof(igraph_t));
	if (!new_graph)
		return NULL;

	// Parameters: nodes=50, fw_prob=0.2, bw_factor=0.2/0.2 < 1, pambs=1, directed=0
	igraph_error_t code = igraph_forest_fire_game(new_graph, 50, 0.2, 0.2, 1, 0);

	if (code != IGRAPH_SUCCESS) {
		igraph_destroy(new_graph);
		free(new_graph);
		return NULL;
	}
	return new_graph;
}

// Random tree
void *compute_igraph_tree_game(igraph_t *graph)
{
	igraph_t *new_graph = malloc(sizeof(igraph_t));
	if (!new_graph)
		return NULL;

	// Parameters: n=20 vertices, undirected, Prüfer sequence method
	igraph_error_t code = igraph_tree_game(new_graph, 20, 0, IGRAPH_RANDOM_TREE_PRUFER);

	if (code != IGRAPH_SUCCESS) {
		igraph_destroy(new_graph);
		free(new_graph);
		return NULL;
	}
	return new_graph;
}

// Degree sequence game (regular graph: all degrees = 4)
void *compute_igraph_degree_sequence_game(igraph_t *graph)
{
	igraph_t *new_graph = malloc(sizeof(igraph_t));
	if (!new_graph)
		return NULL;

	// Create a degree sequence: 20 vertices each with degree 4
	igraph_vector_int_t degrees;
	igraph_vector_int_init(&degrees, 20);
	for (int i = 0; i < 20; i++) {
		igraph_vector_int_set(&degrees, i, 4);
	}

	// Parameters: out_degrees, in_degrees=NULL (undirected), configuration model
	igraph_error_t code = igraph_degree_sequence_game(new_graph, &degrees, NULL, IGRAPH_DEGSEQ_CONFIGURATION);

	igraph_vector_int_destroy(&degrees);

	if (code != IGRAPH_SUCCESS) {
		igraph_destroy(new_graph);
		free(new_graph);
		return NULL;
	}
	return new_graph;
}

// Random bipartite graph (G(n1, n2, m))
void *compute_igraph_bipartite_game_gnm(igraph_t *graph)
{
	igraph_t *new_graph = malloc(sizeof(igraph_t));
	if (!new_graph)
		return NULL;

	// Parameters: n1=8 (type 0), n2=6 (type 1), m=15 edges, undirected
	// We can optionally get types vector, but we'll pass NULL for simplicity
	igraph_error_t code = igraph_bipartite_game_gnm(new_graph, NULL, 8, 6, 15, 0, IGRAPH_OUT, IGRAPH_SIMPLE_SW, 0);

	if (code != IGRAPH_SUCCESS) {
		igraph_destroy(new_graph);
		free(new_graph);
		return NULL;
	}
	return new_graph;
}

// Bipartite projection - transforms current bipartite graph to two unipartite projections
// This is special: it operates on the current graph and returns a projection
void *compute_igraph_bipartite_projection(igraph_t *graph)
{
	// First, check if the graph is bipartite and get the vertex types
	igraph_bool_t is_bipartite;
	igraph_vector_bool_t *types = malloc(sizeof(igraph_vector_bool_t));
	if (igraph_vector_bool_init(types, 0) != IGRAPH_SUCCESS) {
		free(types);
		return NULL;
	}

	igraph_error_t code = igraph_is_bipartite(graph, &is_bipartite, types);

	if (code != IGRAPH_SUCCESS || !is_bipartite) {
		igraph_vector_bool_destroy(types);
		free(types);
		fprintf(stderr, "[compute_igraph_bipartite_projection] Error: Graph is not bipartite\n");
		return NULL;
	}

	// Create two projection graphs
	igraph_t *proj1 = malloc(sizeof(igraph_t));
	if (!proj1) {
		igraph_vector_bool_destroy(types);
		free(types);
		return NULL;
	}

	igraph_t *proj2 = malloc(sizeof(igraph_t));
	if (!proj2) {
		igraph_destroy(proj1);
		free(proj1);
		igraph_vector_bool_destroy(types);
		free(types);
		return NULL;
	}

	// Compute the projections
	code = igraph_bipartite_projection(graph, types, proj1, proj2, NULL, NULL, -1);

	igraph_vector_bool_destroy(types);
	free(types);

	if (code != IGRAPH_SUCCESS) {
		igraph_destroy(proj1);
		igraph_destroy(proj2);
		free(proj1);
		free(proj2);
		return NULL;
	}

	// For now, return the first projection (vertices of type FALSE)
	igraph_destroy(proj2);
	free(proj2);

	return proj1;
}

// Geometric random graph
void *compute_igraph_nearest_neighbor_graph(igraph_t *graph)
{
	igraph_t *new_graph = malloc(sizeof(igraph_t));
	if (!new_graph)
		return NULL;

	// Generate random points: 25 vertices in 2D unit square
	igraph_matrix_t points;
	igraph_matrix_init(&points, 25, 2);
	for (int i = 0; i < 25; i++) {
		igraph_matrix_set(&points, i, 0, (igraph_real_t)rand() / RAND_MAX);
		igraph_matrix_set(&points, i, 1, (igraph_real_t)rand() / RAND_MAX);
	}

	// Parameters: points, metric=L2 (Euclidean), k=-1 (use cutoff), cutoff=0.2, undirected
	igraph_error_t code = igraph_nearest_neighbor_graph(new_graph, &points, IGRAPH_METRIC_L2, -1, 0.2, 0);

	igraph_matrix_destroy(&points);

	if (code != IGRAPH_SUCCESS) {
		igraph_destroy(new_graph);
		free(new_graph);
		return NULL;
	}
	return new_graph;
}

// Gabriel graph
void *compute_igraph_gabriel_graph(igraph_t *graph)
{
	igraph_t *new_graph = malloc(sizeof(igraph_t));
	if (!new_graph)
		return NULL;

	// Generate random points: 20 vertices in 2D unit square
	igraph_matrix_t points;
	igraph_matrix_init(&points, 20, 2);
	for (int i = 0; i < 20; i++) {
		igraph_matrix_set(&points, i, 0, (igraph_real_t)rand() / RAND_MAX);
		igraph_matrix_set(&points, i, 1, (igraph_real_t)rand() / RAND_MAX);
	}

	// Parameters: points, (other parameters have defaults)
	igraph_error_t code = igraph_gabriel_graph(new_graph, &points);

	igraph_matrix_destroy(&points);

	if (code != IGRAPH_SUCCESS) {
		igraph_destroy(new_graph);
		free(new_graph);
		return NULL;
	}
	return new_graph;
}

// ============================================================================
// Apply and Free Functions
// ============================================================================

void apply_new_graph(ExecutionContext *ctx, void *result_data)
{
	if (!ctx || !ctx->app_state || !ctx->current_graph || !result_data) {
		fprintf(stderr, "[apply_new_graph] Error: Invalid parameters\n");
		return;
	}

	AppState *state = ctx->app_state;
	GraphData *data = &state->current_graph;
	Renderer *renderer = &state->renderer;
	igraph_t *new_graph = (igraph_t *)result_data;

	// Check if new graph is valid
	if (igraph_vcount(new_graph) == 0) {
		fprintf(stderr, "[apply_new_graph] Error: Invalid graph (0 vertices)\n");
		return;
	}

	// Clear any active animations before replacing the graph
	animation_manager_cleanup(&state->anim_manager);

	// Free current graph data (nodes, edges, layout, igraph_t, etc.)
	graph_free_data(data);

	// Copy new graph into data->g
	igraph_error_t code = igraph_copy(&data->g, new_graph);
	if (code != IGRAPH_SUCCESS) {
		fprintf(stderr, "[apply_new_graph] Error: Failed to copy graph\n");
		return;
	}
	data->graph_initialized = true;

	// Compute a sphere layout for the new graph BEFORE refreshing data
	igraph_matrix_t *layout = malloc(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(layout, data->node_count, 3) == IGRAPH_SUCCESS) {
		if (igraph_layout_sphere(&data->g, layout) == IGRAPH_SUCCESS) {
			// Replace current_layout with the new layout
			igraph_matrix_destroy(&data->current_layout);
			igraph_matrix_init_copy(&data->current_layout, layout);
		}
		igraph_matrix_destroy(layout);
		free(layout);
	}

	// Refresh all derived data structures (nodes, edges, etc.)
	// This will sync node positions from data->current_layout
	graph_refresh_data(data);

	// Reinitialize animation manager with new graph data
	animation_manager_init(&state->anim_manager, renderer, data);

	// Refresh renderer
	renderer_update_graph(renderer, data);

	printf("[apply_new_graph] New graph generated - %d vertices, %d edges\n", data->node_count, data->edge_count);
}

void free_new_graph(void *result_data)
{
	if (result_data) {
		igraph_destroy((igraph_t *)result_data);
		free(result_data);
	}
}
