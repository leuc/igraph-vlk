/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "graph/wrappers_rotor_routing.h"

#include "app_state.h"
#include "graph/graph_animation.h"
#include "graph/graph_color.h"
#include "graph/rotor_routing.h"
#include "graph/worker_thread.h"
#include <stdio.h>
#include <stdlib.h>

void *compute_rotor_routing_trigger(ExecutionContext *ctx)
{
	if (!ctx || !ctx->app_state || !ctx->app_state->current_graph.graph_initialized) {
		fprintf(stderr, "[RotorRouting] Graph not initialized\n");
		return NULL;
	}
	worker_thread_set_status_message("Rotor Routing: walking from highest-degree vertex...");
	RotorRoutingResult *result = rotor_routing_run(&ctx->app_state->current_graph.g, &ctx->running);
	if (!result)
		return NULL;
	const char *stop = result->stop == ROTOR_ROUTING_COMPLETE ? "complete" : result->stop == ROTOR_ROUTING_SINK ? "stopped at sink" : "stopped at repeated state";
	char message[160];
	snprintf(message, sizeof(message), "Rotor Routing: %s from %lld (%lld/%lld nodes, %lld/%lld edges)", stop, (long long)result->source, (long long)result->visited_nodes, (long long)result->target_nodes, (long long)result->visited_edges, (long long)result->target_edges);
	worker_thread_set_status_message(message);
	worker_thread_set_progress(1.0f);
	return result;
}

void apply_rotor_routing_trigger(ExecutionContext *ctx, void *result_data)
{
	if (!ctx || !ctx->app_state || !result_data)
		return;
	AppState *state = ctx->app_state;
	GraphData *graph = &state->current_graph;
	const RotorRoutingResult *result = result_data;
	if (result->node_count != (igraph_integer_t)graph->node_count || result->edge_count != (igraph_integer_t)graph->edge_count) {
		fprintf(stderr, "[RotorRouting] stale result (graph changed since compute), skipping apply\n");
		return;
	}
	graph_reset_emphasis(graph);
	graph_animation_clear(&state->renderer, graph);
	GraphAnimationRequest request = {
		.node_steps = result->node_steps,
		.edge_steps = result->edge_steps,
		.duration = state->follow_reveal_duration,
		.keep_unrevealed_dim = true,
	};
	graph_animation_play(&state->renderer, graph, &request);
	renderer_update_graph(&state->renderer, graph);
}

void free_rotor_routing_result(void *result_data)
{
	rotor_routing_result_free(result_data);
}

static bool publish_rotor_aggregation_update(const RotorAggregationResult *result, void *data)
{
	(void)data;
	float progress = result->target_nodes > 0 ? (float)result->occupied_count / (float)result->target_nodes : 1.0f;
	worker_thread_set_progress(progress);
	char message[192];
	snprintf(message, sizeof(message), "Rotor Aggregation: released %llu chips from %lld (%lld/%lld occupied)", (unsigned long long)result->particle_count, (long long)result->source, (long long)result->occupied_count, (long long)result->target_nodes);
	worker_thread_set_status_message(message);
	return worker_thread_publish_preview((void *)result);
}

void *compute_rotor_aggregation_trigger(ExecutionContext *ctx)
{
	if (!ctx || !ctx->app_state || !ctx->app_state->current_graph.graph_initialized) {
		fprintf(stderr, "[RotorAggregation] Graph not initialized\n");
		return NULL;
	}
	worker_thread_set_status_message("Rotor Aggregation: releasing chips from highest-degree vertex...");
	RotorAggregationResult *result = rotor_routing_aggregate_with_updates(&ctx->app_state->current_graph.g, &ctx->running, publish_rotor_aggregation_update, NULL);
	if (!result)
		return NULL;
	const char *stop = result->stop == ROTOR_ROUTING_COMPLETE ? "complete" : result->stop == ROTOR_ROUTING_SINK ? "stopped at sink" : "stopped at repeated state";
	char message[192];
	snprintf(message, sizeof(message), "Rotor Aggregation: %s from %lld (%lld/%lld occupied, %llu chips)", stop, (long long)result->source, (long long)result->occupied_count, (long long)result->target_nodes, (unsigned long long)result->particle_count);
	worker_thread_set_status_message(message);
	worker_thread_set_progress(1.0f);
	return result;
}

static void apply_rotor_aggregation_state(ExecutionContext *ctx, void *result_data, bool final)
{
	if (!ctx || !ctx->app_state || !result_data)
		return;
	AppState *state = ctx->app_state;
	GraphData *graph = &state->current_graph;
	const RotorAggregationResult *result = result_data;
	if (result->node_count != (igraph_integer_t)graph->node_count || result->edge_count != (igraph_integer_t)graph->edge_count) {
		fprintf(stderr, "[RotorAggregation] stale result (graph changed since compute), skipping apply\n");
		return;
	}

	uint64_t maximum_node_visits = 0;
	for (igraph_integer_t v = 0; v < result->node_count; v++)
		if (result->node_visits[v] > maximum_node_visits)
			maximum_node_visits = result->node_visits[v];
	float *edge_values = malloc(sizeof(float) * (size_t)(result->edge_count > 0 ? result->edge_count : 1));
	if (!edge_values)
		return;

	graph_reset_emphasis(graph);
	for (igraph_integer_t v = 0; v < result->node_count; v++)
		graph->nodes[v].emphasis = rotor_routing_node_intensity(result->node_visits[v], maximum_node_visits);
	for (igraph_integer_t e = 0; e < result->edge_count; e++)
		edge_values[e] = (float)result->edge_traversals[e];

	GraphAnimationRequest request = {
		.node_steps = result->node_steps,
		.edge_steps = result->edge_steps,
		.edge_values = edge_values,
		.duration = 0.0f,
		.keep_unrevealed_dim = true,
		.reveal_immediately = true,
	};
	graph_animation_play(&state->renderer, graph, &request);
	if (final) {
		state->renderer.needsAttributeUpload = VK_TRUE;
		renderer_update_graph(&state->renderer, graph);
	} else {
		renderer_update_node_attributes(&state->renderer, graph);
	}
	free(edge_values);
}

void apply_rotor_aggregation_trigger(ExecutionContext *ctx, void *result_data)
{
	apply_rotor_aggregation_state(ctx, result_data, true);
}

void apply_rotor_aggregation_preview(ExecutionContext *ctx, void *result_data)
{
	apply_rotor_aggregation_state(ctx, result_data, false);
}

void free_rotor_aggregation_result(void *result_data)
{
	rotor_aggregation_result_free(result_data);
}
