#define _GNU_SOURCE
#include "graph/graph_io.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "graph/graph_core.h"

int graph_load_graphml(const char *filename, GraphData *data,
					   LayoutType layout_type, const char *node_attr,
					   const char *edge_attr) {
	igraph_set_attribute_table(&igraph_cattribute_table);
	FILE *fp = fopen(filename, "r");
	if (!fp)
		return -1;
	if (igraph_read_graph_graphml(&data->g, fp, 0) != IGRAPH_SUCCESS) {
		fclose(fp);
		return -1;
	}
	fclose(fp);
	igraph_simplify(&data->g, 1, 1, NULL);
	data->graph_initialized = true;
	data->node_attr_name = node_attr ? strdup(node_attr) : strdup("pagerank");
	data->edge_attr_name =
		edge_attr ? strdup(edge_attr) : strdup("betweenness");
	data->nodes = NULL;
	data->edges = NULL;
	data->hubs = NULL;
	data->hub_count = 0;

	igraph_matrix_init(&data->current_layout, 0, 0);
	if (layout_type == LAYOUT_OPENORD_3D || layout_type == LAYOUT_RANDOM_3D) {
		igraph_layout_random_3d(&data->g, &data->current_layout);
	} else {
		// Use grid layout as default
		int side = (int)ceil(pow(igraph_vcount(&data->g), 1.0 / 3.0));
		igraph_layout_grid_3d(&data->g, &data->current_layout, side, side);
	}
	data->active_layout = layout_type;

	graph_refresh_data(data);
	return 0;
}
