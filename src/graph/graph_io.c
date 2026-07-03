/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#define _GNU_SOURCE
#include "graph/graph_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "graph/graph_core.h"

bool graph_load_graphml(const char *filename, GraphData *data, const char *node_attr, const char *edge_attr)
{
	igraph_set_attribute_table(&igraph_cattribute_table);
	FILE *fp = fopen(filename, "r");
	if (!fp)
		return false;
	if (igraph_read_graph_graphml(&data->g, fp, 0) != IGRAPH_SUCCESS) {
		fclose(fp);
		return false;
	}
	fclose(fp);
	igraph_simplify(&data->g, 1, 1, NULL);
	data->graph_initialized = true;
	data->node_attr_name = node_attr ? strdup(node_attr) : strdup("pagerank");
	data->nodes = NULL;
	data->edges = NULL;
	data->hubs = NULL;
	data->hub_count = 0;

	if (!graph_import_layout_pos(data)) {
		igraph_matrix_init(&data->current_layout, 0, 0);
		igraph_layout_grid_3d(&data->g, &data->current_layout, 0, 0);
	}

	graph_build_visualization(data);
	return true;
}
