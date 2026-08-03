/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "graph/wrappers_community.h"
#include "app_state.h"
#include "graph/graph_color.h"
#include "interaction/state.h"
#include "vulkan/renderer.h"
#include "vulkan/renderer_anim.h"
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
void *compute_igraph_community_multilevel(ExecutionContext *ctx)
{
	igraph_t *graph = &ctx->app_state->current_graph.g;
	if (!ctx->app_state->current_graph.graph_initialized) {
		fprintf(stderr, "[%s] Graph not initialized\n", __func__);
		return NULL;
	}
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_vector_int_t *membership = IGRAPH_MALLOC(sizeof(igraph_vector_int_t));
	if (igraph_vector_int_init(membership, vcount) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(membership);
		return NULL;
	}

	if (graph_cache_load_vertex_attr_int(graph, "community-louvain", membership))
		return membership;

	igraph_vector_t modularity;
	if (igraph_vector_init(&modularity, 0) != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(membership);
		IGRAPH_FREE(membership);
		return NULL;
	}

	igraph_vector_t weights;
	bool has_weights = graph_build_edge_weights(graph, &weights);

	igraph_error_t code = igraph_community_multilevel(graph, has_weights ? &weights : NULL, 1.0, membership, NULL, &modularity);

	if (has_weights)
		igraph_vector_destroy(&weights);

	igraph_vector_destroy(&modularity);

	if (code != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(membership);
		IGRAPH_FREE(membership);
		return NULL;
	}
	graph_cache_store_vertex_attr_int(graph, "community-louvain", membership);
	return membership;
}

// Leiden
void *compute_igraph_community_leiden(ExecutionContext *ctx)
{
	igraph_t *graph = &ctx->app_state->current_graph.g;
	if (!ctx->app_state->current_graph.graph_initialized) {
		fprintf(stderr, "[%s] Graph not initialized\n", __func__);
		return NULL;
	}
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_vector_int_t *membership = IGRAPH_MALLOC(sizeof(igraph_vector_int_t));
	if (igraph_vector_int_init(membership, vcount) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(membership);
		return NULL;
	}

	if (graph_cache_load_vertex_attr_int(graph, "community-leiden", membership))
		return membership;

	igraph_int_t nb_clusters;
	igraph_real_t quality;

	igraph_integer_t ecount = igraph_ecount(graph);
	double graph_density = (vcount > 1) ? (2.0 * ecount) / ((double)vcount * (vcount - 1)) : 0.0;
	double cpm_resolution = fmax(graph_density * 3.0, 0.001);

	igraph_vector_t weights;
	bool has_weights = graph_build_edge_weights(graph, &weights);

	igraph_error_t code;
	if (igraph_is_directed(graph)) {
		// For directed graphs, use full leiden with vertex weights
		code = igraph_community_leiden(graph, has_weights ? &weights : NULL, NULL, NULL, cpm_resolution, 0.01, 0, -1, membership, &nb_clusters, &quality);
	} else {
		// For undirected graphs, use leiden_simple without vertex weights
		code = igraph_community_leiden_simple(graph, has_weights ? &weights : NULL, IGRAPH_LEIDEN_OBJECTIVE_CPM, cpm_resolution, 0.01, 0, -1, membership, &nb_clusters, &quality);
	}

	if (has_weights)
		igraph_vector_destroy(&weights);

	if (code != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(membership);
		IGRAPH_FREE(membership);
		return NULL;
	}
	graph_cache_store_vertex_attr_int(graph, "community-leiden", membership);
	return membership;
}

