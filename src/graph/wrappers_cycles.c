/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "graph/wrappers_cycles.h"
#include "app_state.h"
#include "graph/graph_core.h"
#include "vulkan/renderer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// Worker: Compute and remove feedback arc set to make graph acyclic.
// Modifies graph in-place (same pattern as compute_splc_animation).
// Returns graph pointer on success, NULL on failure.
// ============================================================================
void *compute_remove_feedback_arc_set(ExecutionContext *ctx)
{
	igraph_t *graph = &ctx->app_state->current_graph.g;
	if (!graph || igraph_vcount(graph) == 0)
		return NULL;

	if (!igraph_is_directed(graph)) {
		fprintf(stderr, "Feedback arc set requires a directed graph\n");
		return NULL;
	}

	igraph_bool_t is_dag = false;
	igraph_is_dag(graph, &is_dag);

	if (!is_dag) {
		igraph_vector_int_t fas;
		igraph_vector_int_init(&fas, 0);
		igraph_error_t ret = igraph_feedback_arc_set(graph, &fas, NULL, IGRAPH_FAS_APPROX_EADES);
		if (ret == IGRAPH_SUCCESS) {
			igraph_integer_t n_fas = igraph_vector_int_size(&fas);
			if (n_fas > 0) {
				igraph_es_t es = igraph_ess_vector(&fas);
				igraph_delete_edges(graph, es);
				printf("Removed %d edges to make graph acyclic\n", (int)n_fas);
			} else {
				printf("Graph has cycles but feedback arc set is empty\n");
			}
		} else {
			igraph_vector_int_destroy(&fas);
			fprintf(stderr, "igraph_feedback_arc_set failed\n");
			return NULL;
		}
		igraph_vector_int_destroy(&fas);
	} else {
		printf("Graph is already acyclic\n");
	}

	return graph;
}

// ============================================================================
// Apply: Refresh visualization after in-place graph modification.
// Does NOT free/copy the graph or compute a new layout.
// ============================================================================
void apply_remove_feedback_arc_set(ExecutionContext *ctx, void *result_data)
{
	if (!ctx || !ctx->app_state)
		return;
	(void)result_data;

	AppState *state = ctx->app_state;
	GraphData *data = &state->current_graph;

	graph_rebuild_edges(data);
	renderer_update_graph(&state->renderer, data);
	state->renderer.label.tree_needs_rebuild = true;

	printf("[apply] Feedback arc set processed - %d vertices, %d edges\n", data->node_count, data->edge_count);
}

// ============================================================================
// Worker: Simplify graph by removing multi-edges and loops.
// Modifies graph in-place.
// Returns graph pointer on success, NULL on failure.
// ============================================================================
void *compute_igraph_simplify(ExecutionContext *ctx)
{
	igraph_t *graph = &ctx->app_state->current_graph.g;
	if (!graph || igraph_vcount(graph) == 0)
		return NULL;

	igraph_integer_t before_v = igraph_vcount(graph);
	igraph_integer_t before_e = igraph_ecount(graph);

	igraph_error_t ret = igraph_simplify(graph, 1, 1, NULL);
	if (ret != IGRAPH_SUCCESS) {
		fprintf(stderr, "igraph_simplify failed\n");
		return NULL;
	}

	igraph_integer_t after_e = igraph_ecount(graph);
	printf("Simplified graph: %d vertices, %d edges -> %d edges (%d removed)\n", (int)before_v, (int)before_e, (int)after_e, (int)(before_e - after_e));

	return graph;
}

// ============================================================================
// Worker: Convert undirected graph to directed.
// Modifies graph in-place.
// ============================================================================
void *compute_to_directed(ExecutionContext *ctx)
{
	igraph_t *graph = &ctx->app_state->current_graph.g;
	if (!graph || igraph_vcount(graph) == 0)
		return NULL;

	if (igraph_is_directed(graph)) {
		printf("Graph is already directed\n");
		return graph;
	}

	igraph_error_t ret = igraph_to_directed(graph, IGRAPH_TO_DIRECTED_ARBITRARY);
	if (ret != IGRAPH_SUCCESS) {
		fprintf(stderr, "igraph_to_directed failed\n");
		return NULL;
	}

	printf("Converted graph to directed: %d vertices, %d edges\n", (int)igraph_vcount(graph), (int)igraph_ecount(graph));
	return graph;
}

// ============================================================================
// Worker: Convert directed graph to undirected (collapse mode).
// One undirected edge per connected pair, no multi-edges.
// ============================================================================
void *compute_to_undirected_collapse(ExecutionContext *ctx)
{
	igraph_t *graph = &ctx->app_state->current_graph.g;
	if (!graph || igraph_vcount(graph) == 0)
		return NULL;

	if (!igraph_is_directed(graph)) {
		printf("Graph is already undirected\n");
		return graph;
	}

	igraph_error_t ret = igraph_to_undirected(graph, IGRAPH_TO_UNDIRECTED_COLLAPSE, NULL);
	if (ret != IGRAPH_SUCCESS) {
		fprintf(stderr, "igraph_to_undirected (collapse) failed\n");
		return NULL;
	}

	printf("Converted graph to undirected (collapse): %d vertices, %d edges\n", (int)igraph_vcount(graph), (int)igraph_ecount(graph));
	return graph;
}

// ============================================================================
// Worker: Convert directed graph to undirected (mutual mode).
// Only edges existing in both directions survive. No multi-edges.
// ============================================================================
void *compute_to_undirected_mutual(ExecutionContext *ctx)
{
	igraph_t *graph = &ctx->app_state->current_graph.g;
	if (!graph || igraph_vcount(graph) == 0)
		return NULL;

	if (!igraph_is_directed(graph)) {
		printf("Graph is already undirected\n");
		return graph;
	}

	igraph_integer_t before_e = igraph_ecount(graph);

	igraph_error_t ret = igraph_to_undirected(graph, IGRAPH_TO_UNDIRECTED_MUTUAL, NULL);
	if (ret != IGRAPH_SUCCESS) {
		fprintf(stderr, "igraph_to_undirected (mutual) failed\n");
		return NULL;
	}

	igraph_integer_t after_e = igraph_ecount(graph);
	printf("Converted graph to undirected (mutual): %d vertices, %d edges -> %d edges (%d dropped)\n", (int)igraph_vcount(graph), (int)before_e, (int)after_e, (int)(before_e - after_e));
	return graph;
}

// ============================================================================
// Shared apply: Refresh visualization after any in-place graph modification.
// ============================================================================
void apply_inplace_graph_update(ExecutionContext *ctx, void *result_data)
{
	if (!ctx || !ctx->app_state)
		return;
	(void)result_data;

	AppState *state = ctx->app_state;
	GraphData *data = &state->current_graph;

	graph_rebuild_edges(data);
	renderer_update_graph(&state->renderer, data);
	state->renderer.label.tree_needs_rebuild = true;

	printf("[apply] Graph updated - %d vertices, %d edges\n", data->node_count, data->edge_count);
}

// ============================================================================
// Free: No-op — graph is owned by GraphData, not the result
// ============================================================================
void free_noop(void *result_data)
{
	(void)result_data;
}
