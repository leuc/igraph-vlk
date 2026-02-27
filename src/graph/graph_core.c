#define _GNU_SOURCE
#include "graph/graph_core.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "graph/layout_openord.h"
#include "graph/layered_sphere.h"

void graph_init(GraphData *data) {
	memset(data, 0, sizeof(GraphData));
	igraph_matrix_init(&data->current_layout, 0, 0);
}

void graph_sync_node_positions(GraphData *data) {
	if (!data->nodes)
		return;
	for (int i = 0; i < data->node_count; i++) {
		data->nodes[i].position[0] = (float)MATRIX(data->current_layout, i, 0);
		data->nodes[i].position[1] = (float)MATRIX(data->current_layout, i, 1);
		data->nodes[i].position[2] =
			(igraph_matrix_ncol(&data->current_layout) > 2)
				? (float)MATRIX(data->current_layout, i, 2)
				: 0.0f;
	}
}

void graph_refresh_data(GraphData *data) {
	data->node_count = igraph_vcount(&data->g);
	data->edge_count = igraph_ecount(&data->g);

	data->props.node_count = (int)data->node_count;
	data->props.edge_count = (int)data->edge_count;

	// Re-calculate basic node properties
	if (data->nodes) {
		for (uint32_t i = 0; i < data->node_count; i++)
			if (data->nodes[i].label)
				free(data->nodes[i].label);
		free(data->nodes);
	}
	if (data->edges)
		free(data->edges);

	data->nodes = malloc(sizeof(Node) * data->node_count);
	bool has_node_attr = igraph_cattribute_has_attr(
		&data->g, IGRAPH_ATTRIBUTE_VERTEX, data->node_attr_name);
	bool has_label =
		igraph_cattribute_has_attr(&data->g, IGRAPH_ATTRIBUTE_VERTEX, "label");
	float max_n_val = 0.0f;
	if (has_node_attr) {
		for (int i = 0; i < data->node_count; i++) {
			float val = (float)VAN(&data->g, data->node_attr_name, i);
			if (val > max_n_val)
				max_n_val = val;
		}
	}

	igraph_vector_int_t coreness;
	igraph_vector_int_init(&coreness, data->node_count);
	igraph_coreness(&data->g, &coreness, IGRAPH_ALL);

	for (int i = 0; i < data->node_count; i++) {
		data->nodes[i].color[0] = (float)rand() / RAND_MAX;
		data->nodes[i].color[1] = (float)rand() / RAND_MAX;
		data->nodes[i].color[2] = (float)rand() / RAND_MAX;
		data->nodes[i].size =
			(has_node_attr && max_n_val > 0)
				? (float)VAN(&data->g, data->node_attr_name, i) / max_n_val
				: 1.0f;
		data->nodes[i].label =
			has_label ? strdup(VAS(&data->g, "label", i)) : NULL;
		igraph_vector_int_t neighbors;
		igraph_vector_int_init(&neighbors, 0);
		igraph_neighbors(&data->g, &neighbors, i, IGRAPH_ALL);
		data->nodes[i].degree = igraph_vector_int_size(&neighbors);
		igraph_vector_int_destroy(&neighbors);
		data->nodes[i].coreness = VECTOR(coreness)[i];
		data->nodes[i].glow = 0.0f;
	}
	igraph_vector_int_destroy(&coreness);
	graph_sync_node_positions(data);

	bool has_edge_attr = igraph_cattribute_has_attr(
		&data->g, IGRAPH_ATTRIBUTE_EDGE, data->edge_attr_name);
	float max_e_val = 0.0f;
	if (has_edge_attr) {
		for (int i = 0; i < data->edge_count; i++) {
			float val = (float)EAN(&data->g, data->edge_attr_name, i);
			if (val > max_e_val)
				max_e_val = val;
		}
	}
	data->edges = malloc(sizeof(Edge) * data->edge_count);
	for (int i = 0; i < data->edge_count; i++) {
		igraph_integer_t from, to;
		igraph_edge(&data->g, i, &from, &to);
		data->edges[i].from = (uint32_t)from;
		data->edges[i].to = (uint32_t)to;
		data->edges[i].size =
			(has_edge_attr && max_e_val > 0)
				? (float)EAN(&data->g, data->edge_attr_name, i) / max_e_val
				: 1.0f;
	}
}

void graph_free_data(GraphData *data) {
	if (data->graph_initialized) {
		igraph_destroy(&data->g);
		igraph_matrix_destroy(&data->current_layout);
		data->graph_initialized = false;
	}
	if (data->openord) {
		openord_cleanup(data->openord);
		free(data->openord);
		data->openord = NULL;
	}
	if (data->layered_sphere) {
		layered_sphere_cleanup(data->layered_sphere);
		free(data->layered_sphere);
		data->layered_sphere = NULL;
	}
	if (data->node_attr_name)
		free(data->node_attr_name);
	if (data->edge_attr_name)
		free(data->edge_attr_name);
	if (data->hubs) {
		free(data->hubs);
		data->hubs = NULL;
	}
	for (uint32_t i = 0; i < data->node_count; i++)
		if (data->nodes[i].label)
			free(data->nodes[i].label);
	free(data->nodes);
	free(data->edges);
}
