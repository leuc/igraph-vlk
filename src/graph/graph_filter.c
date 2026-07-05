/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#define _GNU_SOURCE
#include "graph/graph_filter.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "graph/graph_core.h"

void graph_filter_degree(GraphData *data, int min_degree)
{
	if (!data->graph_initialized)
		return;
	igraph_vector_int_t vids;
	if (igraph_vector_int_init(&vids, 0) != IGRAPH_SUCCESS)
		return;
	igraph_vector_int_t degrees;
	if (igraph_vector_int_init(&degrees, 0) != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(&vids);
		return;
	}
	igraph_degree(&data->g, &degrees, igraph_vss_all(), IGRAPH_ALL, IGRAPH_LOOPS);

	for (int i = 0; i < igraph_vector_int_size(&degrees); i++) {
		if (VECTOR(degrees)[i] < min_degree) {
			igraph_vector_int_push_back(&vids, i);
		}
	}

	if (igraph_vector_int_size(&vids) > 0) {
		printf("Filtering nodes with degree < %d. Removing %d nodes...\n", min_degree, (int)igraph_vector_int_size(&vids));
		igraph_delete_vertices(&data->g, igraph_vss_vector(&vids));

		// Cleanup graph
		igraph_simplify(&data->g, 1, 1, NULL);

		// Reset layout for new graph size
		igraph_matrix_destroy(&data->current_layout);
		igraph_matrix_init(&data->current_layout, 0, 0);
		int side = (int)ceil(pow(igraph_vcount(&data->g), 1.0 / 3.0));
		igraph_layout_grid_3d(&data->g, &data->current_layout, side, side);
		graph_build_visualization(data);
	}
	igraph_vector_int_destroy(&vids);
	igraph_vector_int_destroy(&degrees);
}

void graph_filter_coreness(GraphData *data, int min_coreness)
{
	if (!data->graph_initialized)
		return;
	igraph_vector_int_t vids;
	if (igraph_vector_int_init(&vids, 0) != IGRAPH_SUCCESS)
		return;
	igraph_vector_int_t coreness;
	if (igraph_vector_int_init(&coreness, 0) != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(&vids);
		return;
	}
	igraph_coreness(&data->g, &coreness, IGRAPH_ALL);

	for (int i = 0; i < igraph_vector_int_size(&coreness); i++) {
		if (VECTOR(coreness)[i] < min_coreness) {
			igraph_vector_int_push_back(&vids, i);
		}
	}

	if (igraph_vector_int_size(&vids) > 0) {
		printf("Filtering nodes with coreness < %d. Removing %d nodes...\n", min_coreness, (int)igraph_vector_int_size(&vids));
		igraph_delete_vertices(&data->g, igraph_vss_vector(&vids));
		igraph_simplify(&data->g, 1, 1, NULL);
		data->props.coreness_filter = min_coreness;

		// Reset layout for new graph size
		igraph_matrix_destroy(&data->current_layout);
		igraph_matrix_init(&data->current_layout, 0, 0);
		int side = (int)ceil(pow(igraph_vcount(&data->g), 1.0 / 3.0));
		igraph_layout_grid_3d(&data->g, &data->current_layout, side, side);
		graph_build_visualization(data);
	}
	igraph_vector_int_destroy(&vids);
	igraph_vector_int_destroy(&coreness);
}

void graph_highlight_infrastructure(GraphData *data)
{
	if (!data->graph_initialized)
		return;

	// Articulation points
	igraph_vector_int_t ap;
	if (igraph_vector_int_init(&ap, 0) != IGRAPH_SUCCESS)
		return;
	igraph_articulation_points(&data->g, &ap);
	for (int i = 0; i < igraph_vector_int_size(&ap); i++) {
		int v_idx = VECTOR(ap)[i];
		data->nodes[v_idx].color[0] = 1.0f;
		data->nodes[v_idx].color[1] = 0.2f;
		data->nodes[v_idx].color[2] = 0.2f;
	}
	igraph_vector_int_destroy(&ap);

	// Bridges
	igraph_vector_int_t bridges;
	if (igraph_vector_int_init(&bridges, 0) != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(&ap);
		return;
	}
	igraph_bridges(&data->g, &bridges);
	for (int i = 0; i < igraph_vector_int_size(&bridges); i++) {
		igraph_integer_t from, to;
		igraph_edge(&data->g, VECTOR(bridges)[i], &from, &to);
		// Optionally color nodes connected to bridges differently
		data->nodes[from].color[0] = 1.0f;
		data->nodes[from].color[1] = 0.5f;
		data->nodes[from].color[2] = 0.0f;
		data->nodes[to].color[0] = 1.0f;
		data->nodes[to].color[1] = 0.5f;
		data->nodes[to].color[2] = 0.0f;
	}
	igraph_vector_int_destroy(&bridges);
}
