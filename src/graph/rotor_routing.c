/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "graph/rotor_routing.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
	igraph_integer_t *edges;
	igraph_integer_t *heads;
	igraph_integer_t count;
} RotorSchedule;

typedef struct
{
	igraph_integer_t position;
	igraph_integer_t *next;
	uint64_t hash;
} RotorWalker;

static uint64_t rotor_routing_mix(uint64_t value)
{
	value ^= value >> 30;
	value *= UINT64_C(0xbf58476d1ce4e5b9);
	value ^= value >> 27;
	value *= UINT64_C(0x94d049bb133111eb);
	return value ^ (value >> 31);
}

static uint64_t rotor_routing_position_hash(igraph_integer_t position)
{
	return rotor_routing_mix((uint64_t)position + UINT64_C(0x243f6a8885a308d3));
}

static uint64_t rotor_routing_next_hash(igraph_integer_t vertex, igraph_integer_t next)
{
	return rotor_routing_mix(((uint64_t)vertex << 32) ^ (uint64_t)next ^ UINT64_C(0x13198a2e03707344));
}

static void rotor_routing_schedules_destroy(RotorSchedule *schedules, igraph_integer_t count)
{
	if (!schedules)
		return;
	for (igraph_integer_t v = 0; v < count; v++) {
		free(schedules[v].edges);
		free(schedules[v].heads);
	}
	free(schedules);
}

static RotorSchedule *rotor_routing_schedules_create(const igraph_t *graph, igraph_integer_t node_count, igraph_integer_t edge_count)
{
	RotorSchedule *schedules = calloc((size_t)(node_count > 0 ? node_count : 1), sizeof(RotorSchedule));
	if (!schedules)
		return NULL;

	bool directed = igraph_is_directed(graph);
	for (igraph_integer_t e = 0; e < edge_count; e++) {
		igraph_integer_t from, to;
		if (igraph_edge(graph, e, &from, &to) != IGRAPH_SUCCESS) {
			rotor_routing_schedules_destroy(schedules, node_count);
			return NULL;
		}
		schedules[from].count++;
		if (!directed)
			schedules[to].count++;
	}

	for (igraph_integer_t v = 0; v < node_count; v++) {
		if (schedules[v].count == 0)
			continue;
		schedules[v].edges = malloc(sizeof(igraph_integer_t) * (size_t)schedules[v].count);
		schedules[v].heads = malloc(sizeof(igraph_integer_t) * (size_t)schedules[v].count);
		if (!schedules[v].edges || !schedules[v].heads) {
			rotor_routing_schedules_destroy(schedules, node_count);
			return NULL;
		}
		schedules[v].count = 0;
	}

	for (igraph_integer_t e = 0; e < edge_count; e++) {
		igraph_integer_t from, to;
		if (igraph_edge(graph, e, &from, &to) != IGRAPH_SUCCESS) {
			rotor_routing_schedules_destroy(schedules, node_count);
			return NULL;
		}
		igraph_integer_t index = schedules[from].count++;
		schedules[from].edges[index] = e;
		schedules[from].heads[index] = to;
		if (!directed) {
			index = schedules[to].count++;
			schedules[to].edges[index] = e;
			schedules[to].heads[index] = from;
		}
	}
	return schedules;
}

static bool rotor_routing_target(const RotorSchedule *schedules, igraph_integer_t node_count, igraph_integer_t edge_count, bool *target_nodes, bool *target_edges, igraph_integer_t *target_node_count, igraph_integer_t *target_edge_count)
{
	igraph_integer_t *queue = malloc(sizeof(igraph_integer_t) * (size_t)node_count);
	if (!queue)
		return false;
	igraph_integer_t head = 0;
	igraph_integer_t tail = 0;
	target_nodes[0] = true;
	queue[tail++] = 0;
	*target_node_count = 1;
	*target_edge_count = 0;
	while (head < tail) {
		igraph_integer_t vertex = queue[head++];
		for (igraph_integer_t i = 0; i < schedules[vertex].count; i++) {
			igraph_integer_t edge = schedules[vertex].edges[i];
			igraph_integer_t next = schedules[vertex].heads[i];
			if (!target_edges[edge]) {
				target_edges[edge] = true;
				(*target_edge_count)++;
			}
			if (!target_nodes[next]) {
				target_nodes[next] = true;
				queue[tail++] = next;
				(*target_node_count)++;
			}
		}
	}
	free(queue);
	return true;
}

static bool rotor_routing_walker_init(RotorWalker *walker, const RotorSchedule *schedules, igraph_integer_t node_count)
{
	walker->next = malloc(sizeof(igraph_integer_t) * (size_t)node_count);
	if (!walker->next)
		return false;
	walker->position = 0;
	walker->hash = rotor_routing_position_hash(0);
	for (igraph_integer_t v = 0; v < node_count; v++) {
		walker->next[v] = schedules[v].count > 0 ? schedules[v].count - 1 : 0;
		walker->hash ^= rotor_routing_next_hash(v, walker->next[v]);
	}
	return true;
}

static bool rotor_routing_walker_step(RotorWalker *walker, const RotorSchedule *schedules, igraph_integer_t *edge, igraph_integer_t *arrival)
{
	igraph_integer_t vertex = walker->position;
	igraph_integer_t count = schedules[vertex].count;
	if (count == 0)
		return false;
	igraph_integer_t old_next = walker->next[vertex];
	igraph_integer_t new_next = (old_next + 1) % count;
	igraph_integer_t next_position = schedules[vertex].heads[new_next];
	walker->next[vertex] = new_next;
	walker->hash ^= rotor_routing_position_hash(vertex) ^ rotor_routing_position_hash(next_position);
	walker->hash ^= rotor_routing_next_hash(vertex, old_next) ^ rotor_routing_next_hash(vertex, new_next);
	walker->position = next_position;
	if (edge)
		*edge = schedules[vertex].edges[new_next];
	if (arrival)
		*arrival = next_position;
	return true;
}