// Walktrap
void *compute_igraph_community_walktrap(ExecutionContext *ctx)
{
	igraph_t *graph = &ctx->app_state->current_graph.g;
	if (!ctx->app_state->current_graph.graph_initialized) {
		fprintf(stderr, "[%s] Graph not initialized\n", __func__);
		return NULL;
	}
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_vector_int_t *membership = IGRAPH_MALLOC(sizeof(igraph_vector_int_t));
	if (igraph_vector_int_init(membership, vcount) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(membership);
		return NULL;
	}

	if (graph_cache_load_vertex_attr_int(graph, "community-walktrap", membership))
		return membership;

	igraph_matrix_int_t merges;
	if (igraph_matrix_int_init(&merges, 0, 0) != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(membership);
		IGRAPH_FREE(membership);
		return NULL;
	}
	igraph_vector_t modularity;
	if (igraph_vector_init(&modularity, 0) != IGRAPH_SUCCESS) {
		igraph_matrix_int_destroy(&merges);
		igraph_vector_int_destroy(membership);
		IGRAPH_FREE(membership);
		return NULL;
	}

	igraph_vector_t weights;
	bool has_weights = graph_build_edge_weights(graph, &weights);

	igraph_error_t code = igraph_community_walktrap(graph, has_weights ? &weights : NULL, 4, &merges, &modularity, membership);

	if (has_weights)
		igraph_vector_destroy(&weights);

	igraph_matrix_int_destroy(&merges);
	igraph_vector_destroy(&modularity);

	if (code != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(membership);
		IGRAPH_FREE(membership);
		return NULL;
	}
	graph_cache_store_vertex_attr_int(graph, "community-walktrap", membership);
	return membership;
}

// Edge Betweenness (Girvan-Newman)
void *compute_igraph_community_edge_betweenness(ExecutionContext *ctx)
{
	igraph_t *graph = &ctx->app_state->current_graph.g;
	if (!ctx->app_state->current_graph.graph_initialized) {
		fprintf(stderr, "[%s] Graph not initialized\n", __func__);
		return NULL;
	}
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_vector_int_t *membership = IGRAPH_MALLOC(sizeof(igraph_vector_int_t));
	if (igraph_vector_int_init(membership, vcount) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(membership);
		return NULL;
	}

	if (graph_cache_load_vertex_attr_int(graph, "community-edge-betweenness", membership))
		return membership;

	igraph_vector_int_t removed_edges;
	if (igraph_vector_int_init(&removed_edges, 0) != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(membership);
		IGRAPH_FREE(membership);
		return NULL;
	}
	igraph_vector_t edge_betweenness;
	if (igraph_vector_init(&edge_betweenness, 0) != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(&removed_edges);
		igraph_vector_int_destroy(membership);
		IGRAPH_FREE(membership);
		return NULL;
	}
	igraph_matrix_int_t merges;
	if (igraph_matrix_int_init(&merges, 0, 0) != IGRAPH_SUCCESS) {
		igraph_vector_destroy(&edge_betweenness);
		igraph_vector_int_destroy(&removed_edges);
		igraph_vector_int_destroy(membership);
		IGRAPH_FREE(membership);
		return NULL;
	}
	igraph_vector_int_t bridges;
	if (igraph_vector_int_init(&bridges, 0) != IGRAPH_SUCCESS) {
		igraph_matrix_int_destroy(&merges);
		igraph_vector_destroy(&edge_betweenness);
		igraph_vector_int_destroy(&removed_edges);
		igraph_vector_int_destroy(membership);
		IGRAPH_FREE(membership);
		return NULL;
	}
	igraph_vector_t modularity;
	if (igraph_vector_init(&modularity, 0) != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(&bridges);
		igraph_matrix_int_destroy(&merges);
		igraph_vector_destroy(&edge_betweenness);
		igraph_vector_int_destroy(&removed_edges);
		igraph_vector_int_destroy(membership);
		IGRAPH_FREE(membership);
		return NULL;
	}

	igraph_vector_t weights;
	bool has_weights = graph_build_edge_weights(graph, &weights);

	igraph_error_t code = igraph_community_edge_betweenness(graph, &removed_edges, &edge_betweenness, &merges, &bridges, &modularity, membership, igraph_is_directed(graph), has_weights ? &weights : NULL, NULL);

	if (has_weights)
		igraph_vector_destroy(&weights);

	igraph_vector_int_destroy(&removed_edges);
	igraph_vector_destroy(&edge_betweenness);
	igraph_matrix_int_destroy(&merges);
	igraph_vector_int_destroy(&bridges);
	igraph_vector_destroy(&modularity);

	if (code != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(membership);
		IGRAPH_FREE(membership);
		return NULL;
	}
	graph_cache_store_vertex_attr_int(graph, "community-edge-betweenness", membership);
	return membership;
}

