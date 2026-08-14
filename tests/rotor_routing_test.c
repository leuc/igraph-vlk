/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "graph/rotor_routing.h"
#include "test_utilities.h"

#include <stdlib.h>

static int test_walk_cycle_and_reveal_order(void)
{
	igraph_t graph;
	IGRAPH_ASSERT(igraph_small(&graph, 4, IGRAPH_UNDIRECTED, 0, 1, 0, 2, 0, 3, -1) == IGRAPH_SUCCESS);
	IGRAPH_ASSERT(rotor_routing_select_source(&graph) == 0);
	RotorRoutingResult *result = rotor_routing_run(&graph, 0, ROTOR_ROUTING_WALK, NULL, NULL);
	IGRAPH_ASSERT(result != NULL);
	IGRAPH_ASSERT(result->component_size == 4);
	IGRAPH_ASSERT(result->completed_count == 4);
	IGRAPH_ASSERT(result->total_moves == 5);
	IGRAPH_ASSERT(result->node_first_move[0] == 0);
	IGRAPH_ASSERT(result->node_first_move[1] == 1);
	IGRAPH_ASSERT(result->node_first_move[2] == 3);
	IGRAPH_ASSERT(result->node_first_move[3] == 5);
	IGRAPH_ASSERT(result->edge_first_move[0] == 1);
	IGRAPH_ASSERT(result->edge_first_move[1] == 3);
	IGRAPH_ASSERT(result->edge_first_move[2] == 5);
	IGRAPH_ASSERT(result->rotor_positions[0] == 2);
	IGRAPH_ASSERT(result->edge_traversals[0] == 2);
	IGRAPH_ASSERT(result->edge_traversals[1] == 2);
	IGRAPH_ASSERT(result->edge_traversals[2] == 1);
	rotor_routing_result_free(result);
	igraph_destroy(&graph);
	return 0;
}

static int test_parallel_edges_are_distinct_exits(void)
{
	igraph_t graph;
	IGRAPH_ASSERT(igraph_small(&graph, 3, IGRAPH_UNDIRECTED, 0, 1, 0, 1, 0, 2, -1) == IGRAPH_SUCCESS);
	RotorRoutingResult *result = rotor_routing_run(&graph, 0, ROTOR_ROUTING_WALK, NULL, NULL);
	IGRAPH_ASSERT(result != NULL);
	IGRAPH_ASSERT(result->edge_first_move[0] == 1);
	IGRAPH_ASSERT(result->edge_first_move[1] == 3);
	IGRAPH_ASSERT(result->edge_traversals[0] == 2);
	IGRAPH_ASSERT(result->edge_traversals[1] == 2);
	IGRAPH_ASSERT(result->rotor_positions[0] == 2);
	rotor_routing_result_free(result);
	igraph_destroy(&graph);
	return 0;
}

static int test_loop_uses_two_schedule_positions(void)
{
	igraph_t graph;
	IGRAPH_ASSERT(igraph_small(&graph, 2, IGRAPH_UNDIRECTED, 0, 0, 0, 1, -1) == IGRAPH_SUCCESS);
	RotorRoutingResult *result = rotor_routing_run(&graph, 0, ROTOR_ROUTING_WALK, NULL, NULL);
	IGRAPH_ASSERT(result != NULL);
	IGRAPH_ASSERT(result->total_moves == 3);
	IGRAPH_ASSERT(result->edge_traversals[0] == 2);
	IGRAPH_ASSERT(result->edge_first_move[1] == 3);
	IGRAPH_ASSERT(result->node_first_move[1] == 3);
	IGRAPH_ASSERT(result->rotor_positions[0] == 2);
	rotor_routing_result_free(result);
	igraph_destroy(&graph);
	return 0;
}

