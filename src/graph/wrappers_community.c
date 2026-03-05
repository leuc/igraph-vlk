#include "graph/wrappers_community.h"
#include "app_state.h"
#include "interaction/state.h"
#include "vulkan/renderer.h"
#include <igraph.h>
#include <igraph_progress.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// Worker Functions
// Each returns igraph_vector_int_t* (membership) on success, NULL on failure
// ============================================================================

// Multilevel (Louvain)
void *compute_com_multilevel(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_vector_int_t *membership = malloc(sizeof(igraph_vector_int_t));
	if (igraph_vector_int_init(membership, vcount) != IGRAPH_SUCCESS) {
		free(membership);
		return NULL;
	}

	igraph_vector_t modularity;
	igraph_vector_init(&modularity, 0);

	igraph_error_t code = igraph_community_multilevel(graph, NULL, 1.0, membership, NULL, &modularity);

	igraph_vector_destroy(&modularity);

	if (code != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(membership);
		free(membership);
		return NULL;
	}
	return membership;
}

// Leiden
void *compute_com_leiden(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_vector_int_t *membership = malloc(sizeof(igraph_vector_int_t));
	if (igraph_vector_int_init(membership, vcount) != IGRAPH_SUCCESS) {
		free(membership);
		return NULL;
	}

	igraph_int_t nb_clusters;
	igraph_real_t quality;

	// Use a resolution parameter based on graph density (as in graph_clustering.c)
	igraph_real_t resolution = 1.0 / (2.0 * (igraph_real_t)igraph_ecount(graph));

	igraph_error_t code;
	if (igraph_is_directed(graph)) {
		// For directed graphs, use full leiden with vertex weights
		code = igraph_community_leiden(graph, NULL, NULL, NULL, resolution, 0.01, 1, 100, membership, &nb_clusters, &quality);
	} else {
		// For undirected graphs, use leiden_simple without vertex weights
		code = igraph_community_leiden_simple(graph, NULL, IGRAPH_LEIDEN_OBJECTIVE_CPM, resolution, 0.01, 1, 100, membership, &nb_clusters, &quality);
	}

	if (code != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(membership);
		free(membership);
		return NULL;
	}
	return membership;
}

// Walktrap
void *compute_com_walktrap(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_vector_int_t *membership = malloc(sizeof(igraph_vector_int_t));
	if (igraph_vector_int_init(membership, vcount) != IGRAPH_SUCCESS) {
		free(membership);
		return NULL;
	}

	igraph_matrix_int_t merges;
	igraph_vector_t modularity;
	igraph_matrix_int_init(&merges, 0, 0);
	igraph_vector_init(&modularity, 0);

	igraph_error_t code = igraph_community_walktrap(graph, NULL, 4, &merges, &modularity, membership);

	igraph_matrix_int_destroy(&merges);
	igraph_vector_destroy(&modularity);

	if (code != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(membership);
		free(membership);
		return NULL;
	}
	return membership;
}

// Edge Betweenness (Girvan-Newman)
void *compute_com_edge_betweenness(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_vector_int_t *membership = malloc(sizeof(igraph_vector_int_t));
	if (igraph_vector_int_init(membership, vcount) != IGRAPH_SUCCESS) {
		free(membership);
		return NULL;
	}

	igraph_vector_int_t removed_edges;
	igraph_vector_t edge_betweenness;
	igraph_matrix_int_t merges;
	igraph_vector_int_t bridges;
	igraph_vector_t modularity;
	igraph_vector_int_init(&removed_edges, 0);
	igraph_vector_init(&edge_betweenness, 0);
	igraph_matrix_int_init(&merges, 0, 0);
	igraph_vector_int_init(&bridges, 0);
	igraph_vector_init(&modularity, 0);

	igraph_error_t code = igraph_community_edge_betweenness(graph, &removed_edges, &edge_betweenness, &merges, &bridges, &modularity, membership, igraph_is_directed(graph), NULL, NULL);

	igraph_vector_int_destroy(&removed_edges);
	igraph_vector_destroy(&edge_betweenness);
	igraph_matrix_int_destroy(&merges);
	igraph_vector_int_destroy(&bridges);
	igraph_vector_destroy(&modularity);

	if (code != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(membership);
		free(membership);
		return NULL;
	}
	return membership;
}

