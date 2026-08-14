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

void *compute_rotor_routing_trigger(ExecutionContext *ctx)
{
	if (!ctx || !ctx->app_state || !ctx->app_state->current_graph.graph_initialized) {
		fprintf(stderr, "[RotorRouting] Graph not initialized\n");
		return NULL;
	}
	worker_thread_set_status_message("Rotor Routing: walking from vertex 0...");
	RotorRoutingResult *result = rotor_routing_run(&ctx->app_state->current_graph.g, &ctx->running);
	if (!result)
		return NULL;
	const char *stop = result->stop == ROTOR_ROUTING_COMPLETE ? "complete" : result->stop == ROTOR_ROUTING_SINK ? "stopped at sink" : "stopped at repeated state";
	char message[160];
	snprintf(message, sizeof(message), "Rotor Routing: %s (%lld/%lld nodes, %lld/%lld edges)", stop, (long long)result->visited_nodes, (long long)result->target_nodes, (long long)result->visited_edges, (long long)result->target_edges);
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
