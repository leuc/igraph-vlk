/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "graph/wrappers_splc.h"
#include "app_state.h"
#include "graph/graph_animation.h"
#include "graph/graph_color.h"
#include "graph/graph_core.h"
#include "vulkan/renderer.h"
#include "vulkan/renderer_criticality.h"

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
	if (igraph_vector_int_init(&topo_order, 0) != IGRAPH_SUCCESS) {
		fprintf(stderr, "Failed to initialize topo_order vector\n");
		return -1;
	}

	if (igraph_topological_sorting(graph, &topo_order, IGRAPH_OUT) != IGRAPH_SUCCESS) {
		fprintf(stderr, "Graph is not a DAG (topological sort failed)\n");
		igraph_vector_int_destroy(&topo_order);
		return -1;
	}

	igraph_vector_int_resize(levels, n);
	igraph_vector_int_null(levels);

	igraph_vector_int_t out_neis;
	if (igraph_vector_int_init(&out_neis, 0) != IGRAPH_SUCCESS) {
		fprintf(stderr, "Failed to initialize out_neis vector\n");
		igraph_vector_int_destroy(&topo_order);
		return -1;
	}

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
void *main_path_prepare(ExecutionContext *ctx)
{
	igraph_t *graph = &ctx->app_state->current_graph.g;
	if (!ctx->app_state->current_graph.graph_initialized) {
		fprintf(stderr, "[%s] Graph not initialized\n", __func__);
		return NULL;
	}
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
		if (igraph_vector_int_init(&fas, 0) != IGRAPH_SUCCESS) {
			fprintf(stderr, "Failed to initialize fas vector\n");
			return NULL;
		}
		if (igraph_feedback_arc_set(graph, &fas, NULL, IGRAPH_FAS_APPROX_EADES) == IGRAPH_SUCCESS) {
			if (igraph_vector_int_size(&fas) > 0) {
				igraph_es_t es = igraph_ess_vector(&fas);
				igraph_delete_edges(graph, es);
				printf("Removed %d edges to make graph acyclic\n", (int)igraph_vector_int_size(&fas));
			}
		}
		igraph_vector_int_destroy(&fas);
	}

	MainPathPrep *prep = calloc(1, sizeof(*prep));
	if (!prep || igraph_vector_int_init(&prep->levels, 0) != IGRAPH_SUCCESS) {
		free(prep);
		return NULL;
	}
	igraph_integer_t max_level = calculate_dag_levels(graph, &prep->levels);
	if (max_level < 0) {
		igraph_vector_int_destroy(&prep->levels);
		free(prep);
		return NULL;
	}
	prep->num_levels = (int)max_level + 1;
	prep->node_count = igraph_vcount(graph);
	return prep;
}

void *compute_splc_animation(ExecutionContext *ctx)
{
	return main_path_prepare(ctx);
}

void free_main_path_prep(void *result_data)
{
	MainPathPrep *prep = result_data;
	if (!prep)
		return;
	igraph_vector_int_destroy(&prep->levels);
	free(prep);
}

// ============================================================================
// Apply: Refresh graph data and trigger SPLC buffer init via renderer update
// ============================================================================
void apply_splc_animation(ExecutionContext *ctx, void *result_data)
{
	if (!ctx || !ctx->app_state || !result_data)
		return;
	MainPathPrep *prep = result_data;

	AppState *state = ctx->app_state;
	GraphData *data = &state->current_graph;
	if (prep->node_count != (igraph_integer_t)data->node_count) {
		fprintf(stderr, "[apply_splc_animation] stale result\n");
		return;
	}

	graph_reset_emphasis(data);
	graph_animation_clear(&state->renderer);

	state->renderer.needsAttributeUpload = VK_TRUE;
	if (!graph_rebuild_edges(data)) {
		fprintf(stderr, "[apply_splc_animation] graph_rebuild_edges failed\n");
		return;
	}
	renderer_update_graph(&state->renderer, data);
	if (!renderer_init_criticality_buffers(&state->renderer, data, &prep->levels, prep->num_levels, CRIT_WEIGHT_UNIT) || !renderer_start_main_path_weighting(&state->renderer)) {
		fprintf(stderr, "[apply_splc_animation] failed to initialize live main-path weighting\n");
		return;
	}
	state->renderer.label.tree_needs_rebuild = true;

	printf("SPLC live weighting started (graph has %d levels)\n", prep->num_levels);

	igraph_vector_int_t indeg, outdeg;
	igraph_integer_t nv = igraph_vcount(&data->g);
	if (igraph_vector_int_init(&indeg, nv) != IGRAPH_SUCCESS) {
		fprintf(stderr, "Failed to initialize indeg vector\n");
		return;
	}
	if (igraph_vector_int_init(&outdeg, nv) != IGRAPH_SUCCESS) {
		fprintf(stderr, "Failed to initialize outdeg vector\n");
		igraph_vector_int_destroy(&indeg);
		return;
	}
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
	return !r->crit.active && !r->crit.readback_pending;
}
