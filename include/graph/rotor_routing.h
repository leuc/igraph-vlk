/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GRAPH_ROTOR_ROUTING_H
#define GRAPH_ROTOR_ROUTING_H

#include <igraph.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum {
	ROTOR_ROUTING_COMPLETE,
	ROTOR_ROUTING_SINK,
	ROTOR_ROUTING_CYCLE,
} RotorRoutingStop;

typedef struct
{
	int *node_steps;
	int *edge_steps;
	igraph_integer_t node_count;
	igraph_integer_t edge_count;
	igraph_integer_t source;
	RotorRoutingStop stop;
	igraph_integer_t visited_nodes;
	igraph_integer_t visited_edges;
	igraph_integer_t target_nodes;
	igraph_integer_t target_edges;
} RotorRoutingResult;

typedef struct
{
	int *node_steps;
	int *edge_steps;
	uint64_t *node_visits;
	uint64_t *edge_traversals;
	igraph_integer_t node_count;
	igraph_integer_t edge_count;
	igraph_integer_t source;
	RotorRoutingStop stop;
	igraph_integer_t occupied_count;
	igraph_integer_t target_nodes;
	uint64_t particle_count;
} RotorAggregationResult;

typedef bool (*RotorAggregationUpdateFunc)(const RotorAggregationResult *result, void *data);

// Runs one finite rotor walk from the smallest-ID highest-degree vertex.
// Schedules contain arcs in
// ascending igraph edge-ID order and use advance-then-move. Directed graphs
// retain their arcs; undirected graphs contribute one arc in each direction.
// A false running flag cancels the calculation and returns NULL.
RotorRoutingResult *rotor_routing_run(const igraph_t *graph, const bool *running);
void rotor_routing_result_free(RotorRoutingResult *result);

// Runs finite-host rotor-router aggregation. The host graph remains unchanged;
// only an occupied subset grows. Rotors persist between serial chip releases.
RotorAggregationResult *rotor_routing_aggregate(const igraph_t *graph, const bool *running);
RotorAggregationResult *rotor_routing_aggregate_with_updates(const igraph_t *graph, const bool *running, RotorAggregationUpdateFunc update_func, void *update_data);
void rotor_aggregation_result_free(RotorAggregationResult *result);

float rotor_routing_node_intensity(uint64_t count, uint64_t maximum);

#endif
