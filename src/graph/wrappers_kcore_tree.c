/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "graph/wrappers_kcore_tree.h"

#include "app_state.h"
#include "graph/dyn_core_tree.h"
#include "graph/graph_color.h"
#include "graph/worker_thread.h"
#include "vulkan/renderer_anim.h"
#include <igraph.h>
#include <stdio.h>
#include <stdlib.h>

// Edges are replayed into the hierarchy in fixed-size chunks so the worker
// thread can report progress and observe cancellation between chunks,
// instead of blocking for the whole graph in one call. The chunk size is
// fixed, NOT adapted to wall-clock time: igraph_add_edges' per-call cost
// scales with the CURRENT size of the graph (its internal indices are
// rebuilt on mutation), so shrinking the batch size on a "slow" chunk makes
// the per-edge overhead worse, not better — confirmed empirically (a
// standalone harness with zero dyn_core_tree.c involvement reproduced the
// exact same stall from small-batch igraph_add_edges calls alone; a fixed
// ~4096-edge batch eliminated it, ~1000x). Matches dyn_core_tree_init's own
// internal bootstrap chunk size.
#define KCORE_TREE_BOOTSTRAP_CHUNK 4096

typedef struct
{
	int *ranks;
	igraph_integer_t node_count;
} KCoreTreeResult;

void *compute_kcore_tree_trigger(ExecutionContext *ctx)
{
	if (!ctx->app_state->current_graph.graph_initialized) {
		fprintf(stderr, "[KCoreTree] Graph not initialized\n");
		return NULL;
	}
	igraph_t *graph = &ctx->app_state->current_graph.g;
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_integer_t ecount = igraph_ecount(graph);
	if (vcount == 0)
		return NULL;

	worker_thread_set_status_message("K-Core Tree: building hierarchy...");
	fprintf(stderr, "[KCoreTree] starting: %lld vertices, %lld edges\n", (long long)vcount, (long long)ecount);

	igraph_t seed;
	if (igraph_empty(&seed, vcount, IGRAPH_UNDIRECTED) != IGRAPH_SUCCESS) {
		fprintf(stderr, "[KCoreTree] failed to create seed graph\n");
		return NULL;
	}
	DynCoreTree *ct = dyn_core_tree_init(&seed);
	igraph_destroy(&seed);
	if (!ct) {
		fprintf(stderr, "[KCoreTree] dyn_core_tree_init failed\n");
		return NULL;
	}

	igraph_t scratch;
	if (igraph_empty(&scratch, vcount, IGRAPH_UNDIRECTED) != IGRAPH_SUCCESS) {
		fprintf(stderr, "[KCoreTree] failed to create scratch graph\n");
		dyn_core_tree_destroy(ct);
		return NULL;
	}

	// Grow the scratch graph and the hierarchy in lockstep: dyn_core_tree_on_edges
	// requires the graph to already contain the batch being inserted.
	igraph_vector_int_t batch;
	if (igraph_vector_int_init(&batch, KCORE_TREE_BOOTSTRAP_CHUNK * 2) != IGRAPH_SUCCESS) {
		fprintf(stderr, "[KCoreTree] batch alloc failed\n");
		igraph_destroy(&scratch);
		dyn_core_tree_destroy(ct);
		return NULL;
	}

	for (igraph_integer_t base = 0; base < ecount;) {
		if (!ctx->running) {
			fprintf(stderr, "[KCoreTree] cancelled at edge %lld/%lld\n", (long long)base, (long long)ecount);
			igraph_vector_int_destroy(&batch);
			igraph_destroy(&scratch);
			dyn_core_tree_destroy(ct);
			return NULL;
		}

		igraph_integer_t n = (ecount - base < KCORE_TREE_BOOTSTRAP_CHUNK) ? (ecount - base) : KCORE_TREE_BOOTSTRAP_CHUNK;
		igraph_vector_int_resize(&batch, n * 2);
		for (igraph_integer_t i = 0; i < n; i++) {
			igraph_integer_t from, to;
			igraph_edge(graph, base + i, &from, &to);
			VECTOR(batch)[2 * i] = from;
			VECTOR(batch)[2 * i + 1] = to;
		}

		bool ok = igraph_add_edges(&scratch, &batch, NULL) == IGRAPH_SUCCESS;
		if (ok)
			ok = dyn_core_tree_on_edges(ct, &scratch, &batch, NULL);
		if (!ok) {
			fprintf(stderr, "[KCoreTree] failed processing batch at edge %lld\n", (long long)base);
			igraph_vector_int_destroy(&batch);
			igraph_destroy(&scratch);
			dyn_core_tree_destroy(ct);
			return NULL;
		}

		base += n;
		float frac = (float)base / (float)ecount;
		worker_thread_set_progress(frac * 0.9f);
		char msg[160];
		snprintf(msg, sizeof(msg), "K-Core Tree: %lld/%lld edges (%d tree nodes)", (long long)base, (long long)ecount, dyn_core_tree_node_count(ct));
		worker_thread_set_status_message(msg);
	}
	igraph_vector_int_destroy(&batch);
	igraph_destroy(&scratch);

	worker_thread_set_status_message("K-Core Tree: computing reveal order...");
	fprintf(stderr, "[KCoreTree] built tree: %d nodes for %lld vertices\n", dyn_core_tree_node_count(ct), (long long)vcount);

	// BFS over the TREE structure (not the graph): assigns each vertex a
	// reveal rank equal to its visitation order, exactly mirroring
	// renderer_anim_compute_bfs's ranks[order[i]] = i pattern, so it drives
	// the same shader-side reveal-by-rank animation BFS/DFS/topo use.
	int *ranks = malloc(sizeof(int) * vcount);
	if (!ranks) {
		fprintf(stderr, "[KCoreTree] ranks alloc failed\n");
		dyn_core_tree_destroy(ct);
		return NULL;
	}
	for (igraph_integer_t i = 0; i < vcount; i++)
		ranks[i] = -5;

	igraph_vector_int_t queue;
	if (igraph_vector_int_init(&queue, 0) != IGRAPH_SUCCESS) {
		fprintf(stderr, "[KCoreTree] queue alloc failed\n");
		free(ranks);
		dyn_core_tree_destroy(ct);
		return NULL;
	}
	igraph_vector_int_push_back(&queue, DYN_CORE_TREE_ROOT);
	igraph_integer_t rank_counter = 0;
	igraph_integer_t head = 0;
	while (head < igraph_vector_int_size(&queue)) {
		if (!ctx->running) {
			fprintf(stderr, "[KCoreTree] cancelled during tree walk\n");
			igraph_vector_int_destroy(&queue);
			free(ranks);
			dyn_core_tree_destroy(ct);
			return NULL;
		}

		int node = VECTOR(queue)[head++];
		for (igraph_integer_t v = dyn_core_tree_first_member(ct, node); v != -1; v = dyn_core_tree_next_member(ct, v))
			ranks[v] = (int)rank_counter++;
		for (int child = dyn_core_tree_first_child(ct, node); child != -1; child = dyn_core_tree_next_sibling(ct, child))
			igraph_vector_int_push_back(&queue, child);
	}
	igraph_vector_int_destroy(&queue);
	dyn_core_tree_destroy(ct);

	fprintf(stderr, "[KCoreTree] ranked %lld/%lld vertices\n", (long long)rank_counter, (long long)vcount);

	worker_thread_set_progress(1.0f);
	worker_thread_set_status_message("K-Core Tree: done");

	KCoreTreeResult *result = malloc(sizeof(KCoreTreeResult));
	if (!result) {
		free(ranks);
		return NULL;
	}
	result->ranks = ranks;
	result->node_count = vcount;
	return result;
}

