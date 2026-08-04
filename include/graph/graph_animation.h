/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GRAPH_ANIMATION_H
#define GRAPH_ANIMATION_H

#include "graph/graph_types.h"
#include "vulkan/vulkan_types.h"

typedef struct
{
	const int *node_steps;
	const float *node_values;
	const float *edge_values;
	float duration;
} GraphAnimationRequest;

void graph_animation_play(Renderer *renderer, const GraphData *graph, const GraphAnimationRequest *request);
void graph_animation_clear(Renderer *renderer);
void graph_animation_play_bfs(Renderer *renderer, const GraphData *graph);
void graph_animation_play_dfs(Renderer *renderer, const GraphData *graph);
void graph_animation_play_topological(Renderer *renderer, const GraphData *graph);

#endif