// Fast Greedy
void *compute_igraph_community_fastgreedy(ExecutionContext *ctx)
{
	igraph_t *graph = &ctx->app_state->current_graph.g;
	if (!ctx->app_state->current_graph.graph_initialized) {
		fprintf(stderr, "[%s] Graph not initialized\n", __func__);
		return NULL;
	}
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_vector_int_t *membership = IGRAPH_MALLOC(sizeof(igraph_vector_int_t));
	if (igraph_vector_int_init(membership, vcount) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(membership);
		return NULL;
	}

	if (graph_cache_load_vertex_attr_int(graph, "community-fastgreedy", membership))
		return membership;

	igraph_matrix_int_t merges;
	if (igraph_matrix_int_init(&merges, 0, 0) != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(membership);
		IGRAPH_FREE(membership);
		return NULL;
	}
	igraph_vector_t modularity;
	if (igraph_vector_init(&modularity, 0) != IGRAPH_SUCCESS) {
		igraph_matrix_int_destroy(&merges);
		igraph_vector_int_destroy(membership);
		IGRAPH_FREE(membership);
		return NULL;
	}

	igraph_vector_t weights;
	bool has_weights = graph_build_edge_weights(graph, &weights);

	igraph_error_t code = igraph_community_fastgreedy(graph, has_weights ? &weights : NULL, &merges, &modularity, membership);

	if (has_weights)
		igraph_vector_destroy(&weights);

	igraph_matrix_int_destroy(&merges);
	igraph_vector_destroy(&modularity);

	if (code != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(membership);
		IGRAPH_FREE(membership);
		return NULL;
	}
	graph_cache_store_vertex_attr_int(graph, "community-fastgreedy", membership);
	return membership;
}

// Infomap
void *compute_igraph_community_infomap(ExecutionContext *ctx)
{
	igraph_t *graph = &ctx->app_state->current_graph.g;
	if (!ctx->app_state->current_graph.graph_initialized) {
		fprintf(stderr, "[%s] Graph not initialized\n", __func__);
		return NULL;
	}
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_vector_int_t *membership = IGRAPH_MALLOC(sizeof(igraph_vector_int_t));
	if (igraph_vector_int_init(membership, vcount) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(membership);
		return NULL;
	}

	if (graph_cache_load_vertex_attr_int(graph, "community-infomap", membership))
		return membership;

	igraph_real_t codelength;

	igraph_vector_t weights;
	bool has_weights = graph_build_edge_weights(graph, &weights);

	igraph_error_t code = igraph_community_infomap(graph, has_weights ? &weights : NULL, NULL, 10, 0, 0.0, membership, &codelength);

	if (has_weights)
		igraph_vector_destroy(&weights);

	if (code != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(membership);
		IGRAPH_FREE(membership);
		return NULL;
	}
	graph_cache_store_vertex_attr_int(graph, "community-infomap", membership);
	return membership;
}

// Label Propagation
void *compute_igraph_community_label_propagation(ExecutionContext *ctx)
{
	igraph_t *graph = &ctx->app_state->current_graph.g;
	if (!ctx->app_state->current_graph.graph_initialized) {
		fprintf(stderr, "[%s] Graph not initialized\n", __func__);
		return NULL;
	}
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_vector_int_t *membership = IGRAPH_MALLOC(sizeof(igraph_vector_int_t));
	if (igraph_vector_int_init(membership, vcount) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(membership);
		return NULL;
	}

	if (graph_cache_load_vertex_attr_int(graph, "community-label-propagation", membership))
		return membership;

	igraph_vector_t weights;
	bool has_weights = graph_build_edge_weights(graph, &weights);

	igraph_error_t code = igraph_community_label_propagation(graph, membership, IGRAPH_ALL, has_weights ? &weights : NULL, NULL, NULL, IGRAPH_LPA_FAST);

	if (has_weights)
		igraph_vector_destroy(&weights);

	if (code != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(membership);
		IGRAPH_FREE(membership);
		return NULL;
	}
	graph_cache_store_vertex_attr_int(graph, "community-label-propagation", membership);
	return membership;
}

