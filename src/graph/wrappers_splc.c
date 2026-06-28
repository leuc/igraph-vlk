/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "graph/wrappers_splc.h"
#include "app_state.h"
#include "graph/graph_core.h"
#include "vulkan/renderer.h"
#include "vulkan/renderer_compute.h"

#include <stdio.h>
#include <stdlib.h>

igraph_integer_t calculate_dag_levels(const igraph_t *graph, igraph_vector_int_t *levels)
{
	igraph_integer_t n = igraph_vcount(graph);
	if (n == 0)
		return -1;

	if (!igraph_is_directed(graph)) {
		fprintf(stderr, "SPLC requires a directed graph\n");
		return -1;
	}

	igraph_bool_t is_dag = false;
	igraph_is_dag(graph, &is_dag);
	if (!is_dag) {
		return -1;
	}

	igraph_vector_int_t topo_order;
	igraph_vector_int_init(&topo_order, 0);

	if (igraph_topological_sorting(graph, &topo_order, IGRAPH_OUT) != IGRAPH_SUCCESS) {
		fprintf(stderr, "Graph is not a DAG (topological sort failed)\n");
		igraph_vector_int_destroy(&topo_order);
		return -1;
	}

	igraph_vector_int_resize(levels, n);
	igraph_vector_int_null(levels);

	igraph_vector_int_t out_neis;
	igraph_vector_int_init(&out_neis, 0);

	igraph_integer_t max_level = 0;

	for (igraph_integer_t i = 0; i < igraph_vector_int_size(&topo_order); i++) {
		igraph_integer_t node = VECTOR(topo_order)[i];
		igraph_integer_t node_level = VECTOR(*levels)[node];

		igraph_vector_int_clear(&out_neis);
		igraph_neighbors(graph, &out_neis, node, IGRAPH_OUT, IGRAPH_LOOPS, IGRAPH_NO_MULTIPLE);

		for (igraph_integer_t j = 0; j < igraph_vector_int_size(&out_neis); j++) {
			igraph_integer_t neighbor = VECTOR(out_neis)[j];
			igraph_integer_t candidate = node_level + 1;
			if (VECTOR(*levels)[neighbor] < candidate) {
				VECTOR(*levels)[neighbor] = candidate;
				if (candidate > max_level)
					max_level = candidate;
			}
		}
	}

	igraph_vector_int_destroy(&out_neis);
	igraph_vector_int_destroy(&topo_order);

	return max_level;
}

// ============================================================================
// Worker: Prepare graph for SPLC animation
// Checks directed, makes acyclic if needed (in-place), returns graph on success
// ============================================================================
void *compute_splc_animation(ExecutionContext *ctx)
{
	igraph_t *graph = &ctx->app_state->current_graph.g;
	if (!graph || igraph_vcount(graph) == 0)
		return NULL;

	if (!igraph_is_directed(graph)) {
		fprintf(stderr, "SPLC requires a directed graph\n");
		return NULL;
	}

	igraph_bool_t is_dag = false;
	igraph_is_dag(graph, &is_dag);

	if (!is_dag) {
		igraph_vector_int_t fas;
		igraph_vector_int_init(&fas, 0);
		if (igraph_feedback_arc_set(graph, &fas, NULL, IGRAPH_FAS_APPROX_EADES) == IGRAPH_SUCCESS) {
			if (igraph_vector_int_size(&fas) > 0) {
				igraph_es_t es = igraph_ess_vector(&fas);
				igraph_delete_edges(graph, es);
				printf("Removed %d edges to make graph acyclic\n", (int)igraph_vector_int_size(&fas));
			}
		}
		igraph_vector_int_destroy(&fas);
	}

	return graph;
}

// ============================================================================
// Apply: Refresh graph data and trigger SPLC buffer init via renderer update
// ============================================================================
void apply_splc_animation(ExecutionContext *ctx, void *result_data)
{
	if (!ctx || !ctx->app_state || !result_data)
		return;
	(void)result_data;

	AppState *state = ctx->app_state;
	GraphData *data = &state->current_graph;

	state->renderer.needsAttributeUpload = VK_TRUE;
	graph_rebuild_edges(data);
	renderer_init_splc_buffers(&state->renderer, data);
	renderer_update_graph(&state->renderer, data);
	state->renderer.label.tree_needs_rebuild = true;

	printf("SPLC animation started (graph has %d levels)\n", state->renderer.splc.num_levels);

	igraph_vector_int_t indeg, outdeg;
	igraph_integer_t nv = igraph_vcount(&data->g);
	igraph_vector_int_init(&indeg, nv);
	igraph_vector_int_init(&outdeg, nv);
	igraph_degree(&data->g, &indeg, igraph_vss_all(), IGRAPH_IN, IGRAPH_LOOPS);
	igraph_degree(&data->g, &outdeg, igraph_vss_all(), IGRAPH_OUT, IGRAPH_LOOPS);
	igraph_integer_t sources = 0, sinks = 0;
	for (igraph_integer_t i = 0; i < nv; i++) {
		if (VECTOR(indeg)[i] == 0)
			sources++;
		if (VECTOR(outdeg)[i] == 0)
			sinks++;
	}
	printf("  sources: %d, sinks: %d\n", (int)sources, (int)sinks);
	igraph_vector_int_destroy(&indeg);
	igraph_vector_int_destroy(&outdeg);
}

// ============================================================================
// GPU Poll: Per-frame lifecycle — returns true when SPLC animation is complete
// ============================================================================
bool poll_splc_gpu(ExecutionContext *ctx)
{
	if (!ctx || !ctx->app_state)
		return true;

	Renderer *r = &ctx->app_state->renderer;

	// SPLC per-frame dispatch happens in renderer_draw_frame.
	// This poll manages the lifecycle: once splc_active goes false (all levels
	// dispatched and readback done), the job is complete.
	if (!r->splc.active && !r->splc.readback_pending)
		return true;

	return false;
}