// Fast Greedy
void *compute_com_fastgreedy(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_vector_int_t *membership = malloc(sizeof(igraph_vector_int_t));
	if (igraph_vector_int_init(membership, vcount) != IGRAPH_SUCCESS) {
		free(membership);
		return NULL;
	}

	igraph_matrix_int_t merges;
	igraph_vector_t modularity;
	igraph_matrix_int_init(&merges, 0, 0);
	igraph_vector_init(&modularity, 0);

	igraph_error_t code = igraph_community_fastgreedy(graph, NULL, &merges, &modularity, membership);

	igraph_matrix_int_destroy(&merges);
	igraph_vector_destroy(&modularity);

	if (code != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(membership);
		free(membership);
		return NULL;
	}
	return membership;
}

// Infomap
void *compute_com_infomap(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_vector_int_t *membership = malloc(sizeof(igraph_vector_int_t));
	if (igraph_vector_int_init(membership, vcount) != IGRAPH_SUCCESS) {
		free(membership);
		return NULL;
	}

	igraph_real_t codelength;

	igraph_error_t code = igraph_community_infomap(graph, NULL, NULL, 10, 0, 0.0, membership, &codelength);

	if (code != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(membership);
		free(membership);
		return NULL;
	}
	return membership;
}

// Label Propagation
void *compute_com_label_prop(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_vector_int_t *membership = malloc(sizeof(igraph_vector_int_t));
	if (igraph_vector_int_init(membership, vcount) != IGRAPH_SUCCESS) {
		free(membership);
		return NULL;
	}

	igraph_error_t code = igraph_community_label_propagation(graph, membership, IGRAPH_ALL, NULL, NULL, NULL, IGRAPH_LPA_FAST);

	if (code != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(membership);
		free(membership);
		return NULL;
	}
	return membership;
}

// Spinglass
void *compute_com_spinglass(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_vector_int_t *membership = malloc(sizeof(igraph_vector_int_t));
	if (igraph_vector_int_init(membership, vcount) != IGRAPH_SUCCESS) {
		free(membership);
		return NULL;
	}

	igraph_real_t modularity;
	igraph_real_t temperature;
	igraph_vector_int_t csize;
	igraph_vector_int_init(&csize, 0);

	// Parameters: weights, modularity, temperature, membership, csize, spins, parupdate, starttemp, stoptemp, coolfact, update_rule, gamma, implementation, gamma_minus
	// Use number of vertices as number of spins, must be at least 2
	igraph_int_t spins = vcount > 2 ? vcount : 2;
	igraph_error_t code = igraph_community_spinglass(graph, NULL, &modularity, &temperature, membership, &csize, spins, 0, 1.0, 0.01, 0.5, IGRAPH_SPINCOMM_UPDATE_SIMPLE, 1.0, IGRAPH_SPINCOMM_IMP_ORIG, 0.5);

	igraph_vector_int_destroy(&csize);

	if (code != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(membership);
		free(membership);
		return NULL;
	}
	return membership;
}

// Leading Eigenvector
void *compute_com_leading_eigenvector(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_vector_int_t *membership = malloc(sizeof(igraph_vector_int_t));
	if (igraph_vector_int_init(membership, vcount) != IGRAPH_SUCCESS) {
		free(membership);
		return NULL;
	}

	igraph_matrix_int_t merges;
	igraph_vector_t eigenvalues;
	igraph_vector_list_t eigenvectors;
	igraph_vector_int_t history;
	igraph_matrix_int_init(&merges, 0, 0);
	igraph_vector_init(&eigenvalues, 0);
	igraph_vector_list_init(&eigenvectors, 0);
	igraph_vector_int_init(&history, 0);

	igraph_arpack_options_t options;
	igraph_arpack_options_init(&options);

	igraph_real_t modularity;
	igraph_bool_t start = 0;

	igraph_error_t code = igraph_community_leading_eigenvector(graph, NULL, &merges, membership, 100, &options, &modularity, start, &eigenvalues, &eigenvectors, &history, NULL, NULL);

	igraph_matrix_int_destroy(&merges);
	igraph_vector_destroy(&eigenvalues);
	igraph_vector_list_destroy(&eigenvectors);
	igraph_vector_int_destroy(&history);

	if (code != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(membership);
		free(membership);
		return NULL;
	}
	return membership;
}

// Optimal Modularity
void *compute_com_optimal_modularity(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_vector_int_t *membership = malloc(sizeof(igraph_vector_int_t));
	if (igraph_vector_int_init(membership, vcount) != IGRAPH_SUCCESS) {
		free(membership);
		return NULL;
	}

	igraph_real_t modularity;

	igraph_error_t code = igraph_community_optimal_modularity(graph, NULL, 1.0, &modularity, membership);

	if (code != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(membership);
		free(membership);
		return NULL;
	}
	return membership;
}