// Matches apply_bfs_trigger's exact shape/order: reset to the shared grey
// baseline, then upload the real ranks so the shader animates the reveal.
void apply_kcore_tree_trigger(ExecutionContext *ctx, void *result_data)
{
	if (!ctx || !ctx->app_state || !result_data)
		return;
	KCoreTreeResult *result = (KCoreTreeResult *)result_data;
	AppState *state = ctx->app_state;
	GraphData *graph = &state->current_graph;

	if (result->node_count != (igraph_integer_t)graph->node_count) {
		fprintf(stderr, "[KCoreTree] stale result (graph changed since compute), skipping apply\n");
		return;
	}

	graph_reset_emphasis(graph);
	renderer_anim_reset_nodes(&state->renderer, graph);
	renderer_anim_reset_edges(&state->renderer);

	uint32_t *from = malloc(sizeof(uint32_t) * graph->edge_count);
	for (igraph_integer_t i = 0; i < (igraph_integer_t)graph->edge_count; i++)
		from[i] = graph->edges[i].from;

	renderer_anim_upload_node_ranks(&state->renderer, result->ranks, graph->node_count, 3.0f);
	renderer_anim_upload_edge_from(&state->renderer, from, graph->edge_count);
	renderer_anim_reset_timer(&state->renderer);

	free(from);
}

void free_kcore_tree_result(void *result_data)
{
	if (!result_data)
		return;
	KCoreTreeResult *result = (KCoreTreeResult *)result_data;
	free(result->ranks);
	free(result);
}
