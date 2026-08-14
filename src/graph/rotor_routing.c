/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "graph/rotor_routing.h"

#include <stdlib.h>

typedef struct
{
	uint32_t head;
	uint32_t edge_id;
} RotorArc;

typedef struct
{
	uint32_t *offsets;
	RotorArc *arcs;
} RotorSchedule;

/* Finite inputs are routed on the symmetric directed multigraph obtained by
 * adding both orientations of every edge. Parallel arcs and both orientations
 * of a loop remain distinct schedule positions. */

static bool rotor_routing_dimensions(const igraph_t *graph, uint32_t *node_count, uint32_t *edge_count, uint32_t *arc_count)
{
	if (!graph || !node_count || !edge_count || !arc_count)
		return false;
	igraph_integer_t nodes = igraph_vcount(graph);
	igraph_integer_t edges = igraph_ecount(graph);
	if (nodes < 0 || edges < 0 || (uint64_t)nodes > UINT32_MAX || (uint64_t)edges > UINT32_MAX || (uint64_t)edges * 2 > UINT32_MAX)
		return false;
	*node_count = (uint32_t)nodes;
	*edge_count = (uint32_t)edges;
	*arc_count = (uint32_t)edges * 2;
	return true;
}

igraph_integer_t rotor_routing_select_source(const igraph_t *graph)
{
	uint32_t node_count, edge_count, arc_count;
	if (!rotor_routing_dimensions(graph, &node_count, &edge_count, &arc_count) || node_count == 0)
		return -1;
	(void)arc_count;

	uint32_t *degrees = calloc(node_count, sizeof(uint32_t));
	if (!degrees)
		return -1;
	for (uint32_t edge = 0; edge < edge_count; edge++) {
		igraph_integer_t from, to;
		if (igraph_edge(graph, edge, &from, &to) != IGRAPH_SUCCESS || from < 0 || to < 0 || (uint64_t)from >= node_count || (uint64_t)to >= node_count || degrees[from] == UINT32_MAX || degrees[to] == UINT32_MAX) {
			free(degrees);
			return -1;
		}
		degrees[from]++;
		degrees[to]++;
	}

	uint32_t source = 0;
	for (uint32_t node = 1; node < node_count; node++)
		if (degrees[node] > degrees[source])
			source = node;
	free(degrees);
	return (igraph_integer_t)source;
}

void rotor_routing_result_free(RotorRoutingResult *result)
{
	if (!result)
		return;
	free(result->node_first_move);
	free(result->edge_first_move);
	free(result->node_visits);
	free(result->edge_traversals);
	free(result->rotor_positions);
	free(result);
}

static RotorRoutingResult *rotor_routing_result_alloc(uint32_t node_count, uint32_t edge_count, RotorRoutingMode mode, uint32_t source)
{
	RotorRoutingResult *result = calloc(1, sizeof(RotorRoutingResult));
	if (!result)
		return NULL;
	result->mode = mode;
	result->source = source;
	result->node_count = node_count;
	result->edge_count = edge_count;
	result->node_first_move = malloc(sizeof(uint64_t) * node_count);
	result->edge_first_move = malloc(sizeof(uint64_t) * (edge_count > 0 ? edge_count : 1));
	result->node_visits = calloc(node_count, sizeof(uint64_t));
	result->edge_traversals = calloc(edge_count > 0 ? edge_count : 1, sizeof(uint64_t));
	result->rotor_positions = malloc(sizeof(uint32_t) * node_count);
	if (!result->node_first_move || !result->edge_first_move || !result->node_visits || !result->edge_traversals || !result->rotor_positions) {
		rotor_routing_result_free(result);
		return NULL;
	}
	for (uint32_t node = 0; node < node_count; node++)
		result->node_first_move[node] = UINT64_MAX;
	for (uint32_t edge = 0; edge < edge_count; edge++)
		result->edge_first_move[edge] = UINT64_MAX;
	return result;
}

