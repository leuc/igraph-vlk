/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "graph/wrappers_path_cover.h"

#include "app_state.h"
#include "graph/graph_animation.h"
#include "graph/graph_color.h"
#include "graph/graph_core.h"
#include "graph/worker_thread.h"
#include "ui/menu.h"
#include "vulkan/renderer.h"

#include <igraph.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
	int *ranks;
	igraph_integer_t node_count;
	bool is_antichain;
} PathCoverResult;

// Directed/cycle check mirrors Main Path preparation's block exactly:
// deletes any feedback-arc-set edges in place on the live graph to force a
// DAG, since igraph_minimum_path_cover requires one.
static bool path_cover_ensure_dag(igraph_t *graph, const char *label)
{
	if (!igraph_is_directed(graph)) {
		fprintf(stderr, "[%s] requires a directed graph\n", label);
		return false;
	}

	igraph_bool_t is_dag = false;
	igraph_is_dag(graph, &is_dag);
	if (!is_dag) {
		igraph_vector_int_t fas;
		if (igraph_vector_int_init(&fas, 0) != IGRAPH_SUCCESS) {
			fprintf(stderr, "[%s] failed to initialize fas vector\n", label);
			return false;
		}
		igraph_vector_t weights;
		bool has_weights = graph_build_edge_weights(graph, &weights);
		igraph_error_t fas_ret = igraph_feedback_arc_set(graph, &fas, has_weights ? &weights : NULL, IGRAPH_FAS_APPROX_EADES);
		if (has_weights)
			igraph_vector_destroy(&weights);
		if (fas_ret == IGRAPH_SUCCESS) {
			if (igraph_vector_int_size(&fas) > 0) {
				igraph_es_t es = igraph_ess_vector(&fas);
				igraph_delete_edges(graph, es);
				fprintf(stderr, "[%s] removed %d edges to make graph acyclic\n", label, (int)igraph_vector_int_size(&fas));
			}
		}
		igraph_vector_int_destroy(&fas);
	}

	return true;
}

// Walks each path/chain in cover in order, first-touch-assigning an
// incrementing reveal rank per vertex (vertices shared between paths keep
// the rank from whichever path reaches them first). Mirrors
// graph_animation_play_bfs's ranks[order[i]] = i pattern.
static int *ranks_from_cover(const igraph_vector_int_list_t *cover, igraph_integer_t vcount)
{
	int *ranks = malloc(sizeof(int) * vcount);
	if (!ranks)
		return NULL;
	for (igraph_integer_t i = 0; i < vcount; i++)
		ranks[i] = -5;

	int counter = 0;
	igraph_integer_t num_paths = igraph_vector_int_list_size(cover);
	for (igraph_integer_t p = 0; p < num_paths; p++) {
		igraph_vector_int_t *path = igraph_vector_int_list_get_ptr(cover, p);
		igraph_integer_t len = igraph_vector_int_size(path);
		for (igraph_integer_t k = 0; k < len; k++) {
			igraph_integer_t v = VECTOR(*path)[k];
			if (ranks[v] == -5)
				ranks[v] = counter++;
		}
	}

	return ranks;
}

static PathCoverResult *path_cover_result_alloc(int *ranks, igraph_integer_t node_count, bool is_antichain)
{
	PathCoverResult *result = malloc(sizeof(PathCoverResult));
	if (!result) {
		free(ranks);
		return NULL;
	}
	result->ranks = ranks;
	result->node_count = node_count;
	result->is_antichain = is_antichain;
	return result;
}