// Spinglass
void *compute_igraph_community_spinglass(ExecutionContext *ctx)
{
	igraph_t *graph = &ctx->app_state->current_graph.g;
	if (!ctx->app_state->current_graph.graph_initialized) {
		fprintf(stderr, "[%s] Graph not initialized\n", __func__);
		return NULL;
	}
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_vector_int_t *membership = IGRAPH_MALLOC(sizeof(igraph_vector_int_t));
	if (igraph_vector_int_init(membership, vcount) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(membership);
		return NULL;
	}

	if (graph_cache_load_vertex_attr_int(graph, "community-spinglass", membership))
		return membership;

	igraph_real_t modularity;
	igraph_real_t temperature;
	igraph_vector_int_t csize;
	if (igraph_vector_int_init(&csize, 0) != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(membership);
		IGRAPH_FREE(membership);
		return NULL;
	}

	// Parameters: weights, modularity, temperature, membership, csize, spins, parupdate, starttemp, stoptemp, coolfact, update_rule, gamma, implementation, gamma_minus
	// Use number of vertices as number of spins, must be at least 2
	igraph_int_t spins = vcount > 2 ? vcount : 2;

	igraph_vector_t weights;
	bool has_weights = graph_build_edge_weights(graph, &weights);

	igraph_error_t code = igraph_community_spinglass(graph, has_weights ? &weights : NULL, &modularity, &temperature, membership, &csize, spins, 0, 1.0, 0.01, 0.5, IGRAPH_SPINCOMM_UPDATE_SIMPLE, 1.0, IGRAPH_SPINCOMM_IMP_ORIG, 0.5);

	if (has_weights)
		igraph_vector_destroy(&weights);

	igraph_vector_int_destroy(&csize);

	if (code != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(membership);
		IGRAPH_FREE(membership);
		return NULL;
	}
	graph_cache_store_vertex_attr_int(graph, "community-spinglass", membership);
	return membership;
}

// Leading Eigenvector
void *compute_igraph_community_leading_eigenvector(ExecutionContext *ctx)
{
	igraph_t *graph = &ctx->app_state->current_graph.g;
	if (!ctx->app_state->current_graph.graph_initialized) {
		fprintf(stderr, "[%s] Graph not initialized\n", __func__);
		return NULL;
	}
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_vector_int_t *membership = IGRAPH_MALLOC(sizeof(igraph_vector_int_t));
	if (igraph_vector_int_init(membership, vcount) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(membership);
		return NULL;
	}

	if (graph_cache_load_vertex_attr_int(graph, "community-leading-eigenvector", membership))
		return membership;

	igraph_matrix_int_t merges;
	if (igraph_matrix_int_init(&merges, 0, 0) != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(membership);
		IGRAPH_FREE(membership);
		return NULL;
	}
	igraph_vector_t eigenvalues;
	if (igraph_vector_init(&eigenvalues, 0) != IGRAPH_SUCCESS) {
		igraph_matrix_int_destroy(&merges);
		igraph_vector_int_destroy(membership);
		IGRAPH_FREE(membership);
		return NULL;
	}
	igraph_vector_list_t eigenvectors;
	if (igraph_vector_list_init(&eigenvectors, 0) != IGRAPH_SUCCESS) {
		igraph_vector_destroy(&eigenvalues);
		igraph_matrix_int_destroy(&merges);
		igraph_vector_int_destroy(membership);
		IGRAPH_FREE(membership);
		return NULL;
	}
	igraph_vector_int_t history;
	if (igraph_vector_int_init(&history, 0) != IGRAPH_SUCCESS) {
		igraph_vector_list_destroy(&eigenvectors);
		igraph_vector_destroy(&eigenvalues);
		igraph_matrix_int_destroy(&merges);
		igraph_vector_int_destroy(membership);
		IGRAPH_FREE(membership);
		return NULL;
	}

	igraph_arpack_options_t options;
	igraph_arpack_options_init(&options);

	igraph_real_t modularity;
	igraph_bool_t start = 0;

	igraph_vector_t weights;
	bool has_weights = graph_build_edge_weights(graph, &weights);

	igraph_error_t code = igraph_community_leading_eigenvector(graph, has_weights ? &weights : NULL, &merges, membership, 100, &options, &modularity, start, &eigenvalues, &eigenvectors, &history, NULL, NULL);

	if (has_weights)
		igraph_vector_destroy(&weights);

	igraph_matrix_int_destroy(&merges);
	igraph_vector_destroy(&eigenvalues);
	igraph_vector_list_destroy(&eigenvectors);
	igraph_vector_int_destroy(&history);

	if (code != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(membership);
		IGRAPH_FREE(membership);
		return NULL;
	}
	graph_cache_store_vertex_attr_int(graph, "community-leading-eigenvector", membership);
	return membership;
}