static int test_directed_input_is_symmetric(void)
{
	igraph_t graph;
	IGRAPH_ASSERT(igraph_small(&graph, 3, IGRAPH_DIRECTED, 1, 0, 2, 0, -1) == IGRAPH_SUCCESS);
	IGRAPH_ASSERT(rotor_routing_select_source(&graph) == 0);
	RotorRoutingResult *result = rotor_routing_run(&graph, 0, ROTOR_ROUTING_WALK, NULL, NULL);
	IGRAPH_ASSERT(result != NULL);
	IGRAPH_ASSERT(result->completed_count == 3);
	IGRAPH_ASSERT(result->node_first_move[1] != UINT64_MAX);
	IGRAPH_ASSERT(result->node_first_move[2] != UINT64_MAX);
	rotor_routing_result_free(result);
	igraph_destroy(&graph);
	return 0;
}

static int test_walk_stays_in_source_component(void)
{
	igraph_t graph;
	IGRAPH_ASSERT(igraph_small(&graph, 5, IGRAPH_UNDIRECTED, 0, 1, 2, 3, 3, 4, 4, 2, -1) == IGRAPH_SUCCESS);
	RotorRoutingResult *result = rotor_routing_run(&graph, 0, ROTOR_ROUTING_WALK, NULL, NULL);
	IGRAPH_ASSERT(result != NULL);
	IGRAPH_ASSERT(result->component_size == 2);
	IGRAPH_ASSERT(result->completed_count == 2);
	IGRAPH_ASSERT(result->node_first_move[0] == 0);
	IGRAPH_ASSERT(result->node_first_move[1] == 1);
	for (uint32_t node = 2; node < 5; node++)
		IGRAPH_ASSERT(result->node_first_move[node] == UINT64_MAX);
	for (uint32_t edge = 1; edge < 4; edge++)
		IGRAPH_ASSERT(result->edge_traversals[edge] == 0);
	rotor_routing_result_free(result);
	igraph_destroy(&graph);
	return 0;
}

static int test_aggregation_persists_rotors(void)
{
	igraph_t graph;
	IGRAPH_ASSERT(igraph_small(&graph, 3, IGRAPH_UNDIRECTED, 0, 1, 1, 2, -1) == IGRAPH_SUCCESS);
	igraph_integer_t nodes_before = igraph_vcount(&graph);
	igraph_integer_t edges_before = igraph_ecount(&graph);
	RotorRoutingResult *result = rotor_routing_run(&graph, 1, ROTOR_ROUTING_AGGREGATION, NULL, NULL);
	IGRAPH_ASSERT(result != NULL);
	IGRAPH_ASSERT(igraph_vcount(&graph) == nodes_before);
	IGRAPH_ASSERT(igraph_ecount(&graph) == edges_before);
	IGRAPH_ASSERT(result->component_size == 3);
	IGRAPH_ASSERT(result->completed_count == 3);
	IGRAPH_ASSERT(result->settled_chips == 2);
	IGRAPH_ASSERT(result->total_moves == 2);
	IGRAPH_ASSERT(result->node_visits[0] == 1);
	IGRAPH_ASSERT(result->node_visits[1] == 3);
	IGRAPH_ASSERT(result->node_visits[2] == 1);
	IGRAPH_ASSERT(result->edge_traversals[0] == 1);
	IGRAPH_ASSERT(result->edge_traversals[1] == 1);
	IGRAPH_ASSERT(result->rotor_positions[1] == 1);
	rotor_routing_result_free(result);
	igraph_destroy(&graph);
	return 0;
}

static int test_isolated_vertex(void)
{
	igraph_t graph;
	IGRAPH_ASSERT(igraph_empty(&graph, 1, IGRAPH_DIRECTED) == IGRAPH_SUCCESS);
	for (RotorRoutingMode mode = ROTOR_ROUTING_WALK; mode <= ROTOR_ROUTING_AGGREGATION; mode++) {
		RotorRoutingResult *result = rotor_routing_run(&graph, 0, mode, NULL, NULL);
		IGRAPH_ASSERT(result != NULL);
		IGRAPH_ASSERT(result->component_size == 1);
		IGRAPH_ASSERT(result->completed_count == 1);
		IGRAPH_ASSERT(result->total_moves == 0);
		IGRAPH_ASSERT(result->node_visits[0] == 1);
		IGRAPH_ASSERT(result->rotor_positions[0] == UINT32_MAX);
		rotor_routing_result_free(result);
	}
	igraph_destroy(&graph);
	return 0;
}