void *compute_min_path_cover_trigger(ExecutionContext *ctx)
{
	if (!ctx->app_state->current_graph.graph_initialized) {
		fprintf(stderr, "[MinPathCover] Graph not initialized\n");
		return NULL;
	}
	igraph_t *graph = &ctx->app_state->current_graph.g;
	igraph_integer_t vcount = igraph_vcount(graph);
	if (vcount == 0)
		return NULL;

	worker_thread_set_status_message("Minimum Path Cover: preparing DAG...");
	if (!path_cover_ensure_dag(graph, "MinPathCover"))
		return NULL;
	if (!ctx->running)
		return NULL;
	worker_thread_set_progress(0.3f);

	worker_thread_set_status_message("Minimum Path Cover: solving flow...");
	igraph_vector_int_list_t cover;
	if (igraph_vector_int_list_init(&cover, 0) != IGRAPH_SUCCESS) {
		fprintf(stderr, "[MinPathCover] failed to initialize cover list\n");
		return NULL;
	}
	igraph_integer_t width = 0;
	igraph_error_t err = igraph_minimum_path_cover(graph, &cover, &width, NULL, IGRAPH_MPC_REDUCTION_GREEDY, IGRAPH_MPC_SOLVER_MAXFLOW_REDUCTION, NULL, NULL, NULL);
	if (err != IGRAPH_SUCCESS) {
		fprintf(stderr, "[MinPathCover] igraph_minimum_path_cover failed\n");
		igraph_vector_int_list_destroy(&cover);
		return NULL;
	}
	if (!ctx->running) {
		igraph_vector_int_list_destroy(&cover);
		return NULL;
	}
	worker_thread_set_progress(0.7f);

	int *ranks = ranks_from_cover(&cover, vcount);
	igraph_vector_int_list_destroy(&cover);
	if (!ranks) {
		fprintf(stderr, "[MinPathCover] ranks alloc failed\n");
		return NULL;
	}

	char msg[128];
	snprintf(msg, sizeof(msg), "Minimum Path Cover: %lld paths, %lld vertices", (long long)width, (long long)vcount);
	worker_thread_set_status_message(msg);
	worker_thread_set_progress(1.0f);

	return path_cover_result_alloc(ranks, vcount, false);
}