// Optimal Modularity
void *compute_igraph_community_optimal_modularity(ExecutionContext *ctx)
{
	igraph_t *graph = &ctx->app_state->current_graph.g;
	if (!ctx->app_state->current_graph.graph_initialized) {
		fprintf(stderr, "[%s] Graph not initialized\n", __func__);
		return NULL;
	}
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_vector_int_t *membership = IGRAPH_MALLOC(sizeof(igraph_vector_int_t));
	if (igraph_vector_int_init(membership, vcount) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(membership);
		return NULL;
	}

	if (graph_cache_load_vertex_attr_int(graph, "community-optimal-modularity", membership))
		return membership;

	igraph_real_t modularity;

	igraph_vector_t weights;
	bool has_weights = graph_build_edge_weights(graph, &weights);

	igraph_error_t code = igraph_community_optimal_modularity(graph, has_weights ? &weights : NULL, 1.0, &modularity, membership);

	if (has_weights)
		igraph_vector_destroy(&weights);

	if (code != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(membership);
		IGRAPH_FREE(membership);
		return NULL;
	}
	graph_cache_store_vertex_attr_int(graph, "community-optimal-modularity", membership);
	return membership;
}

// Voronoi
void *compute_igraph_community_voronoi(ExecutionContext *ctx)
{
	igraph_t *graph = &ctx->app_state->current_graph.g;
	if (!ctx->app_state->current_graph.graph_initialized) {
		fprintf(stderr, "[%s] Graph not initialized\n", __func__);
		return NULL;
	}
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_vector_int_t *membership = IGRAPH_MALLOC(sizeof(igraph_vector_int_t));
	if (igraph_vector_int_init(membership, vcount) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(membership);
		return NULL;
	}

	if (graph_cache_load_vertex_attr_int(graph, "community-voronoi", membership))
		return membership;

	igraph_vector_int_t generators;
	igraph_real_t modularity;
	if (igraph_vector_int_init(&generators, 0) != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(membership);
		IGRAPH_FREE(membership);
		return NULL;
	}

	igraph_vector_t weights;
	bool has_weights = graph_build_edge_weights(graph, &weights);

	igraph_error_t code = igraph_community_voronoi(graph, membership, &generators, &modularity, NULL, has_weights ? &weights : NULL, IGRAPH_ALL, -1.0);

	if (has_weights)
		igraph_vector_destroy(&weights);

	igraph_vector_int_destroy(&generators);

	if (code != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(membership);
		IGRAPH_FREE(membership);
		return NULL;
	}
	graph_cache_store_vertex_attr_int(graph, "community-voronoi", membership);
	return membership;
}

