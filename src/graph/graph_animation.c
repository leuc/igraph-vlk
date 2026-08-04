/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "graph/graph_animation.h"

#include <stdio.h>
#include <stdlib.h>

#include "vulkan/renderer_anim.h"
#include "vulkan/renderer_compute.h"

static void graph_animation_submit(Renderer *renderer, const GraphData *graph, const GraphAnimationRequest *request)
{
	if (!renderer || !graph || !request)
		return;

	uint32_t *edge_sources = NULL;
	if (graph->edge_count > 0) {
		edge_sources = malloc(sizeof(uint32_t) * graph->edge_count);
		if (!edge_sources) {
			fprintf(stderr, "[graph animation] Failed to allocate edge sources\n");
			return;
		}
		for (uint32_t i = 0; i < graph->edge_count; i++)
			edge_sources[i] = graph->edges[i].from;
	}

	RendererAnimClip clip = {
		.node_steps = request->node_steps,
		.node_values = request->node_values,
		.edge_sources = edge_sources,
		.edge_values = request->edge_values,
		.node_count = graph->node_count,
		.edge_count = graph->edge_count,
		.duration = request->duration,
		.edge_event_offsets = request->edge_event_offsets,
		.edge_events = request->edge_events,
		.edge_event_count = request->edge_event_count,
	};
	renderer_anim_play(renderer, &clip);
	free(edge_sources);
}

void graph_animation_play(Renderer *renderer, const GraphData *graph, const GraphAnimationRequest *request)
{
	graph_animation_submit(renderer, graph, request);
}

void graph_animation_clear(Renderer *renderer)
{
	if (!renderer)
		return;
	renderer_anim_clear(renderer);
	renderer_splc_clear_visualization(renderer);
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

static void graph_animation_play_order(Renderer *renderer, const GraphData *graph, GraphAnimationOrder kind)
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

	GraphAnimationRequest request = {.node_steps = steps, .duration = 3.0f};
	graph_animation_submit(renderer, graph, &request);
	free(steps);
}

void graph_animation_play_bfs(Renderer *renderer, const GraphData *graph)
{
	graph_animation_play_order(renderer, graph, GRAPH_ANIMATION_BFS);
}

void graph_animation_play_dfs(Renderer *renderer, const GraphData *graph)
{
	graph_animation_play_order(renderer, graph, GRAPH_ANIMATION_DFS);
}

void graph_animation_play_topological(Renderer *renderer, const GraphData *graph)
{
	graph_animation_play_order(renderer, graph, GRAPH_ANIMATION_TOPOLOGICAL);
}