void *compute_max_antichain_trigger(ExecutionContext *ctx)
{
	if (!ctx->app_state->current_graph.graph_initialized) {
		fprintf(stderr, "[MaxAntichain] Graph not initialized\n");
		return NULL;
	}
	igraph_t *graph = &ctx->app_state->current_graph.g;
	igraph_integer_t vcount = igraph_vcount(graph);
	if (vcount == 0)
		return NULL;

	worker_thread_set_status_message("Maximum Antichain: preparing DAG...");
	if (!path_cover_ensure_dag(graph, "MaxAntichain"))
		return NULL;
	if (!ctx->running)
		return NULL;
	worker_thread_set_progress(0.2f);

	worker_thread_set_status_message("Maximum Antichain: solving flow...");
	igraph_vector_int_t flow, demand;
	if (igraph_vector_int_init(&flow, 0) != IGRAPH_SUCCESS) {
		fprintf(stderr, "[MaxAntichain] failed to initialize flow vector\n");
		return NULL;
	}
	if (igraph_vector_int_init(&demand, 0) != IGRAPH_SUCCESS) {
		fprintf(stderr, "[MaxAntichain] failed to initialize demand vector\n");
		igraph_vector_int_destroy(&flow);
		return NULL;
	}
	igraph_vector_int_list_t cover;
	if (igraph_vector_int_list_init(&cover, 0) != IGRAPH_SUCCESS) {
		fprintf(stderr, "[MaxAntichain] failed to initialize cover list\n");
		igraph_vector_int_destroy(&flow);
		igraph_vector_int_destroy(&demand);
		return NULL;
	}

	igraph_t flow_network;
	igraph_integer_t width = 0;
	igraph_error_t err = igraph_minimum_path_cover(graph, &cover, &width, NULL, IGRAPH_MPC_REDUCTION_GREEDY, IGRAPH_MPC_SOLVER_MAXFLOW_REDUCTION, &flow_network, &flow, &demand);
	igraph_vector_int_list_destroy(&cover);
	if (err != IGRAPH_SUCCESS) {
		fprintf(stderr, "[MaxAntichain] igraph_minimum_path_cover failed\n");
		igraph_vector_int_destroy(&flow);
		igraph_vector_int_destroy(&demand);
		return NULL;
	}
	if (!ctx->running) {
		igraph_destroy(&flow_network);
		igraph_vector_int_destroy(&flow);
		igraph_vector_int_destroy(&demand);
		return NULL;
	}
	worker_thread_set_progress(0.7f);

	worker_thread_set_status_message("Maximum Antichain: extracting cut...");
	igraph_vector_int_t antichain;
	if (igraph_vector_int_init(&antichain, 0) != IGRAPH_SUCCESS) {
		fprintf(stderr, "[MaxAntichain] failed to initialize antichain vector\n");
		igraph_destroy(&flow_network);
		igraph_vector_int_destroy(&flow);
		igraph_vector_int_destroy(&demand);
		return NULL;
	}
	err = igraph_maximum_antichain(&flow_network, &flow, &demand, &antichain);
	igraph_destroy(&flow_network);
	igraph_vector_int_destroy(&flow);
	igraph_vector_int_destroy(&demand);
	if (err != IGRAPH_SUCCESS) {
		fprintf(stderr, "[MaxAntichain] igraph_maximum_antichain failed\n");
		igraph_vector_int_destroy(&antichain);
		return NULL;
	}

	int *ranks = malloc(sizeof(int) * vcount);
	if (!ranks) {
		fprintf(stderr, "[MaxAntichain] ranks alloc failed\n");
		igraph_vector_int_destroy(&antichain);
		return NULL;
	}
	// Everyone fills in at rank 1; the antichain leads the reveal at rank 0.
	for (igraph_integer_t i = 0; i < vcount; i++)
		ranks[i] = 1;
	igraph_integer_t antichain_size = igraph_vector_int_size(&antichain);
	for (igraph_integer_t k = 0; k < antichain_size; k++)
		ranks[VECTOR(antichain)[k]] = 0;

	igraph_vector_int_destroy(&antichain);

	// Persist membership as a vertex attribute (1 = antichain member, 0 = other)
	// so it survives filtering/GraphML export, mirroring community-* membership.
	igraph_vector_int_t membership;
	if (igraph_vector_int_init(&membership, vcount) != IGRAPH_SUCCESS) {
		fprintf(stderr, "[MaxAntichain] failed to initialize membership vector\n");
		free(ranks);
		return NULL;
	}
	for (igraph_integer_t i = 0; i < vcount; i++)
		VECTOR(membership)[i] = (ranks[i] == 0) ? 1 : 0;
	graph_cache_store_vertex_attr_int(graph, "antichain", &membership);
	igraph_vector_int_destroy(&membership);

	char msg[128];
	snprintf(msg, sizeof(msg), "Maximum Antichain: %lld vertices", (long long)antichain_size);
	worker_thread_set_status_message(msg);
	worker_thread_set_progress(1.0f);

	return path_cover_result_alloc(ranks, vcount, true);
}

