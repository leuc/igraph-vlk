/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "graph/wrappers_rotor.h"

#include "app_state.h"
#include "graph/graph_color.h"
#include "graph/rotor_routing.h"
#include "graph/worker_thread.h"
#include "vulkan/renderer.h"
#include "vulkan/renderer_anim.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define ROTOR_ROUTING_ANIMATION_DURATION 4.0f

typedef struct
{
	ExecutionContext *ctx;
	const char *label;
} RotorRoutingPollContext;

static bool rotor_routing_worker_poll(uint32_t completed, uint32_t total, uint64_t moves, void *data)
{
	const RotorRoutingPollContext *poll = data;
	if (!poll || !poll->ctx || !poll->ctx->running)
		return false;
	worker_thread_set_progress(total > 0 ? (float)completed / (float)total : 1.0f);
	char message[160];
	snprintf(message, sizeof(message), "%s: %u/%u nodes, %llu moves", poll->label, completed, total, (unsigned long long)moves);
	worker_thread_set_status_message(message);
	return true;
}

static void *compute_rotor_routing(ExecutionContext *ctx, RotorRoutingMode mode, const char *label)
{
	if (!ctx || !ctx->app_state || !ctx->app_state->current_graph.graph_initialized) {
		fprintf(stderr, "[%s] Graph not initialized\n", label);
		return NULL;
	}
	const igraph_t *graph = &ctx->app_state->current_graph.g;
	igraph_integer_t source = rotor_routing_select_source(graph);
	if (source < 0) {
		fprintf(stderr, "[%s] Graph has no routable source\n", label);
		return NULL;
	}

	char message[128];
	snprintf(message, sizeof(message), "%s: starting from node %lld", label, (long long)source);
	worker_thread_set_status_message(message);
	RotorRoutingPollContext poll = {.ctx = ctx, .label = label};
	RotorRoutingResult *result = rotor_routing_run(graph, source, mode, rotor_routing_worker_poll, &poll);
	if (!result)
		return NULL;
	worker_thread_set_progress(1.0f);
	snprintf(message, sizeof(message), "%s: done, %llu moves", label, (unsigned long long)result->total_moves);
	worker_thread_set_status_message(message);
	return result;
}

void *compute_rotor_walk(ExecutionContext *ctx)
{
	return compute_rotor_routing(ctx, ROTOR_ROUTING_WALK, "Rotor Walk");
}

void *compute_rotor_aggregation(ExecutionContext *ctx)
{
	return compute_rotor_routing(ctx, ROTOR_ROUTING_AGGREGATION, "Rotor Aggregation");
}

static float rotor_routing_reveal_at(uint64_t first_move, uint64_t total_moves)
{
	if (first_move == UINT64_MAX)
		return FLT_MAX;
	if (total_moves == 0)
		return 0.0f;
	return ROTOR_ROUTING_ANIMATION_DURATION * (float)((double)first_move / (double)total_moves);
}

void apply_rotor_routing(ExecutionContext *ctx, void *result_data)
{
	if (!ctx || !ctx->app_state || !result_data)
		return;
	RotorRoutingResult *result = result_data;
	AppState *state = ctx->app_state;
	GraphData *graph = &state->current_graph;
	RendererAnimNode *nodes = calloc(result->node_count > 0 ? result->node_count : 1, sizeof(RendererAnimNode));
	RendererAnimEdge *edges = calloc(result->edge_count > 0 ? result->edge_count : 1, sizeof(RendererAnimEdge));
	if (!nodes || !edges) {
		free(nodes);
		free(edges);
		return;
	}

	for (uint32_t node = 0; node < result->node_count; node++)
		nodes[node].reveal_at = rotor_routing_reveal_at(result->node_first_move[node], result->total_moves);
	float maximum = 0.0f;
	for (uint32_t edge = 0; edge < result->edge_count; edge++) {
		edges[edge].reveal_at = rotor_routing_reveal_at(result->edge_first_move[edge], result->total_moves);
		edges[edge].strength = result->mode == ROTOR_ROUTING_AGGREGATION ? renderer_anim_host_strength((float)result->edge_traversals[edge]) : (result->edge_traversals[edge] > 0 ? 1.0f : 0.0f);
		if (edges[edge].strength > maximum)
			maximum = edges[edge].strength;
	}

	RendererAnimClip clip = {
		.nodes = nodes,
		.edges = edges,
		.node_count = result->node_count,
		.edge_count = result->edge_count,
		.strength_max = maximum,
		.fade = 0.3f,
		.reveal_mask = RENDERER_ANIM_REVEAL_NODES | RENDERER_ANIM_REVEAL_EDGES,
		.owner = RENDERER_ANIM_HOST,
	};
	bool played = renderer_anim_play(&state->renderer, &clip);
	free(nodes);
	free(edges);
	if (!played)
		return;

	graph_reset_emphasis(graph);
	if (result->mode == ROTOR_ROUTING_AGGREGATION) {
		uint64_t max_visits = 0;
		for (uint32_t node = 0; node < result->node_count; node++)
			if (result->node_visits[node] > max_visits)
				max_visits = result->node_visits[node];
		double denominator = log1p((double)max_visits);
		for (uint32_t node = 0; node < result->node_count; node++) {
			double normalized = denominator > 0.0 ? log1p((double)result->node_visits[node]) / denominator : 0.0;
			graph->nodes[node].emphasis = EMPHASIS_DIMMED + (EMPHASIS_FULL - EMPHASIS_DIMMED) * (float)normalized;
		}
	}
	state->renderer.needsAttributeUpload = VK_TRUE;
	renderer_update_graph(&state->renderer, graph);
	printf("%s applied from node %u across %u nodes (%llu moves)\n", result->mode == ROTOR_ROUTING_AGGREGATION ? "Rotor aggregation" : "Rotor walk", result->source, result->component_size, (unsigned long long)result->total_moves);
}

void free_rotor_routing_result(void *result_data)
{
	rotor_routing_result_free(result_data);
}