// Fluid Communities
void *compute_igraph_community_fluid_communities(ExecutionContext *ctx)
{
	igraph_t *graph = &ctx->app_state->current_graph.g;
	if (!ctx->app_state->current_graph.graph_initialized) {
		fprintf(stderr, "[%s] Graph not initialized\n", __func__);
		return NULL;
	}
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_vector_int_t *membership = IGRAPH_MALLOC(sizeof(igraph_vector_int_t));
	if (igraph_vector_int_init(membership, vcount) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(membership);
		return NULL;
	}

	if (graph_cache_load_vertex_attr_int(graph, "community-fluid", membership))
		return membership;

	// Estimate number of communities: roughly sqrt(n/2) but ensure at least 1 and less than n
	igraph_int_t ncomm = vcount > 1 ? (igraph_int_t)sqrt(vcount / 2.0) + 1 : 1;
	if (ncomm >= vcount)
		ncomm = vcount - 1;
	if (ncomm < 1)
		ncomm = 1;

	igraph_error_t code = igraph_community_fluid_communities(graph, ncomm, membership);

	if (code != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(membership);
		IGRAPH_FREE(membership);
		return NULL;
	}
	graph_cache_store_vertex_attr_int(graph, "community-fluid", membership);
	return membership;
}

// ============================================================================
// Apply and Free Functions
// ============================================================================

void community_id_to_rgb(igraph_integer_t comm_id, float out_rgb[3])
{
	float hue = (float)comm_id * 0.618033988749895f;
	hue -= floorf(hue);
	float h = hue * 6.0f;
	int hi = (int)floorf(h);
	float f = h - hi;
	out_rgb[0] = (hi == 0 || hi == 5) ? 1.0f : (hi == 1 || hi == 2) ? 1.0f - f : f;
	out_rgb[1] = (hi == 0 || hi == 3) ? f : (hi == 1 || hi == 2) ? 1.0f : 1.0f - f;
	out_rgb[2] = (hi == 0 || hi == 4) ? 1.0f - f : (hi == 2 || hi == 3) ? f : 1.0f;
}

void apply_community_membership(ExecutionContext *ctx, void *result_data)
{
	if (!ctx || !ctx->app_state || !result_data) {
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

	// Group never wants leftover dimming/reveal state from a prior Follow command
	graph_reset_emphasis(data);
	renderer_anim_reset_nodes(renderer, data);
	renderer_anim_reset_edges(renderer);

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

	for (int i = 0; i < data->node_count; i++) {
		int comm = VECTOR(*membership)[i];
		if (comm < cluster_count)
			community_id_to_rgb(comm, data->nodes[i].color);
	}

	free(cluster_sizes);

	// Mark attributes dirty and refresh renderer
	renderer->needsAttributeUpload = VK_TRUE;
	renderer_update_graph(renderer, data);

	printf("[apply_community_membership] Communities applied - %d communities found\n", cluster_count);
}

void free_community_membership(void *result_data)
{
	if (result_data) {
		igraph_vector_int_destroy((igraph_vector_int_t *)result_data);
		IGRAPH_FREE(result_data);
	}
}

// ============================================================================
// Every attribute name a Group command may have cached, so an in-place graph
// edit (Alter menu) can wipe them all and force the next Group click to
// recompute rather than reapply now-stale membership.
// ============================================================================
static const char *const GROUP_CACHED_ATTRS[] = {
	"community-louvain", "community-leiden", "community-walktrap", "community-edge-betweenness", "community-fastgreedy", "community-infomap", "community-label-propagation", "community-spinglass", "community-leading-eigenvector", "community-optimal-modularity", "community-voronoi", "community-fluid",
};

void community_clear_cached_attrs(igraph_t *graph)
{
	if (!graph)
		return;
	for (size_t i = 0; i < sizeof(GROUP_CACHED_ATTRS) / sizeof(GROUP_CACHED_ATTRS[0]); i++) {
		const char *name = GROUP_CACHED_ATTRS[i];
		if (igraph_cattribute_has_attr(graph, IGRAPH_ATTRIBUTE_VERTEX, name))
			DELVA(graph, name);
	}
}
