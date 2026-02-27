#define _GNU_SOURCE
#include "graph/graph_filter.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "graph/graph_core.h"

void graph_filter_degree(GraphData *data, int min_degree) {
	if (!data->graph_initialized)
		return;
	igraph_vector_int_t vids;
	igraph_vector_int_init(&vids, 0);
	igraph_vector_int_t degrees;
	igraph_vector_int_init(&degrees, 0);
	igraph_degree(&data->g, &degrees, igraph_vss_all(), IGRAPH_ALL,
				  IGRAPH_LOOPS);

	for (int i = 0; i < igraph_vector_int_size(&degrees); i++) {
		if (VECTOR(degrees)[i] < min_degree) {
			igraph_vector_int_push_back(&vids, i);
		}
	}

	if (igraph_vector_int_size(&vids) > 0) {
		printf("Filtering nodes with degree < %d. Removing %d nodes...\n",
			   min_degree, (int)igraph_vector_int_size(&vids));
		igraph_delete_vertices(&data->g, igraph_vss_vector(&vids));

		// Cleanup graph
		igraph_simplify(&data->g, 1, 1, NULL);

		// Reset layout for new graph size
		igraph_matrix_destroy(&data->current_layout);
		igraph_matrix_init(&data->current_layout, 0, 0);
		int side = (int)ceil(pow(igraph_vcount(&data->g), 1.0 / 3.0));
		igraph_layout_grid_3d(&data->g, &data->current_layout, side, side);
		graph_refresh_data(data);
	}
	igraph_vector_int_destroy(&vids);
	igraph_vector_int_destroy(&degrees);
}

void graph_filter_coreness(GraphData *data, int min_coreness) {
	if (!data->graph_initialized)
		return;
	igraph_vector_int_t vids;
	igraph_vector_int_init(&vids, 0);
	igraph_vector_int_t coreness;
	igraph_vector_int_init(&coreness, 0);
	igraph_coreness(&data->g, &coreness, IGRAPH_ALL);

	for (int i = 0; i < igraph_vector_int_size(&coreness); i++) {
		if (VECTOR(coreness)[i] < min_coreness) {
			igraph_vector_int_push_back(&vids, i);
		}
	}

	if (igraph_vector_int_size(&vids) > 0) {
		printf("Filtering nodes with coreness < %d. Removing %d nodes...\n",
			   min_coreness, (int)igraph_vector_int_size(&vids));
		igraph_delete_vertices(&data->g, igraph_vss_vector(&vids));
		igraph_simplify(&data->g, 1, 1, NULL);
		data->props.coreness_filter = min_coreness;

		// Reset layout for new graph size
		igraph_matrix_destroy(&data->current_layout);
		igraph_matrix_init(&data->current_layout, 0, 0);
		int side = (int)ceil(pow(igraph_vcount(&data->g), 1.0 / 3.0));
		igraph_layout_grid_3d(&data->g, &data->current_layout, side, side);
		graph_refresh_data(data);

		// Set glow based on coreness
		int max_core = 0;
		for (int i = 0; i < data->node_count; i++)
			if (data->nodes[i].coreness > max_core)
				max_core = data->nodes[i].coreness;
		for (int i = 0; i < data->node_count; i++) {
			if (max_core > 0)
				data->nodes[i].glow =
					(float)data->nodes[i].coreness / (float)max_core;
			else
				data->nodes[i].glow = 0.5f;
		}
	}
	igraph_vector_int_destroy(&vids);
	igraph_vector_int_destroy(&coreness);
}

void graph_highlight_infrastructure(GraphData *data) {
	if (!data->graph_initialized)
		return;

	// Reset glow
	for (int i = 0; i < data->node_count; i++)
		data->nodes[i].glow = 0.0f;

	// Articulation points
	igraph_vector_int_t ap;
	igraph_vector_int_init(&ap, 0);
	igraph_articulation_points(&data->g, &ap);
	for (int i = 0; i < igraph_vector_int_size(&ap); i++) {
		int v_idx = VECTOR(ap)[i];
		data->nodes[v_idx].glow = 1.0f;
		data->nodes[v_idx].color[0] = 1.0f;
		data->nodes[v_idx].color[1] = 0.2f;
		data->nodes[v_idx].color[2] = 0.2f;
	}
	igraph_vector_int_destroy(&ap);

	// Bridges
	igraph_vector_int_t bridges;
	igraph_vector_int_init(&bridges, 0);
	igraph_bridges(&data->g, &bridges);
	for (int i = 0; i < igraph_vector_int_size(&bridges); i++) {
		igraph_integer_t from, to;
		igraph_edge(&data->g, VECTOR(bridges)[i], &from, &to);
		data->nodes[from].glow = 1.0f;
		data->nodes[to].glow = 1.0f;
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

void graph_generate_hubs(GraphData *data, int num_hubs) {
	if (data->edge_count == 0)
		return;
	data->hub_count = num_hubs;
	data->hubs = realloc(data->hubs, sizeof(Hub) * num_hubs);

	// Init hubs randomly to node positions
	for (int i = 0; i < num_hubs; i++) {
		int n = rand() % data->node_count;
		memcpy(data->hubs[i].position, data->nodes[n].position,
			   sizeof(float) * 3);
	}

	// K-Means Clustering on Edge Midpoints (10 Iterations)
	int *counts = calloc(num_hubs, sizeof(int));
	float *sums = calloc(num_hubs * 3, sizeof(float));

	for (int iter = 0; iter < 10; iter++) {
		memset(counts, 0, sizeof(int) * num_hubs);
		memset(sums, 0, sizeof(float) * num_hubs * 3);

		for (uint32_t i = 0; i < data->edge_count; i++) {
			float mid[3] = {(data->nodes[data->edges[i].from].position[0] +
							 data->nodes[data->edges[i].to].position[0]) *
								0.5f,
							(data->nodes[data->edges[i].from].position[1] +
							 data->nodes[data->edges[i].to].position[1]) *
								0.5f,
							(data->nodes[data->edges[i].from].position[2] +
							 data->nodes[data->edges[i].to].position[2]) *
								0.5f};
			int best_hub = 0;
			float best_dist = 1e9f;
			for (int h = 0; h < num_hubs; h++) {
				float dx = mid[0] - data->hubs[h].position[0];
				float dy = mid[1] - data->hubs[h].position[1];
				float dz = mid[2] - data->hubs[h].position[2];
				float d = dx * dx + dy * dy + dz * dz;
				if (d < best_dist) {
					best_dist = d;
					best_hub = h;
				}
			}
			sums[best_hub * 3 + 0] += mid[0];
			sums[best_hub * 3 + 1] += mid[1];
			sums[best_hub * 3 + 2] += mid[2];
			counts[best_hub]++;
		}
		for (int h = 0; h < num_hubs; h++) {
			if (counts[h] > 0) {
				data->hubs[h].position[0] = sums[h * 3 + 0] / counts[h];
				data->hubs[h].position[1] = sums[h * 3 + 1] / counts[h];
				data->hubs[h].position[2] = sums[h * 3 + 2] / counts[h];
			}
		}
	}
	free(counts);
	free(sums);
}
