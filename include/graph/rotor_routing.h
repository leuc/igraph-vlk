/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GRAPH_ROTOR_ROUTING_H
#define GRAPH_ROTOR_ROUTING_H

#include <igraph.h>
#include <stdbool.h>

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
	RotorRoutingStop stop;
	igraph_integer_t visited_nodes;
	igraph_integer_t visited_edges;
	igraph_integer_t target_nodes;
	igraph_integer_t target_edges;
} RotorRoutingResult;

// Runs one finite rotor walk from vertex 0. Schedules contain arcs in
// ascending igraph edge-ID order and use advance-then-move. Directed graphs
// retain their arcs; undirected graphs contribute one arc in each direction.
// A false running flag cancels the calculation and returns NULL.
RotorRoutingResult *rotor_routing_run(const igraph_t *graph, const bool *running);
void rotor_routing_result_free(RotorRoutingResult *result);

#endif
