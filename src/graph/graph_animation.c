/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "graph/graph_animation.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "vulkan/renderer_anim.h"

static void graph_animation_submit(Renderer *renderer, const GraphData *graph, const GraphAnimationRequest *request)
{
	if (!renderer || !graph || !request)
		return;

	uint32_t max_step = 0;
	if (request->node_steps)
		for (uint32_t i = 0; i < graph->node_count; i++)
			if (request->node_steps[i] >= 0 && (uint32_t)request->node_steps[i] > max_step)
				max_step = (uint32_t)request->node_steps[i];
	if (request->edge_steps)
		for (uint32_t i = 0; i < graph->edge_count; i++)
			if (request->edge_steps[i] >= 0 && (uint32_t)request->edge_steps[i] > max_step)
				max_step = (uint32_t)request->edge_steps[i];
	float seconds_per_step = max_step > 0 ? request->duration / (float)max_step : 0.0f;
	RendererAnimNode *nodes = calloc(graph->node_count > 0 ? graph->node_count : 1, sizeof(RendererAnimNode));
	RendererAnimEdge *edges = calloc(graph->edge_count > 0 ? graph->edge_count : 1, sizeof(RendererAnimEdge));
	if (!nodes || !edges) {
		free(nodes);
		free(edges);
		fprintf(stderr, "[graph animation] Failed to allocate animation records\n");
		return;
	}
	for (uint32_t i = 0; i < graph->node_count; i++)
		if (request->node_steps) {
			nodes[i].reveal_at = renderer_anim_reveal_at_or_dim(request->node_steps[i], seconds_per_step, request->keep_unrevealed_dim);
			if (request->reveal_immediately && request->node_steps[i] >= 0)
				nodes[i].reveal_at = -0.3f;
		}

	bool all_zero = graph->edge_count > 0;
	float max_strength = 0.0f;
	for (uint32_t i = 0; i < graph->edge_count; i++) {
		float raw = request->edge_values ? request->edge_values[i] : graph->edges[i].weight;
		edges[i].reveal_at = request->edge_steps ? renderer_anim_reveal_at_or_dim(request->edge_steps[i], seconds_per_step, request->keep_unrevealed_dim) : nodes[graph->edges[i].from].reveal_at;
		if (request->reveal_immediately && (!request->edge_steps || request->edge_steps[i] >= 0))
			edges[i].reveal_at = -0.3f;
		edges[i].strength = renderer_anim_host_strength(raw);
		if (edges[i].strength > 0.0f)
			all_zero = false;
		if (edges[i].strength > max_strength)
			max_strength = edges[i].strength;
	}
	if (!request->edge_values && all_zero) {
		max_strength = 1.0f;
		for (uint32_t i = 0; i < graph->edge_count; i++)
			edges[i].strength = 1.0f;
	}

	RendererAnimClip clip = {
		.nodes = nodes,
		.edges = edges,
		.node_count = graph->node_count,
		.edge_count = graph->edge_count,
		.strength_max = max_strength,
		.fade = 0.3f,
		.reveal_mask = RENDERER_ANIM_REVEAL_NODES | RENDERER_ANIM_REVEAL_EDGES,
		.owner = RENDERER_ANIM_HOST,
	};
	renderer_anim_play(renderer, &clip);
	free(nodes);
	free(edges);
}

void graph_animation_play(Renderer *renderer, const GraphData *graph, const GraphAnimationRequest *request)
{
	graph_animation_submit(renderer, graph, request);
}

void graph_animation_clear(Renderer *renderer, const GraphData *graph)
{
	if (!renderer || !graph)
		return;
	renderer->needsAttributeUpload = VK_TRUE;
	renderer_anim_clear(renderer, graph);
}

static igraph_integer_t graph_animation_source(const GraphData *graph)
{
	igraph_integer_t source = 0;
	for (igraph_integer_t i = 1; i < (igraph_integer_t)graph->node_count; i++)
		if (graph->nodes[i].degree > graph->nodes[source].degree)
			source = i;
	return source;
}

typedef enum {
	GRAPH_ANIMATION_BFS,
	GRAPH_ANIMATION_DFS,
	GRAPH_ANIMATION_TOPOLOGICAL,
} GraphAnimationOrder;

static void graph_animation_play_order(Renderer *renderer, const GraphData *graph, GraphAnimationOrder kind, float duration)
{
	if (!renderer || !graph || graph->node_count == 0)
		return;

	igraph_vector_int_t order;
	if (igraph_vector_int_init(&order, 0) != IGRAPH_SUCCESS) {
		fprintf(stderr, "[graph animation] Failed to initialize traversal order\n");
		return;
	}

	igraph_error_t error;
	if (kind == GRAPH_ANIMATION_BFS)
		error = igraph_bfs_simple(&graph->g, graph_animation_source(graph), IGRAPH_ALL, &order, NULL, NULL);
	else if (kind == GRAPH_ANIMATION_DFS)
		error = igraph_dfs(&graph->g, graph_animation_source(graph), IGRAPH_ALL, true, &order, NULL, NULL, NULL, NULL, NULL, NULL);
	else
		error = igraph_topological_sorting(&graph->g, &order, IGRAPH_OUT);

	if (error != IGRAPH_SUCCESS) {
		fprintf(stderr, "[graph animation] Failed to calculate traversal order: %s\n", igraph_strerror(error));
		igraph_vector_int_destroy(&order);
		return;
	}

	int *steps = malloc(sizeof(int) * graph->node_count);
	if (!steps) {
		fprintf(stderr, "[graph animation] Failed to allocate node steps\n");
		igraph_vector_int_destroy(&order);
		return;
	}
	for (uint32_t i = 0; i < graph->node_count; i++)
		steps[i] = -5;
	for (igraph_integer_t i = 0; i < igraph_vector_int_size(&order); i++)
		steps[VECTOR(order)[i]] = (int)i;
	igraph_vector_int_destroy(&order);

	GraphAnimationRequest request = {.node_steps = steps, .duration = duration};
	graph_animation_submit(renderer, graph, &request);
	free(steps);
}

void graph_animation_play_bfs(Renderer *renderer, const GraphData *graph, float duration)
{
	graph_animation_play_order(renderer, graph, GRAPH_ANIMATION_BFS, duration);
}

void graph_animation_play_dfs(Renderer *renderer, const GraphData *graph, float duration)
{
	graph_animation_play_order(renderer, graph, GRAPH_ANIMATION_DFS, duration);
}

void graph_animation_play_topological(Renderer *renderer, const GraphData *graph, float duration)
{
	graph_animation_play_order(renderer, graph, GRAPH_ANIMATION_TOPOLOGICAL, duration);
}