typedef struct
{
	int calls;
} CancelPoll;

static bool cancel_immediately(uint32_t completed, uint32_t total, uint64_t moves, void *data)
{
	(void)completed;
	(void)total;
	(void)moves;
	CancelPoll *poll = data;
	poll->calls++;
	return false;
}

static int test_cancellation_and_invalid_input(void)
{
	igraph_t graph;
	IGRAPH_ASSERT(igraph_small(&graph, 2, IGRAPH_UNDIRECTED, 0, 1, -1) == IGRAPH_SUCCESS);
	CancelPoll poll = {0};
	IGRAPH_ASSERT(rotor_routing_run(&graph, 0, ROTOR_ROUTING_WALK, cancel_immediately, &poll) == NULL);
	IGRAPH_ASSERT(poll.calls == 1);
	IGRAPH_ASSERT(rotor_routing_run(&graph, -1, ROTOR_ROUTING_WALK, NULL, NULL) == NULL);
	IGRAPH_ASSERT(rotor_routing_run(&graph, 2, ROTOR_ROUTING_WALK, NULL, NULL) == NULL);
	igraph_destroy(&graph);

	IGRAPH_ASSERT(igraph_empty(&graph, 0, IGRAPH_UNDIRECTED) == IGRAPH_SUCCESS);
	IGRAPH_ASSERT(rotor_routing_select_source(&graph) == -1);
	IGRAPH_ASSERT(rotor_routing_run(&graph, 0, ROTOR_ROUTING_WALK, NULL, NULL) == NULL);
	igraph_destroy(&graph);
	return 0;
}

static int test_deterministic_repeated_run(void)
{
	igraph_t graph;
	IGRAPH_ASSERT(igraph_small(&graph, 4, IGRAPH_UNDIRECTED, 0, 1, 1, 2, 2, 3, 3, 0, 0, 2, -1) == IGRAPH_SUCCESS);
	RotorRoutingResult *first = rotor_routing_run(&graph, 0, ROTOR_ROUTING_AGGREGATION, NULL, NULL);
	RotorRoutingResult *second = rotor_routing_run(&graph, 0, ROTOR_ROUTING_AGGREGATION, NULL, NULL);
	IGRAPH_ASSERT(first != NULL && second != NULL);
	IGRAPH_ASSERT(first->total_moves == second->total_moves);
	for (uint32_t node = 0; node < first->node_count; node++) {
		IGRAPH_ASSERT(first->node_first_move[node] == second->node_first_move[node]);
		IGRAPH_ASSERT(first->node_visits[node] == second->node_visits[node]);
		IGRAPH_ASSERT(first->rotor_positions[node] == second->rotor_positions[node]);
	}
	for (uint32_t edge = 0; edge < first->edge_count; edge++) {
		IGRAPH_ASSERT(first->edge_first_move[edge] == second->edge_first_move[edge]);
		IGRAPH_ASSERT(first->edge_traversals[edge] == second->edge_traversals[edge]);
	}
	rotor_routing_result_free(first);
	rotor_routing_result_free(second);
	igraph_destroy(&graph);
	return 0;
}

int main(void)
{
	RUN_TEST(test_walk_cycle_and_reveal_order);
	RUN_TEST(test_parallel_edges_are_distinct_exits);
	RUN_TEST(test_loop_uses_two_schedule_positions);
	RUN_TEST(test_directed_input_is_symmetric);
	RUN_TEST(test_walk_stays_in_source_component);
	RUN_TEST(test_aggregation_persists_rotors);
	RUN_TEST(test_isolated_vertex);
	RUN_TEST(test_cancellation_and_invalid_input);
	RUN_TEST(test_deterministic_repeated_run);
	return EXIT_SUCCESS;
}
