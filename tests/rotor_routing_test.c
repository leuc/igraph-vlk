/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "graph/rotor_routing.h"

#include <assert.h>
#include <math.h>

static void test_directed_complete(void)
{
	igraph_t graph;
	assert(igraph_small(&graph, 3, IGRAPH_DIRECTED, 0, 1, 0, 2, 1, 0, 2, 0, -1) == IGRAPH_SUCCESS);
	RotorRoutingResult *result = rotor_routing_run(&graph, NULL);
	assert(result);
	assert(result->source == 0);
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
	assert(result->source == 0);
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
	assert(result->source == 1);
	assert(result->stop == ROTOR_ROUTING_COMPLETE);
	assert(result->edge_steps[0] == 1);
	assert(result->edge_steps[1] == 3);
	assert(result->node_steps[2] == 3);
	rotor_routing_result_free(result);
	igraph_destroy(&graph);
}

static void test_sink_partial_result(void)
{
	igraph_t graph;
	assert(igraph_small(&graph, 3, IGRAPH_DIRECTED, 0, 1, 0, 2, -1) == IGRAPH_SUCCESS);
	RotorRoutingResult *result = rotor_routing_run(&graph, NULL);
	assert(result);
	assert(result->source == 0);
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
	assert(igraph_small(&graph, 4, IGRAPH_DIRECTED, 0, 1, 0, 2, 0, 0, 1, 3, 3, 1, 2, 2, -1) == IGRAPH_SUCCESS);
	RotorRoutingResult *result = rotor_routing_run(&graph, NULL);
	assert(result);
	assert(result->source == 0);
	assert(result->stop == ROTOR_ROUTING_CYCLE);
	assert(result->node_steps[0] == 0 && result->node_steps[1] == 1 && result->node_steps[3] == 2);
	assert(result->node_steps[2] == -1);
	assert(result->edge_steps[0] == 1 && result->edge_steps[3] == 2 && result->edge_steps[4] == 3);
	assert(result->edge_steps[1] == -1 && result->edge_steps[2] == -1 && result->edge_steps[5] == -1);
	rotor_routing_result_free(result);
	igraph_destroy(&graph);
}

static void test_aggregation_grows_occupied_set(void)
{
	igraph_t graph;
	assert(igraph_small(&graph, 3, IGRAPH_UNDIRECTED, 0, 1, 1, 2, -1) == IGRAPH_SUCCESS);
	igraph_integer_t original_nodes = igraph_vcount(&graph);
	igraph_integer_t original_edges = igraph_ecount(&graph);
	RotorAggregationResult *result = rotor_routing_aggregate(&graph, NULL);
	assert(result);
	assert(result->source == 1);
	assert(result->stop == ROTOR_ROUTING_COMPLETE);
	assert(result->occupied_count == 3);
	assert(result->particle_count == 3);
	assert(result->node_steps[1] == 0);
	assert(result->node_steps[0] == 1 && result->edge_steps[0] == 1);
	assert(result->node_steps[2] == 2 && result->edge_steps[1] == 2);
	assert(result->node_visits[1] == 3);
	assert(result->node_visits[0] == 1 && result->node_visits[2] == 1);
	assert(result->edge_traversals[0] == 1 && result->edge_traversals[1] == 1);
	assert(igraph_vcount(&graph) == original_nodes);
	assert(igraph_ecount(&graph) == original_edges);
	rotor_aggregation_result_free(result);
	igraph_destroy(&graph);
}

static void test_aggregation_persists_rotors(void)
{
	igraph_t graph;
	assert(igraph_small(&graph, 4, IGRAPH_UNDIRECTED, 0, 1, 1, 2, 2, 3, -1) == IGRAPH_SUCCESS);
	RotorAggregationResult *result = rotor_routing_aggregate(&graph, NULL);
	assert(result);
	assert(result->source == 1);
	assert(result->stop == ROTOR_ROUTING_COMPLETE);
	assert(result->occupied_count == 4);
	assert(result->particle_count == 4);
	uint64_t traversals = 0;
	uint64_t visits = 0;
	for (igraph_integer_t e = 0; e < result->edge_count; e++)
		traversals += result->edge_traversals[e];
	for (igraph_integer_t v = 0; v < result->node_count; v++)
		visits += result->node_visits[v];
	assert(visits == result->particle_count + traversals);
	assert(result->edge_traversals[0] > 1);
	assert(result->node_visits[1] > result->node_visits[3]);
	rotor_aggregation_result_free(result);
	igraph_destroy(&graph);
}