// Voronoi
void *compute_com_voronoi(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_vector_int_t *membership = malloc(sizeof(igraph_vector_int_t));
	if (igraph_vector_int_init(membership, vcount) != IGRAPH_SUCCESS) {
		free(membership);
		return NULL;
	}

	igraph_vector_int_t generators;
	igraph_real_t modularity;
	igraph_vector_int_init(&generators, 0);

	igraph_error_t code = igraph_community_voronoi(graph, membership, &generators, &modularity, NULL, NULL, IGRAPH_ALL, -1.0);

	igraph_vector_int_destroy(&generators);

	if (code != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(membership);
		free(membership);
		return NULL;
	}
	return membership;
}

// Fluid Communities
void *compute_com_fluid(igraph_t *graph)
{
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_vector_int_t *membership = malloc(sizeof(igraph_vector_int_t));
	if (igraph_vector_int_init(membership, vcount) != IGRAPH_SUCCESS) {
		free(membership);
		return NULL;
	}

	// Estimate number of communities: roughly sqrt(n/2) but ensure at least 1 and less than n
	igraph_int_t ncomm = vcount > 1 ? (igraph_int_t)sqrt(vcount / 2.0) + 1 : 1;
	if (ncomm >= vcount)
		ncomm = vcount - 1;
	if (ncomm < 1)
		ncomm = 1;

	igraph_error_t code = igraph_community_fluid_communities(graph, ncomm, membership);

	if (code != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(membership);
		free(membership);
		return NULL;
	}
	return membership;
}

// ============================================================================
// Apply and Free Functions
// ============================================================================

void apply_community_membership(ExecutionContext *ctx, void *result_data)
{
	if (!ctx || !ctx->app_state || !ctx->current_graph || !result_data) {
		fprintf(stderr, "[apply_community_membership] Error: Invalid parameters\n");
		return;
	}

	AppState *state = ctx->app_state;
	GraphData *data = &state->current_graph;
	Renderer *renderer = &state->renderer;
	igraph_vector_int_t *membership = (igraph_vector_int_t *)result_data;

	if (!data->graph_initialized) {
		fprintf(stderr, "[apply_community_membership] Error: Graph not initialized\n");
		return;
	}

	if (igraph_vector_int_size(membership) != data->node_count) {
		fprintf(stderr, "[apply_community_membership] Error: Membership size doesn't match node count\n");
		return;
	}

	// Calculate cluster count and sizes
	int cluster_count = 0;
	for (int i = 0; i < data->node_count; i++) {
		int comm = VECTOR(*membership)[i];
		if (comm > cluster_count)
			cluster_count = comm;
	}
	cluster_count++;

	int *cluster_sizes = calloc(cluster_count, sizeof(int));
	for (int i = 0; i < data->node_count; i++) {
		int comm = VECTOR(*membership)[i];
		if (comm < cluster_count)
			cluster_sizes[comm]++;
	}

	int max_cluster_size = 0;
	for (int i = 0; i < cluster_count; i++) {
		if (cluster_sizes[i] > max_cluster_size)
			max_cluster_size = cluster_sizes[i];
	}

	// Generate distinct colors for each community using golden ratio
	vec3 *colors = malloc(sizeof(vec3) * cluster_count);
	for (int i = 0; i < cluster_count; i++) {
		float hue = (float)i * 0.618033988749895f;
		hue -= floor(hue);
		colors[i][0] = hue;
		colors[i][1] = 0.8f;
		colors[i][2] = 0.9f;
	}

	// Apply colors to nodes (convert HSV-like to RGB)
	for (int i = 0; i < data->node_count; i++) {
		int comm = VECTOR(*membership)[i];
		if (comm < cluster_count) {
			float h = colors[comm][0] * 6.0f;
			int hi = (int)floor(h);
			float f = h - hi;
			float r = (hi == 0 || hi == 5) ? 1.0f : (hi == 1 || hi == 2) ? 1.0f - f : f;
			float g = (hi == 0 || hi == 3) ? f : (hi == 1 || hi == 2) ? 1.0f : 1.0f - f;
			float b = (hi == 0 || hi == 4) ? 1.0f - f : (hi == 2 || hi == 3) ? f : 1.0f;
			data->nodes[i].color[0] = r;
			data->nodes[i].color[1] = g;
			data->nodes[i].color[2] = b;
			data->nodes[i].glow = (max_cluster_size > 0) ? (float)cluster_sizes[comm] / (float)max_cluster_size : 0.0f;
		}
	}

	free(colors);
	free(cluster_sizes);

	// Refresh renderer
	renderer_update_graph(renderer, data);

	printf("[apply_community_membership] Communities applied - %d communities found\n", cluster_count);
}

void free_community_membership(void *result_data)
{
	if (result_data) {
		igraph_vector_int_destroy((igraph_vector_int_t *)result_data);
		free(result_data);
	}
}
