#define _GNU_SOURCE
#include "graph/graph_core.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// Build the entire visualization arrays for a new graph.
// Caller intention: "I have a completely new graph — build everything."
// ============================================================================
void graph_build_visualization(GraphData *data)
{
	uint32_t old_node_count = data->node_count;

	data->node_count = igraph_vcount(&data->g);
	data->edge_count = igraph_ecount(&data->g);
	data->props.node_count = (int)data->node_count;
	data->props.edge_count = (int)data->edge_count;

	// Free old arrays
	if (data->nodes) {
		for (uint32_t i = 0; i < old_node_count; i++)
			if (data->nodes[i].label)
				free(data->nodes[i].label);
		free(data->nodes);
	}
	if (data->edges)
		free(data->edges);

	// Build nodes
	data->nodes = malloc(sizeof(Node) * data->node_count);
	bool has_node_attr = data->node_attr_name && igraph_cattribute_has_attr(&data->g, IGRAPH_ATTRIBUTE_VERTEX, data->node_attr_name);
	bool has_label = igraph_cattribute_has_attr(&data->g, IGRAPH_ATTRIBUTE_VERTEX, "label");
	float max_n_val = 0.0f;
	if (has_node_attr) {
		for (int i = 0; i < data->node_count; i++) {
			float val = (float)VAN(&data->g, data->node_attr_name, i);
			if (val > max_n_val)
				max_n_val = val;
		}
	}

	for (int i = 0; i < data->node_count; i++) {
		data->nodes[i].color[0] = (float)rand() / RAND_MAX;
		data->nodes[i].color[1] = (float)rand() / RAND_MAX;
		data->nodes[i].color[2] = (float)rand() / RAND_MAX;
		data->nodes[i].size = (has_node_attr && max_n_val > 0) ? (float)VAN(&data->g, data->node_attr_name, i) / max_n_val : 1.0f;
		data->nodes[i].label = has_label ? strdup(VAS(&data->g, "label", i)) : NULL;
		igraph_vector_int_t neighbors;
		igraph_vector_int_init(&neighbors, 0);
		igraph_neighbors(&data->g, &neighbors, i, IGRAPH_ALL, IGRAPH_NO_LOOPS, 1);
		data->nodes[i].degree = igraph_vector_int_size(&neighbors);
		igraph_vector_int_destroy(&neighbors);
	}

	// Sync node positions from layout matrix
	if (data->nodes) {
		for (int i = 0; i < data->node_count; i++) {
			data->nodes[i].position[0] = (float)MATRIX(data->current_layout, i, 0);
			data->nodes[i].position[1] = (float)MATRIX(data->current_layout, i, 1);
			data->nodes[i].position[2] = (igraph_matrix_ncol(&data->current_layout) > 2) ? (float)MATRIX(data->current_layout, i, 2) : 0.0f;
		}
	}

	// Build edges
	data->edges = malloc(sizeof(Edge) * data->edge_count);
	for (int i = 0; i < data->edge_count; i++) {
		igraph_integer_t from, to;
		igraph_edge(&data->g, i, &from, &to);
		data->edges[i].from = (uint32_t)from;
		data->edges[i].to = (uint32_t)to;
	}
}

// ============================================================================
// Rebuild only the edge array and node degrees after in-place edge changes.
// Caller intention: "Edges changed — update edges and degrees, leave nodes alone."
// ============================================================================
void graph_rebuild_edges(GraphData *data)
{
	data->edge_count = igraph_ecount(&data->g);
	data->props.edge_count = (int)data->edge_count;

	if (data->edges)
		free(data->edges);

	data->edges = malloc(sizeof(Edge) * data->edge_count);
	for (int i = 0; i < data->edge_count; i++) {
		igraph_integer_t from, to;
		igraph_edge(&data->g, i, &from, &to);
		data->edges[i].from = (uint32_t)from;
		data->edges[i].to = (uint32_t)to;
	}

	// Recompute degree on existing nodes — it affects node shape rendering
	for (uint32_t i = 0; i < data->node_count; i++) {
		igraph_vector_int_t neis;
		igraph_vector_int_init(&neis, 0);
		igraph_neighbors(&data->g, &neis, i, IGRAPH_ALL, IGRAPH_NO_LOOPS, 1);
		data->nodes[i].degree = igraph_vector_int_size(&neis);
		igraph_vector_int_destroy(&neis);
	}
}

// ============================================================================
// Free all graph data
// ============================================================================
void graph_free_data(GraphData *data)
{
	if (data->graph_initialized) {
		igraph_destroy(&data->g);
		igraph_matrix_destroy(&data->current_layout);
		data->graph_initialized = false;
	}
	if (data->node_attr_name) {
		free(data->node_attr_name);
		data->node_attr_name = NULL;
	}
	if (data->hubs) {
		free(data->hubs);
		data->hubs = NULL;
	}
	if (data->nodes) {
		for (uint32_t i = 0; i < data->node_count; i++)
			if (data->nodes[i].label)
				free(data->nodes[i].label);
		free(data->nodes);
		data->nodes = NULL;
	}
	if (data->edges) {
		free(data->edges);
		data->edges = NULL;
	}
}
