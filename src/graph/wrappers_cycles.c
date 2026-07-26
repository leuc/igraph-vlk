/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "graph/wrappers_cycles.h"
#include "app_state.h"
#include "graph/graph_core.h"
#include "graph/wrappers_centrality.h"
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
	if (!ctx->app_state->current_graph.graph_initialized) {
		fprintf(stderr, "[%s] Graph not initialized\n", __func__);
		return NULL;
	}
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
		igraph_vector_t weights;
		bool has_weights = graph_build_edge_weights(graph, &weights);
		igraph_error_t ret = igraph_feedback_arc_set(graph, &fas, has_weights ? &weights : NULL, IGRAPH_FAS_APPROX_EADES);
		if (has_weights)
			igraph_vector_destroy(&weights);
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

	centrality_clear_cached_attrs(&data->g);

	if (!graph_rebuild_edges(data)) {
		fprintf(stderr, "[apply_remove_feedback_arc_set] graph_rebuild_edges failed\n");
		return;
	}
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
	if (!ctx->app_state->current_graph.graph_initialized) {
		fprintf(stderr, "[%s] Graph not initialized\n", __func__);
		return NULL;
	}
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
	if (!ctx->app_state->current_graph.graph_initialized) {
		fprintf(stderr, "[%s] Graph not initialized\n", __func__);
		return NULL;
	}
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
	if (!ctx->app_state->current_graph.graph_initialized) {
		fprintf(stderr, "[%s] Graph not initialized\n", __func__);
		return NULL;
	}
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
	if (!ctx->app_state->current_graph.graph_initialized) {
		fprintf(stderr, "[%s] Graph not initialized\n", __func__);
		return NULL;
	}
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
// Worker: Remove vertices whose 'date' attribute is missing or an empty
// string. Requires the graph to have a 'date' vertex attribute at all.
// Modifies graph in-place. Unlike Simplify/to_directed/to_undirected_*, this
// changes vertex count, so it also drops the same rows from current_layout
// (highest id first, so earlier removals don't shift the indices of ones
// still pending) — otherwise apply_inplace_graph_update_full's rebuild would
// hand surviving vertices stale/mismatched positions from the old, larger
// layout matrix. Returns graph pointer on success, NULL on failure.
// ============================================================================
void *compute_remove_empty_date_nodes(ExecutionContext *ctx)
{
	GraphData *data = &ctx->app_state->current_graph;
	igraph_t *graph = &data->g;
	if (!data->graph_initialized) {
		fprintf(stderr, "[%s] Graph not initialized\n", __func__);
		return NULL;
	}
	if (igraph_vcount(graph) == 0)
		return NULL;

	if (!igraph_cattribute_has_attr(graph, IGRAPH_ATTRIBUTE_VERTEX, "date")) {
		fprintf(stderr, "Remove Nodes with Empty \"date\" requires a 'date' vertex attribute\n");
		return NULL;
	}

	igraph_integer_t before_v = igraph_vcount(graph);
	igraph_vector_int_t to_remove;
	if (igraph_vector_int_init(&to_remove, 0) != IGRAPH_SUCCESS)
		return NULL;

	for (igraph_integer_t i = 0; i < before_v; i++) {
		const char *s = igraph_cattribute_VAS(graph, "date", i);
		if (!s || s[0] == '\0') {
			if (igraph_vector_int_push_back(&to_remove, i) != IGRAPH_SUCCESS) {
				fprintf(stderr, "[%s] push_back failed\n", __func__);
				igraph_vector_int_destroy(&to_remove);
				return NULL;
			}
		}
	}

	igraph_integer_t n_remove = igraph_vector_int_size(&to_remove);
	if (n_remove == 0) {
		igraph_vector_int_destroy(&to_remove);
		printf("Removed 0 vertices with empty \"date\"\n");
		return graph;
	}

	igraph_integer_t ncol = igraph_matrix_ncol(&data->current_layout);
	igraph_matrix_t new_layout;
	if (igraph_matrix_init(&new_layout, before_v - n_remove, ncol) != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(&to_remove);
		return NULL;
	}
	igraph_integer_t ri = 0, dst = 0;
	for (igraph_integer_t i = 0; i < before_v; i++) {
		if (ri < n_remove && VECTOR(to_remove)[ri] == i) {
			ri++;
			continue;
		}
		for (igraph_integer_t c = 0; c < ncol; c++)
			MATRIX(new_layout, dst, c) = MATRIX(data->current_layout, i, c);
		dst++;
	}

	igraph_vs_t vs = igraph_vss_vector(&to_remove);
	if (igraph_delete_vertices(graph, vs) != IGRAPH_SUCCESS) {
		fprintf(stderr, "igraph_delete_vertices failed\n");
		igraph_matrix_destroy(&new_layout);
		igraph_vector_int_destroy(&to_remove);
		return NULL;
	}
	igraph_vector_int_destroy(&to_remove);

	igraph_matrix_destroy(&data->current_layout);
	data->current_layout = new_layout;

	printf("Removed %d vertices with empty \"date\": %d vertices -> %d vertices\n", (int)n_remove, (int)before_v, (int)(before_v - n_remove));

	return graph;
}

// ============================================================================
// Shared apply: Refresh visualization after any in-place graph modification
// that leaves vertex count unchanged (edges/attributes only).
// ============================================================================
void apply_inplace_graph_update(ExecutionContext *ctx, void *result_data)
{
	if (!ctx || !ctx->app_state)
		return;
	(void)result_data;

	AppState *state = ctx->app_state;
	GraphData *data = &state->current_graph;

	centrality_clear_cached_attrs(&data->g);

	if (!graph_rebuild_edges(data)) {
		fprintf(stderr, "[apply_inplace_graph_update] graph_rebuild_edges failed\n");
		return;
	}
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

// ============================================================================
// Shared apply: Refresh visualization after an in-place modification that
// changed vertex count (full rebuild, not just edges — see
// graph_build_visualization vs. graph_rebuild_edges in graph_core.c).
// ============================================================================
void apply_inplace_graph_update_full(ExecutionContext *ctx, void *result_data)
{
	if (!ctx || !ctx->app_state)
		return;
	(void)result_data;

	AppState *state = ctx->app_state;
	GraphData *data = &state->current_graph;

	centrality_clear_cached_attrs(&data->g);

	if (!graph_build_visualization(data)) {
		fprintf(stderr, "[apply_inplace_graph_update_full] graph_build_visualization failed\n");
		return;
	}
	renderer_update_graph(&state->renderer, data);
	state->renderer.label.tree_needs_rebuild = true;

	printf("[apply] Graph updated - %d vertices, %d edges\n", data->node_count, data->edge_count);
}