void *compute_min_chain_cover_trigger(ExecutionContext *ctx)
{
	if (!ctx->app_state->current_graph.graph_initialized) {
		fprintf(stderr, "[MinChainCover] Graph not initialized\n");
		return NULL;
	}
	igraph_t *graph = &ctx->app_state->current_graph.g;
	igraph_integer_t vcount = igraph_vcount(graph);
	if (vcount == 0)
		return NULL;

	worker_thread_set_status_message("Minimum Chain Cover: preparing DAG...");
	if (!path_cover_ensure_dag(graph, "MinChainCover"))
		return NULL;
	if (!ctx->running)
		return NULL;
	worker_thread_set_progress(0.2f);

	worker_thread_set_status_message("Minimum Chain Cover: solving flow...");
	igraph_vector_int_list_t cover;
	if (igraph_vector_int_list_init(&cover, 0) != IGRAPH_SUCCESS) {
		fprintf(stderr, "[MinChainCover] failed to initialize cover list\n");
		return NULL;
	}
	igraph_integer_t width = 0;
	igraph_error_t err = igraph_minimum_path_cover(graph, &cover, &width, NULL, IGRAPH_MPC_REDUCTION_GREEDY, IGRAPH_MPC_SOLVER_MAXFLOW_REDUCTION, NULL, NULL, NULL);
	if (err != IGRAPH_SUCCESS) {
		fprintf(stderr, "[MinChainCover] igraph_minimum_path_cover failed\n");
		igraph_vector_int_list_destroy(&cover);
		return NULL;
	}
	if (!ctx->running) {
		igraph_vector_int_list_destroy(&cover);
		return NULL;
	}
	worker_thread_set_progress(0.5f);

	worker_thread_set_status_message("Minimum Chain Cover: building chains...");
	igraph_vector_int_list_t chain_cover;
	if (igraph_vector_int_list_init(&chain_cover, 0) != IGRAPH_SUCCESS) {
		fprintf(stderr, "[MinChainCover] failed to initialize chain_cover list\n");
		igraph_vector_int_list_destroy(&cover);
		return NULL;
	}
	err = igraph_minimum_chain_cover(graph, &cover, &chain_cover);
	igraph_vector_int_list_destroy(&cover);
	if (err != IGRAPH_SUCCESS) {
		fprintf(stderr, "[MinChainCover] igraph_minimum_chain_cover failed\n");
		igraph_vector_int_list_destroy(&chain_cover);
		return NULL;
	}
	worker_thread_set_progress(0.8f);

	int *ranks = ranks_from_cover(&chain_cover, vcount);
	igraph_vector_int_list_destroy(&chain_cover);
	if (!ranks) {
		fprintf(stderr, "[MinChainCover] ranks alloc failed\n");
		return NULL;
	}

	char msg[128];
	snprintf(msg, sizeof(msg), "Minimum Chain Cover: %lld chains, %lld vertices", (long long)width, (long long)vcount);
	worker_thread_set_status_message(msg);
	worker_thread_set_progress(1.0f);

	return path_cover_result_alloc(ranks, vcount, false);
}

// Matches apply_kcore_tree_trigger's shape, plus Main Path weighting's
// edge-rebuild step (the worker may have deleted feedback-arc-set edges in
// place to force a DAG).
void apply_path_cover_result(ExecutionContext *ctx, void *result_data)
{
	if (!ctx || !ctx->app_state || !result_data)
		return;
	PathCoverResult *result = (PathCoverResult *)result_data;
	AppState *state = ctx->app_state;
	GraphData *graph = &state->current_graph;

	if (result->node_count != (igraph_integer_t)graph->node_count) {
		fprintf(stderr, "[PathCover] stale result (graph changed since compute), skipping apply\n");
		return;
	}

	graph_reset_emphasis(graph);
	graph_animation_clear(&state->renderer, graph);

	state->renderer.needsAttributeUpload = VK_TRUE;
	if (!graph_rebuild_edges(graph)) {
		fprintf(stderr, "[apply_path_cover_result] graph_rebuild_edges failed\n");
		return;
	}

	GraphAnimationRequest request = {.node_steps = result->ranks, .duration = 3.0f};
	graph_animation_play(&state->renderer, graph, &request);

	// Non-antichain vertices are dimmed (not overwritten) so the antichain
	// (rank 0) stays visually distinct after the reveal finishes, while
	// still showing whatever color an earlier command (e.g. community
	// coloring) assigned — and it's non-destructive, so the next command
	// that calls graph_reset_emphasis() restores full brightness.
	if (result->is_antichain) {
		for (igraph_integer_t i = 0; i < (igraph_integer_t)graph->node_count; i++)
			if (result->ranks[i] != 0)
				graph->nodes[i].emphasis = EMPHASIS_DIMMED;

		// The worker just wrote the 'antichain' vertex attribute; refresh
		// Filter > Node so it shows up right away (mirrors apply_cd_index).
		graph_detect_filterable_attrs(graph);
		menu_populate_attribute_filters(state->app_ctx.menu.root, graph);
	}

	renderer_update_graph(&state->renderer, graph);
	state->renderer.label.tree_needs_rebuild = true;
}

void free_path_cover_result(void *result_data)
{
	if (!result_data)
		return;
	PathCoverResult *result = (PathCoverResult *)result_data;
	free(result->ranks);
	free(result);
}