static bool rotor_routing_walkers_equal(const RotorWalker *a, const RotorWalker *b, igraph_integer_t node_count)
{
	return a->position == b->position && a->hash == b->hash && memcmp(a->next, b->next, sizeof(igraph_integer_t) * (size_t)node_count) == 0;
}

void rotor_routing_result_free(RotorRoutingResult *result)
{
	if (!result)
		return;
	free(result->node_steps);
	free(result->edge_steps);
	free(result);
}

RotorRoutingResult *rotor_routing_run(const igraph_t *graph, const bool *running)
{
	if (!graph)
		return NULL;
	igraph_integer_t node_count = igraph_vcount(graph);
	igraph_integer_t edge_count = igraph_ecount(graph);
	if (node_count <= 0)
		return NULL;

	RotorRoutingResult *result = calloc(1, sizeof(RotorRoutingResult));
	RotorSchedule *schedules = NULL;
	bool *target_nodes = NULL;
	bool *target_edges = NULL;
	bool *seen_nodes = NULL;
	bool *seen_edges = NULL;
	RotorWalker walker = {0};
	RotorWalker hare = {0};
	RotorWalker cycle_start = {0};

	if (!result)
		return NULL;
	result->node_count = node_count;
	result->edge_count = edge_count;
	result->node_steps = malloc(sizeof(int) * (size_t)node_count);
	result->edge_steps = malloc(sizeof(int) * (size_t)(edge_count > 0 ? edge_count : 1));
	target_nodes = calloc((size_t)node_count, sizeof(bool));
	target_edges = calloc((size_t)(edge_count > 0 ? edge_count : 1), sizeof(bool));
	seen_nodes = calloc((size_t)node_count, sizeof(bool));
	seen_edges = calloc((size_t)(edge_count > 0 ? edge_count : 1), sizeof(bool));
	if (!result->node_steps || !result->edge_steps || !target_nodes || !target_edges || !seen_nodes || !seen_edges)
		goto cleanup;
	for (igraph_integer_t v = 0; v < node_count; v++)
		result->node_steps[v] = -1;
	for (igraph_integer_t e = 0; e < edge_count; e++)
		result->edge_steps[e] = -1;

	schedules = rotor_routing_schedules_create(graph, node_count, edge_count);
	if (!schedules || !rotor_routing_target(schedules, node_count, edge_count, target_nodes, target_edges, &result->target_nodes, &result->target_edges))
		goto cleanup;
	if (!rotor_routing_walker_init(&walker, schedules, node_count) || !rotor_routing_walker_init(&hare, schedules, node_count))
		goto cleanup;

	seen_nodes[0] = true;
	result->node_steps[0] = 0;
	result->visited_nodes = 1;
	if (result->visited_nodes == result->target_nodes && result->visited_edges == result->target_edges) {
		result->stop = ROTOR_ROUTING_COMPLETE;
		goto success;
	}

	for (int step = 1;; step++) {
		if (running && !*running)
			goto cleanup;
		igraph_integer_t edge, arrival;
		if (!rotor_routing_walker_step(&walker, schedules, &edge, &arrival)) {
			result->stop = ROTOR_ROUTING_SINK;
			goto success;
		}
		if (!seen_edges[edge]) {
			seen_edges[edge] = true;
			result->edge_steps[edge] = step;
			result->visited_edges++;
		}
		if (!seen_nodes[arrival]) {
			seen_nodes[arrival] = true;
			result->node_steps[arrival] = step;
			result->visited_nodes++;
		}
		if (result->visited_nodes == result->target_nodes && result->visited_edges == result->target_edges) {
			result->stop = ROTOR_ROUTING_COMPLETE;
			goto success;
		}
		if (cycle_start.next && rotor_routing_walkers_equal(&walker, &cycle_start, node_count)) {
			result->stop = ROTOR_ROUTING_CYCLE;
			goto success;
		}
		if (step == INT_MAX)
			goto cleanup;

		if (!rotor_routing_walker_step(&hare, schedules, NULL, NULL) || !rotor_routing_walker_step(&hare, schedules, NULL, NULL))
			continue;
		if (!cycle_start.next && rotor_routing_walkers_equal(&walker, &hare, node_count)) {
			cycle_start.next = malloc(sizeof(igraph_integer_t) * (size_t)node_count);
			if (!cycle_start.next)
				goto cleanup;
			cycle_start.position = walker.position;
			cycle_start.hash = walker.hash;
			memcpy(cycle_start.next, walker.next, sizeof(igraph_integer_t) * (size_t)node_count);
		}
	}

success:
	free(walker.next);
	free(hare.next);
	free(cycle_start.next);
	free(target_nodes);
	free(target_edges);
	free(seen_nodes);
	free(seen_edges);
	rotor_routing_schedules_destroy(schedules, node_count);
	return result;

cleanup:
	free(walker.next);
	free(hare.next);
	free(cycle_start.next);
	free(target_nodes);
	free(target_edges);
	free(seen_nodes);
	free(seen_edges);
	rotor_routing_schedules_destroy(schedules, node_count);
	rotor_routing_result_free(result);
	return NULL;
}
