/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef ROTOR_ROUTING_H
#define ROTOR_ROUTING_H

#include <stdbool.h>
#include <stdint.h>

#include <igraph.h>

typedef enum {
	ROTOR_ROUTING_WALK,
	ROTOR_ROUTING_AGGREGATION,
} RotorRoutingMode;

typedef struct
{
	RotorRoutingMode mode;
	uint32_t source;
	uint32_t node_count;
	uint32_t edge_count;
	uint32_t component_size;
	uint32_t completed_count;
	uint64_t settled_chips;
	uint64_t total_moves;
	uint64_t *node_first_move;
	uint64_t *edge_first_move;
	uint64_t *node_visits;
	uint64_t *edge_traversals;
	uint32_t *rotor_positions;
} RotorRoutingResult;

typedef bool (*RotorRoutingPollFunc)(uint32_t completed, uint32_t total, uint64_t moves, void *data);

igraph_integer_t rotor_routing_select_source(const igraph_t *graph);
RotorRoutingResult *rotor_routing_run(const igraph_t *graph, igraph_integer_t source, RotorRoutingMode mode, RotorRoutingPollFunc poll_func, void *poll_data);
void rotor_routing_result_free(RotorRoutingResult *result);

#endif