static bool rotor_routing_build_schedule(const igraph_t *graph, RotorRoutingResult *result, uint32_t arc_count, RotorSchedule *schedule)
{
	uint32_t *degrees = calloc(result->node_count, sizeof(uint32_t));
	uint32_t *cursor = calloc(result->node_count, sizeof(uint32_t));
	schedule->offsets = calloc((size_t)result->node_count + 1, sizeof(uint32_t));
	schedule->arcs = malloc(sizeof(RotorArc) * (arc_count > 0 ? arc_count : 1));
	if (!degrees || !cursor || !schedule->offsets || !schedule->arcs) {
		free(degrees);
		free(cursor);
		free(schedule->offsets);
		free(schedule->arcs);
		schedule->offsets = NULL;
		schedule->arcs = NULL;
		return false;
	}

	for (uint32_t edge = 0; edge < result->edge_count; edge++) {
		igraph_integer_t from, to;
		if (igraph_edge(graph, edge, &from, &to) != IGRAPH_SUCCESS || from < 0 || to < 0 || (uint64_t)from >= result->node_count || (uint64_t)to >= result->node_count || degrees[from] == UINT32_MAX || degrees[to] == UINT32_MAX) {
			free(degrees);
			free(cursor);
			free(schedule->offsets);
			free(schedule->arcs);
			schedule->offsets = NULL;
			schedule->arcs = NULL;
			return false;
		}
		degrees[from]++;
		degrees[to]++;
	}
	for (uint32_t node = 0; node < result->node_count; node++)
		schedule->offsets[node + 1] = schedule->offsets[node] + degrees[node];
	for (uint32_t node = 0; node < result->node_count; node++) {
		cursor[node] = schedule->offsets[node];
		result->rotor_positions[node] = degrees[node] > 0 ? degrees[node] - 1 : UINT32_MAX;
	}

	for (uint32_t edge = 0; edge < result->edge_count; edge++) {
		igraph_integer_t from, to;
		if (igraph_edge(graph, edge, &from, &to) != IGRAPH_SUCCESS) {
			free(degrees);
			free(cursor);
			free(schedule->offsets);
			free(schedule->arcs);
			schedule->offsets = NULL;
			schedule->arcs = NULL;
			return false;
		}
		uint32_t forward = cursor[from]++;
		uint32_t reverse = cursor[to]++;
		schedule->arcs[forward] = (RotorArc){.head = (uint32_t)to, .edge_id = edge};
		schedule->arcs[reverse] = (RotorArc){.head = (uint32_t)from, .edge_id = edge};
	}

	free(degrees);
	free(cursor);
	return true;
}

static bool rotor_routing_component_size(const RotorRoutingResult *result, const RotorSchedule *schedule, uint32_t source, uint32_t *component_size)
{
	bool *seen = calloc(result->node_count, sizeof(bool));
	uint32_t *queue = malloc(sizeof(uint32_t) * result->node_count);
	if (!seen || !queue) {
		free(seen);
		free(queue);
		return false;
	}
	uint32_t head = 0;
	uint32_t tail = 0;
	seen[source] = true;
	queue[tail++] = source;
	while (head < tail) {
		uint32_t node = queue[head++];
		for (uint32_t arc = schedule->offsets[node]; arc < schedule->offsets[node + 1]; arc++) {
			uint32_t next = schedule->arcs[arc].head;
			if (!seen[next]) {
				seen[next] = true;
				queue[tail++] = next;
			}
		}
	}
	*component_size = tail;
	free(seen);
	free(queue);
	return true;
}

static bool rotor_routing_increment(uint64_t *value)
{
	if (*value == UINT64_MAX)
		return false;
	(*value)++;
	return true;
}

static bool rotor_routing_step(RotorRoutingResult *result, const RotorSchedule *schedule, uint32_t position, uint32_t *next_position)
{
	uint32_t first = schedule->offsets[position];
	uint32_t degree = schedule->offsets[position + 1] - first;
	if (degree == 0 || result->total_moves == UINT64_MAX)
		return false;
	/* Advance-then-move: n_j -> n_j+1 mod tau_j (Priezzhev et al. 1996,
	 * https://doi.org/10.1103/PhysRevLett.77.5079, PDF p. 1). Initial degree-1 selects
	 * the first zero-based arc on the first departure. */
	uint32_t rotor = (result->rotor_positions[position] + 1) % degree;
	uint32_t arc_index = first + rotor;
	uint32_t edge = schedule->arcs[arc_index].edge_id;
	result->rotor_positions[position] = rotor;
	result->total_moves++;
	if (!rotor_routing_increment(&result->edge_traversals[edge]))
		return false;
	if (result->edge_first_move[edge] == UINT64_MAX)
		result->edge_first_move[edge] = result->total_moves;
	*next_position = schedule->arcs[arc_index].head;
	return true;
}

