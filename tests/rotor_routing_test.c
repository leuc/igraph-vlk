/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "graph/rotor_routing.h"

#include <assert.h>

static void test_directed_complete(void)
{
	igraph_t graph;
	assert(igraph_small(&graph, 3, IGRAPH_DIRECTED, 0, 1, 0, 2, 1, 0, 2, 0, -1) == IGRAPH_SUCCESS);
	RotorRoutingResult *result = rotor_routing_run(&graph, NULL);
	assert(result);
	assert(result->stop == ROTOR_ROUTING_COMPLETE);
	assert(result->visited_nodes == 3);
	assert(result->visited_edges == 4);
	assert(result->node_steps[0] == 0);
	assert(result->edge_steps[0] == 1 && result->node_steps[1] == 1);
	assert(result->edge_steps[2] == 2);
	assert(result->edge_steps[1] == 3 && result->node_steps[2] == 3);
	assert(result->edge_steps[3] == 4);
	rotor_routing_result_free(result);
	igraph_destroy(&graph);
}

static void test_parallel_arcs_and_loop(void)
{
	igraph_t graph;
	assert(igraph_small(&graph, 2, IGRAPH_DIRECTED, 0, 1, 0, 1, 0, 0, 1, 0, -1) == IGRAPH_SUCCESS);
	RotorRoutingResult *result = rotor_routing_run(&graph, NULL);
	assert(result);
	assert(result->stop == ROTOR_ROUTING_COMPLETE);
	assert(result->edge_steps[0] == 1);
	assert(result->edge_steps[3] == 2);
	assert(result->edge_steps[1] == 3);
	assert(result->edge_steps[2] == 5);
	assert(result->node_steps[0] == 0);
	assert(result->node_steps[1] == 1);
	rotor_routing_result_free(result);
	igraph_destroy(&graph);
}

static void test_undirected_symmetric_arcs(void)
{
	igraph_t graph;
	assert(igraph_small(&graph, 3, IGRAPH_UNDIRECTED, 0, 1, 1, 2, -1) == IGRAPH_SUCCESS);
	RotorRoutingResult *result = rotor_routing_run(&graph, NULL);
	assert(result);
	assert(result->stop == ROTOR_ROUTING_COMPLETE);
	assert(result->edge_steps[0] == 1);
	assert(result->edge_steps[1] == 4);
	assert(result->node_steps[2] == 4);
	rotor_routing_result_free(result);
	igraph_destroy(&graph);
}

static void test_sink_partial_result(void)
{
	igraph_t graph;
	assert(igraph_small(&graph, 3, IGRAPH_DIRECTED, 0, 1, 0, 2, -1) == IGRAPH_SUCCESS);
	RotorRoutingResult *result = rotor_routing_run(&graph, NULL);
	assert(result);
	assert(result->stop == ROTOR_ROUTING_SINK);
	assert(result->node_steps[0] == 0 && result->node_steps[1] == 1);
	assert(result->node_steps[2] == -1);
	assert(result->edge_steps[0] == 1 && result->edge_steps[1] == -1);
	rotor_routing_result_free(result);
	igraph_destroy(&graph);
}

static void test_repeated_state_partial_result(void)
{
	igraph_t graph;
	assert(igraph_small(&graph, 4, IGRAPH_DIRECTED, 0, 1, 0, 2, 1, 3, 3, 1, 2, 2, -1) == IGRAPH_SUCCESS);
	RotorRoutingResult *result = rotor_routing_run(&graph, NULL);
	assert(result);
	assert(result->stop == ROTOR_ROUTING_CYCLE);
	assert(result->node_steps[0] == 0 && result->node_steps[1] == 1 && result->node_steps[3] == 2);
	assert(result->node_steps[2] == -1);
	assert(result->edge_steps[0] == 1 && result->edge_steps[2] == 2 && result->edge_steps[3] == 3);
	assert(result->edge_steps[1] == -1 && result->edge_steps[4] == -1);
	rotor_routing_result_free(result);
	igraph_destroy(&graph);
}

int main(void)
{
	test_directed_complete();
	test_parallel_arcs_and_loop();
	test_undirected_symmetric_arcs();
	test_sink_partial_result();
	test_repeated_state_partial_result();
	return 0;
}