static void test_aggregation_sink_partial_result(void)
{
	igraph_t graph;
	assert(igraph_small(&graph, 3, IGRAPH_DIRECTED, 0, 1, 0, 1, 0, 2, -1) == IGRAPH_SUCCESS);
	RotorAggregationResult *result = rotor_routing_aggregate(&graph, NULL);
	assert(result);
	assert(result->source == 0);
	assert(result->stop == ROTOR_ROUTING_SINK);
	assert(result->occupied_count == 2);
	assert(result->node_steps[1] == 1);
	assert(result->node_steps[2] == -1);
	assert(result->edge_traversals[0] == 1);
	assert(result->edge_traversals[1] == 1);
	assert(result->edge_traversals[2] == 0);
	rotor_aggregation_result_free(result);
	igraph_destroy(&graph);
}

static void test_aggregation_cycle_partial_result(void)
{
	igraph_t graph;
	assert(igraph_small(&graph, 3, IGRAPH_DIRECTED, 0, 1, 0, 1, 0, 2, 0, 0, 1, 1, -1) == IGRAPH_SUCCESS);
	RotorAggregationResult *result = rotor_routing_aggregate(&graph, NULL);
	assert(result);
	assert(result->source == 0);
	assert(result->stop == ROTOR_ROUTING_CYCLE);
	assert(result->occupied_count == 2);
	assert(result->node_steps[2] == -1);
	assert(result->edge_traversals[4] > 0);
	rotor_aggregation_result_free(result);
	igraph_destroy(&graph);
}

typedef struct
{
	igraph_integer_t calls;
	igraph_integer_t occupied[4];
	uint64_t particles[4];
} AggregationUpdates;

static bool record_aggregation_update(const RotorAggregationResult *result, void *data)
{
	AggregationUpdates *updates = data;
	assert(updates->calls < 4);
	updates->occupied[updates->calls] = result->occupied_count;
	updates->particles[updates->calls] = result->particle_count;
	updates->calls++;
	return true;
}

static void test_aggregation_updates_after_each_release(void)
{
	igraph_t graph;
	assert(igraph_small(&graph, 3, IGRAPH_UNDIRECTED, 0, 1, 1, 2, -1) == IGRAPH_SUCCESS);
	AggregationUpdates updates = {0};
	RotorAggregationResult *result = rotor_routing_aggregate_with_updates(&graph, NULL, record_aggregation_update, &updates);
	assert(result);
	assert(updates.calls == 3);
	for (igraph_integer_t i = 0; i < updates.calls; i++) {
		assert(updates.occupied[i] == i + 1);
		assert(updates.particles[i] == (uint64_t)(i + 1));
	}
	rotor_aggregation_result_free(result);
	igraph_destroy(&graph);
}

static void test_node_intensity(void)
{
	assert(rotor_routing_node_intensity(0, 10) == 0.25f);
	assert(fabsf(rotor_routing_node_intensity(10, 10) - 1.0f) < 1e-6f);
	float middle = rotor_routing_node_intensity(2, 10);
	assert(middle > 0.25f && middle < 1.0f);
}

int main(void)
{
	test_directed_complete();
	test_parallel_arcs_and_loop();
	test_undirected_symmetric_arcs();
	test_sink_partial_result();
	test_repeated_state_partial_result();
	test_aggregation_grows_occupied_set();
	test_aggregation_persists_rotors();
	test_aggregation_sink_partial_result();
	test_aggregation_cycle_partial_result();
	test_aggregation_updates_after_each_release();
	test_node_intensity();
	return 0;
}