static bool rotor_routing_poll(RotorRoutingPollFunc poll_func, void *poll_data, const RotorRoutingResult *result, bool force)
{
	if (!poll_func)
		return true;
	if (!force && (result->total_moves & 4095u) != 0)
		return true;
	return poll_func(result->completed_count, result->component_size, result->total_moves, poll_data);
}

static bool rotor_routing_walk(RotorRoutingResult *result, const RotorSchedule *schedule, RotorRoutingPollFunc poll_func, void *poll_data)
{
	bool *visited = calloc(result->node_count, sizeof(bool));
	if (!visited)
		return false;
	uint32_t position = result->source;
	visited[position] = true;
	result->node_first_move[position] = 0;
	result->node_visits[position] = 1;
	result->completed_count = 1;
	if (!rotor_routing_poll(poll_func, poll_data, result, true)) {
		free(visited);
		return false;
	}

	while (result->completed_count < result->component_size) {
		uint32_t next;
		if (!rotor_routing_step(result, schedule, position, &next) || !rotor_routing_increment(&result->node_visits[next])) {
			free(visited);
			return false;
		}
		position = next;
		bool discovered = !visited[position];
		if (discovered) {
			visited[position] = true;
			result->node_first_move[position] = result->total_moves;
			result->completed_count++;
		}
		if (!rotor_routing_poll(poll_func, poll_data, result, discovered)) {
			free(visited);
			return false;
		}
	}

	free(visited);
	return true;
}

static bool rotor_routing_aggregation(RotorRoutingResult *result, const RotorSchedule *schedule, RotorRoutingPollFunc poll_func, void *poll_data)
{
	bool *occupied = calloc(result->node_count, sizeof(bool));
	if (!occupied)
		return false;
	occupied[result->source] = true;
	result->node_first_move[result->source] = 0;
	result->node_visits[result->source] = 1;
	result->completed_count = 1;
	if (!rotor_routing_poll(poll_func, poll_data, result, true)) {
		free(occupied);
		return false;
	}

	while (result->completed_count < result->component_size) {
		uint32_t position = result->source;
		if (!rotor_routing_increment(&result->node_visits[position])) {
			free(occupied);
			return false;
		}
		while (occupied[position]) {
			uint32_t next;
			if (!rotor_routing_step(result, schedule, position, &next) || !rotor_routing_increment(&result->node_visits[next])) {
				free(occupied);
				return false;
			}
			position = next;
			bool settled = false;
			if (!occupied[position]) {
				/* A_(m+1) = A_m union {z_m}; the fixed host graph is unchanged
				 * (Kager and Levine 2010, https://doi.org/10.37236/424, eq. (1), PDF p. 2). */
				occupied[position] = true;
				result->node_first_move[position] = result->total_moves;
				result->completed_count++;
				result->settled_chips++;
				settled = true;
			}
			if (!rotor_routing_poll(poll_func, poll_data, result, settled)) {
				free(occupied);
				return false;
			}
			if (settled)
				break;
		}
	}

	free(occupied);
	return true;
}

RotorRoutingResult *rotor_routing_run(const igraph_t *graph, igraph_integer_t source, RotorRoutingMode mode, RotorRoutingPollFunc poll_func, void *poll_data)
{
	uint32_t node_count, edge_count, arc_count;
	if (!rotor_routing_dimensions(graph, &node_count, &edge_count, &arc_count) || node_count == 0 || source < 0 || (uint64_t)source >= node_count || (mode != ROTOR_ROUTING_WALK && mode != ROTOR_ROUTING_AGGREGATION))
		return NULL;

	RotorRoutingResult *result = rotor_routing_result_alloc(node_count, edge_count, mode, (uint32_t)source);
	if (!result)
		return NULL;
	RotorSchedule schedule = {0};
	if (!rotor_routing_build_schedule(graph, result, arc_count, &schedule) || !rotor_routing_component_size(result, &schedule, result->source, &result->component_size)) {
		free(schedule.offsets);
		free(schedule.arcs);
		rotor_routing_result_free(result);
		return NULL;
	}

	bool success = mode == ROTOR_ROUTING_WALK ? rotor_routing_walk(result, &schedule, poll_func, poll_data) : rotor_routing_aggregation(result, &schedule, poll_func, poll_data);
	free(schedule.offsets);
	free(schedule.arcs);
	if (!success) {
		rotor_routing_result_free(result);
		return NULL;
	}
	return result;
}
