/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GRAPH_ANIMATION_H
#define GRAPH_ANIMATION_H

#include "graph/graph_types.h"
#include "vulkan/renderer_anim.h"

typedef struct
{
	const int *node_steps;
	const int *edge_steps;
	const float *edge_values;
	float duration;
	bool keep_unrevealed_dim;
} GraphAnimationRequest;

void graph_animation_play(Renderer *renderer, const GraphData *graph, const GraphAnimationRequest *request);
void graph_animation_clear(Renderer *renderer, const GraphData *graph);
void graph_animation_play_bfs(Renderer *renderer, const GraphData *graph, float duration);
void graph_animation_play_dfs(Renderer *renderer, const GraphData *graph, float duration);
void graph_animation_play_topological(Renderer *renderer, const GraphData *graph, float duration);

#endif
