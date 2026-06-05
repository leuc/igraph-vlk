#define _GNU_SOURCE
#include "graph/graph_layout.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "graph/graph_core.h"
#include "graph/graph_layout.h"

void graph_layout_step(GraphData *data, LayoutType type, int iterations)
{
	if (!data->graph_initialized)
		return;
	data->active_layout = type;
	switch (type) {
	case LAYOUT_FR_3D:
		igraph_layout_fruchterman_reingold_3d(&data->g, &data->current_layout, 1, iterations, (igraph_real_t)data->node_count, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
		break;
	case LAYOUT_KK_3D:
		igraph_layout_kamada_kawai_3d(&data->g, &data->current_layout, 1, data->node_count * 10, 0.0, (igraph_real_t)data->node_count, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
		break;
	case LAYOUT_RANDOM_3D:
		igraph_layout_random_3d(&data->g, &data->current_layout);
		break;
	case LAYOUT_SPHERE:
		igraph_layout_sphere(&data->g, &data->current_layout);
		break;
	case LAYOUT_GRID_3D: {
		int side = (int)ceil(pow(data->node_count, 1.0 / 3.0));
		igraph_layout_grid_3d(&data->g, &data->current_layout, side, side);
		break;
	}
	case LAYOUT_UMAP_3D:
		igraph_layout_umap_3d(&data->g, &data->current_layout, 1, NULL, 0.1, iterations, 0);
		break;
	case LAYOUT_DRL_3D: {
		igraph_layout_drl_options_t options;
		igraph_layout_drl_options_init(&options, IGRAPH_LAYOUT_DRL_DEFAULT);
		igraph_layout_drl_3d(&data->g, &data->current_layout, 0, &options, NULL);
		break;
	}
	}
	graph_sync_node_positions(data);
}
