#define _GNU_SOURCE
#include "graph/graph_layout.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "graph/graph_core.h"
#include "graph/graph_layout.h"
#include "graph/layout_openord.h"
#include "graph/layered_sphere.h"

void graph_layout_step(GraphData *data, LayoutType type, int iterations) {
	if (!data->graph_initialized)
		return;
	data->active_layout = type;
	switch (type) {
	case LAYOUT_FR_3D:
		igraph_layout_fruchterman_reingold_3d(
			&data->g, &data->current_layout, 1, iterations,
			(igraph_real_t)data->node_count, NULL, NULL, NULL, NULL, NULL, NULL,
			NULL);
		break;
	case LAYOUT_KK_3D:
		igraph_layout_kamada_kawai_3d(&data->g, &data->current_layout, 1,
									  data->node_count * 10, 0.0,
									  (igraph_real_t)data->node_count, NULL,
									  NULL, NULL, NULL, NULL, NULL, NULL);
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
		igraph_layout_umap_3d(&data->g, &data->current_layout, 1, NULL, 0.1,
							  iterations, 0);
		break;
	case LAYOUT_DRL_3D: {
		igraph_layout_drl_options_t options;
		igraph_layout_drl_options_init(&options, IGRAPH_LAYOUT_DRL_DEFAULT);
		igraph_layout_drl_3d(&data->g, &data->current_layout, 0, &options,
							 NULL);
		break;
	}
	case LAYOUT_OPENORD_3D: {
		if (!data->openord) {
			data->openord = malloc(sizeof(OpenOrdContext));
			openord_init(data->openord, data->node_count, 128);
			igraph_layout_random_3d(&data->g, &data->current_layout);
		}
		for (int i = 0; i < iterations; i++) {
			if (!openord_step(data->openord, data))
				break;
		}
		break;
	}
	case LAYOUT_LAYERED_SPHERE: {
		if (!data->layered_sphere) {
			data->layered_sphere = malloc(sizeof(LayeredSphereContext));
			layered_sphere_init(data->layered_sphere, data->node_count);
		}
		for (int i = 0; i < iterations; i++) {
			if (!layered_sphere_step(data->layered_sphere, data))
				break;
		}
		break;
	}
	}
	graph_sync_node_positions(data);
}

void graph_remove_overlaps(GraphData *data, float layoutScale) {
	if (!data->graph_initialized || data->node_count == 0)
		return;
	float max_radius = 0.0f;
	vec3 min_p = {1e10, 1e10, 1e10}, max_p = {-1e10, -1e10, -1e10};
	for (int i = 0; i < data->node_count; i++) {
		float r = 0.5f * data->nodes[i].size;
		if (r > max_radius)
			max_radius = r;
		for (int j = 0; j < 3; j++) {
			if (data->nodes[i].position[j] < min_p[j])
				min_p[j] = data->nodes[i].position[j];
			if (data->nodes[i].position[j] > max_p[j])
				max_p[j] = data->nodes[i].position[j];
		}
	}
	float cellSize = (max_radius * 2.0f) + 0.1f;
	int dimX = (int)((max_p[0] - min_p[0]) / cellSize) + 1;
	int dimY = (int)((max_p[1] - min_p[1]) / cellSize) + 1;
	int dimZ = (int)((max_p[2] - min_p[2]) / cellSize) + 1;
	if (dimX * dimY * dimZ > 1000000)
		cellSize *= 2.0f;
	int totalCells = dimX * dimY * dimZ;
	int *head = malloc(sizeof(int) * totalCells);
	int *next = malloc(sizeof(int) * data->node_count);
	memset(head, -1, sizeof(int) * totalCells);
	for (int i = 0; i < data->node_count; i++) {
		int cx = (int)((data->nodes[i].position[0] - min_p[0]) / cellSize);
		int cy = (int)((data->nodes[i].position[1] - min_p[1]) / cellSize);
		int cz = (int)((data->nodes[i].position[2] - min_p[2]) / cellSize);
		int cellIdx = cx + cy * dimX + cz * dimX * dimY;
		if (cellIdx >= 0 && cellIdx < totalCells) {
			next[i] = head[cellIdx];
			head[cellIdx] = i;
		}
	}
	for (int i = 0; i < data->node_count; i++) {
		int cx = (int)((data->nodes[i].position[0] - min_p[0]) / cellSize);
		int cy = (int)((data->nodes[i].position[1] - min_p[1]) / cellSize);
		int cz = (int)((data->nodes[i].position[2] - min_p[2]) / cellSize);
		for (int nx = cx - 1; nx <= cx + 1; nx++) {
			for (int ny = cy - 1; ny <= cy + 1; ny++) {
				for (int nz = cz - 1; nz <= cz + 1; nz++) {
					if (nx < 0 || nx >= dimX || ny < 0 || ny >= dimY ||
						nz < 0 || nz >= dimZ)
						continue;
					int cellIdx = nx + ny * dimX + nz * dimX * dimY;
					for (int j = head[cellIdx]; j >= 0; j = next[j]) {
						if (i == j)
							continue;
						float dx = data->nodes[i].position[0] -
								   data->nodes[j].position[0];
						float dy = data->nodes[i].position[1] -
								   data->nodes[j].position[1];
						float dz = data->nodes[i].position[2] -
								   data->nodes[j].position[2];
						float distSq = dx * dx + dy * dy + dz * dz;
						float minDist =
							0.5f * (data->nodes[i].size + data->nodes[j].size);
						if (distSq < minDist * minDist && distSq > 0.0001f) {
							float dist = sqrtf(distSq);
							float overlap = 0.5f * (minDist - dist);
							dx /= dist;
							dy /= dist;
							dz /= dist;
							data->nodes[i].position[0] += overlap * dx;
							data->nodes[i].position[1] += overlap * dy;
							data->nodes[i].position[2] += overlap * dz;
							data->nodes[j].position[0] -= overlap * dx;
							data->nodes[j].position[1] -= overlap * dy;
							data->nodes[j].position[2] -= overlap * dz;
						}
					}
				}
			}
		}
	}
	graph_sync_node_positions(data);
	free(head);
	free(next);
}
