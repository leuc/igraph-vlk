/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "graph/wrappers_criticality.h"

#include "app_state.h"
#include "graph/graph_color.h"
#include "graph/graph_core.h"
#include "graph/worker_thread.h"
#include "graph/wrappers_splc.h"
#include "ui/menu.h"
#include "vulkan/renderer.h"
#include "vulkan/renderer_anim.h"
#include "vulkan/renderer_criticality.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Budget f for the secondary basket B(f) of eq. 2.16, reported alongside the
// zero-criticality basket so the two can be compared on coverage. The
// continuous criticality is stored as a vertex attribute, so any other budget
// can be explored afterwards through Filter > Node.
#define CRIT_BASKET_BUDGET 0.01

typedef struct
{
	igraph_vector_int_t levels;
	int num_levels;
	uint32_t weight_mode;
	igraph_integer_t node_count;
} CritPrep;

// The GPU poll callback is handed an ExecutionContext carrying only the app
// state — not the worker's result — so the handful of values the finishing
// pass needs live here between apply and poll.
static struct
{
	bool active;
	uint32_t weight_mode;
	igraph_integer_t node_count;
	igraph_vector_int_t levels;
	bool levels_valid;
} g_crit_pending;

static void crit_pending_clear(void)
{
	if (g_crit_pending.levels_valid)
		igraph_vector_int_destroy(&g_crit_pending.levels);
	g_crit_pending.levels_valid = false;
	g_crit_pending.active = false;
}

static const char *crit_attr_name(uint32_t weight_mode)
{
	return weight_mode == CRIT_WEIGHT_SPE ? "criticality-spe" : "criticality-unit";
}

static const char *crit_basket_attr_name(uint32_t weight_mode)
{
	return weight_mode == CRIT_WEIGHT_SPE ? "basket-spe" : "basket-unit";
}

// ============================================================================
// Worker: force a DAG and assign levels
// ============================================================================

static void *crit_prepare(ExecutionContext *ctx, uint32_t weight_mode)
{
	const char *label = weight_mode == CRIT_WEIGHT_SPE ? "Criticality (SPE)" : "Criticality (unit)";

	worker_thread_set_status_message("Criticality: preparing DAG...");
	// Same directed check and feedback-arc-set removal the SPLC animation
	// needs; reused rather than copied, since both require an acyclic graph.
	igraph_t *graph = compute_splc_animation(ctx);
	if (!graph)
		return NULL;
	if (!ctx->running)
		return NULL;
	worker_thread_set_progress(0.4f);

	worker_thread_set_status_message("Criticality: assigning levels...");
	CritPrep *prep = calloc(1, sizeof(CritPrep));
	if (!prep) {
		fprintf(stderr, "[%s] allocation failed\n", label);
		return NULL;
	}
	if (igraph_vector_int_init(&prep->levels, 0) != IGRAPH_SUCCESS) {
		fprintf(stderr, "[%s] failed to initialize levels vector\n", label);
		free(prep);
		return NULL;
	}

	igraph_integer_t max_level = calculate_dag_levels(graph, &prep->levels);
	if (max_level < 0) {
		fprintf(stderr, "[%s] level assignment failed (graph is not a DAG)\n", label);
		igraph_vector_int_destroy(&prep->levels);
		free(prep);
		return NULL;
	}

	prep->num_levels = (int)max_level + 1;
	prep->weight_mode = weight_mode;
	prep->node_count = igraph_vcount(graph);

	char msg[128];
	snprintf(msg, sizeof(msg), "Criticality: %d levels, dispatching...", prep->num_levels);
	worker_thread_set_status_message(msg);
	worker_thread_set_progress(0.6f);

	return prep;
}

void *compute_criticality_unit(ExecutionContext *ctx)
{
	return crit_prepare(ctx, CRIT_WEIGHT_UNIT);
}

void *compute_criticality_spe(ExecutionContext *ctx)
{
	return crit_prepare(ctx, CRIT_WEIGHT_SPE);
}

// ============================================================================
// Main thread: build buffers and submit the four sweeps
// ============================================================================

void apply_criticality_basket(ExecutionContext *ctx, void *result_data)
{
	if (!ctx || !ctx->app_state || !result_data)
		return;
	CritPrep *prep = (CritPrep *)result_data;
	AppState *state = ctx->app_state;
	GraphData *graph = &state->current_graph;

	crit_pending_clear();

	if (prep->node_count != (igraph_integer_t)graph->node_count) {
		fprintf(stderr, "[Criticality] stale result (graph changed since compute), skipping apply\n");
		return;
	}

	// The worker may have deleted feedback-arc-set edges in place, so the
	// render buffers are refreshed here rather than waiting for the poll to
	// finish — otherwise they stay stale for every frame the GPU sweeps run.
	state->renderer.needsAttributeUpload = VK_TRUE;
	if (!graph_rebuild_edges(graph)) {
		fprintf(stderr, "[Criticality] graph_rebuild_edges failed\n");
		return;
	}
	renderer_update_graph(&state->renderer, graph);

	if (!renderer_init_criticality_buffers(&state->renderer, graph, &prep->levels, prep->num_levels, prep->weight_mode)) {
		fprintf(stderr, "[Criticality] failed to create GPU buffers\n");
		return;
	}
	if (!renderer_dispatch_criticality(&state->renderer)) {
		fprintf(stderr, "[Criticality] failed to dispatch\n");
		return;
	}

	if (igraph_vector_int_init_copy(&g_crit_pending.levels, &prep->levels) == IGRAPH_SUCCESS)
		g_crit_pending.levels_valid = true;
	g_crit_pending.weight_mode = prep->weight_mode;
	g_crit_pending.node_count = prep->node_count;
	g_crit_pending.active = true;
}

// ============================================================================
// Analysis on the readback values
// ============================================================================

// Sanity check available for free in unit mode: the generalised height under
// unit weights is exactly the longest-path level the host already assigned,
// so any disagreement means the CSR, the level ordering or the shader itself
// is wrong.
static void crit_check_unit_invariant(const float *height, const igraph_vector_int_t *levels, igraph_integer_t n)
{
	igraph_integer_t mismatches = 0;
	for (igraph_integer_t v = 0; v < n; v++)
		if (fabsf(height[v] - (float)VECTOR(*levels)[v]) > 0.5f)
			mismatches++;
	if (mismatches > 0)
		fprintf(stderr, "[Criticality] unit height disagrees with DAG levels for %lld of %lld nodes\n", (long long)mismatches, (long long)n);
}

static int crit_cmp_double(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;
	return (x > y) - (x < y);
}

// Threshold for B(f) of eq. 2.16: the r-th smallest criticality, r = floor(f*n).
static double crit_budget_threshold(const double *c, igraph_integer_t n, double budget)
{
	double *sorted = malloc(sizeof(double) * (size_t)n);
	if (!sorted)
		return 0.0;
	memcpy(sorted, c, sizeof(double) * (size_t)n);
	qsort(sorted, (size_t)n, sizeof(double), crit_cmp_double);

	igraph_integer_t r = (igraph_integer_t)(budget * (double)n);
	if (r < 0)
		r = 0;
	if (r >= n)
		r = n - 1;
	double threshold = sorted[r];
	free(sorted);
	return threshold;
}

static double crit_edge_weight(uint32_t weight_mode, const float *lnW, const float *lnX, igraph_integer_t u, igraph_integer_t v)
{
	if (weight_mode == CRIT_WEIGHT_SPE)
		return (double)lnW[u] + (double)lnX[v];
	return 1.0;
}

// Walks one optimal path. An edge (u, v) lies on a maximal-weight path iff
// h(u) + w(u,v) + d(v) == H, so the extraction is a direct read-off of the
// height/depth arrays rather than a search.
static igraph_vector_int_t *crit_extract_main_path(const igraph_t *g, const float *height, const float *depth, const float *lnW, const float *lnX, const double *c, igraph_integer_t n, uint32_t weight_mode, double H, double eps)
{
	igraph_vector_int_t *path = malloc(sizeof(igraph_vector_int_t));
	if (!path)
		return NULL;
	if (igraph_vector_int_init(path, 0) != IGRAPH_SUCCESS) {
		free(path);
		return NULL;
	}

	igraph_vector_int_t neis;
	if (igraph_vector_int_init(&neis, 0) != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(path);
		free(path);
		return NULL;
	}

	// Start from the lowest-criticality source (in-degree 0, height 0).
	igraph_integer_t start = -1;
	for (igraph_integer_t v = 0; v < n; v++) {
		igraph_neighbors(g, &neis, v, IGRAPH_IN, IGRAPH_LOOPS, IGRAPH_NO_MULTIPLE);
		if (igraph_vector_int_size(&neis) != 0)
			continue;
		if (start < 0 || c[v] < c[start])
			start = v;
	}
	if (start < 0) {
		igraph_vector_int_destroy(&neis);
		return path; // no sources: empty path
	}

	igraph_integer_t current = start;
	igraph_vector_int_push_back(path, current);

	for (igraph_integer_t step = 0; step < n; step++) {
		igraph_neighbors(g, &neis, current, IGRAPH_OUT, IGRAPH_LOOPS, IGRAPH_NO_MULTIPLE);
		igraph_integer_t next = -1;
		for (igraph_integer_t j = 0; j < igraph_vector_int_size(&neis); j++) {
			igraph_integer_t v = VECTOR(neis)[j];
			double total = (double)height[current] + crit_edge_weight(weight_mode, lnW, lnX, current, v) + (double)depth[v];
			if (fabs(total - H) > eps)
				continue;
			// Deterministic tie-break: lowest criticality, then lowest id.
			if (next < 0 || c[v] < c[next] || (c[v] == c[next] && v < next))
				next = v;
		}
		if (next < 0)
			break; // reached a sink, or no optimal continuation
		igraph_vector_int_push_back(path, next);
		current = next;
	}

	igraph_vector_int_destroy(&neis);
	return path;
}

static int crit_cmp_ranked(const void *a, const void *b)
{
	const double *x = (const double *)a, *y = (const double *)b;
	if (x[0] != y[0])
		return x[0] < y[0] ? -1 : 1;
	return x[1] < y[1] ? -1 : 1;
}

// Reveal order: lowest criticality first, so the core of the DAG appears
// before the periphery. Mirrors the rank upload in apply_path_cover_result.
static int *crit_ranks_by_criticality(const double *c, igraph_integer_t n)
{
	double *pairs = malloc(sizeof(double) * 2 * (size_t)n);
	int *ranks = malloc(sizeof(int) * (size_t)n);
	if (!pairs || !ranks) {
		free(pairs);
		free(ranks);
		return NULL;
	}
	for (igraph_integer_t i = 0; i < n; i++) {
		pairs[2 * i] = c[i];
		pairs[2 * i + 1] = (double)i;
	}
	qsort(pairs, (size_t)n, sizeof(double) * 2, crit_cmp_ranked);
	for (igraph_integer_t i = 0; i < n; i++)
		ranks[(igraph_integer_t)pairs[2 * i + 1]] = (int)i;
	free(pairs);
	return ranks;
}

static void crit_store_results(GraphData *graph, uint32_t weight_mode, const double *c, const igraph_vector_int_t *basket_flags, const igraph_vector_int_t *main_path_flags)
{
	igraph_integer_t n = igraph_vcount(&graph->g);

	igraph_vector_t values;
	if (igraph_vector_init(&values, n) == IGRAPH_SUCCESS) {
		for (igraph_integer_t v = 0; v < n; v++)
			VECTOR(values)[v] = c[v];
		graph_cache_store_vertex_attr(&graph->g, crit_attr_name(weight_mode), &values);
		igraph_vector_destroy(&values);
	}
	graph_cache_store_vertex_attr_int(&graph->g, crit_basket_attr_name(weight_mode), basket_flags);
	graph_cache_store_vertex_attr_int(&graph->g, "main-path", main_path_flags);
}

// The SPLC animation accumulates only the forward traversal count W_u, so the
// edge weights it leaves behind are half of the paper's SPC weight
// G(spc)_vu = W_u * X_v (eq. 2.5a). The backward count is exactly what the lnX
// sweep produces, so the true weights get written here, in both forms:
//
//   spe-weight  ln(W_u) + ln(X_v), the SPE weight of eq. 2.8a
//   spc-weight  exp of the above — the literal traversal count, which section
//               2.6 warns grows factorially and so overflows on real networks
//
// Storing both makes the overflow visible instead of silent: when spc-weight
// is infinite, spe-weight is still exact.
static void crit_store_path_count_weights(GraphData *graph, const float *lnW, const float *lnX)
{
	igraph_integer_t m = igraph_ecount(&graph->g);
	if (m == 0)
		return;

	igraph_vector_t spe, spc;
	if (igraph_vector_init(&spe, m) != IGRAPH_SUCCESS)
		return;
	if (igraph_vector_init(&spc, m) != IGRAPH_SUCCESS) {
		igraph_vector_destroy(&spe);
		return;
	}

	igraph_integer_t overflowed = 0;
	for (igraph_integer_t e = 0; e < m; e++) {
		igraph_integer_t from, to;
		igraph_edge(&graph->g, e, &from, &to);
		double log_weight = (double)lnW[from] + (double)lnX[to];
		VECTOR(spe)[e] = log_weight;
		double linear = exp(log_weight);
		if (!isfinite(linear))
			overflowed++;
		VECTOR(spc)[e] = linear;
	}

	graph_cache_store_edge_attr(&graph->g, "spe-weight", &spe);
	graph_cache_store_edge_attr(&graph->g, "spc-weight", &spc);
	igraph_vector_destroy(&spe);
	igraph_vector_destroy(&spc);

	if (overflowed > 0)
		fprintf(stderr, "[Criticality] %lld of %lld SPC traversal counts overflowed to infinity (arXiv:2512.12355 §2.6); use spe-weight instead\n", (long long)overflowed, (long long)m);
}

// ============================================================================
// GPU poll: finish the analysis once the sweeps land
// ============================================================================

bool poll_criticality_gpu(ExecutionContext *ctx)
{
	if (!ctx || !ctx->app_state)
		return true;
	if (!g_crit_pending.active)
		return true;

	AppState *state = ctx->app_state;
	Renderer *r = &state->renderer;
	if (!renderer_criticality_ready(r))
		return false;

	GraphData *graph = &state->current_graph;
	igraph_integer_t n = g_crit_pending.node_count;
	uint32_t weight_mode = g_crit_pending.weight_mode;

	if (n != (igraph_integer_t)graph->node_count) {
		fprintf(stderr, "[Criticality] graph changed while the GPU was busy, discarding results\n");
		crit_pending_clear();
		return true;
	}

	float *height = NULL, *depth = NULL, *lnW = NULL, *lnX = NULL;
	if (!renderer_readback_criticality(r, &height, &depth, &lnW, &lnX)) {
		fprintf(stderr, "[Criticality] readback failed\n");
		free(height);
		free(depth);
		free(lnW);
		free(lnX);
		crit_pending_clear();
		return true;
	}

	if (weight_mode == CRIT_WEIGHT_UNIT && g_crit_pending.levels_valid)
		crit_check_unit_invariant(height, &g_crit_pending.levels, n);

	// H (eq. 2.14) and criticality (eq. 2.15)
	double H = 0.0;
	for (igraph_integer_t v = 0; v < n; v++) {
		double total = (double)height[v] + (double)depth[v];
		if (total > H)
			H = total;
	}

	double *c = malloc(sizeof(double) * (size_t)n);
	if (!c) {
		free(height);
		free(depth);
		free(lnW);
		free(lnX);
		crit_pending_clear();
		return true;
	}
	for (igraph_integer_t v = 0; v < n; v++) {
		double value = H - (double)height[v] - (double)depth[v];
		c[v] = value > 0.0 ? value : 0.0;
	}

	// Unit criticality is a small exact integer in fp32; SPE criticality is a
	// sum of logarithms, so it needs a tolerance scaled to the DAG height.
	double eps = (weight_mode == CRIT_WEIGHT_SPE) ? fmax(1e-4 * H, 1e-4) : 0.5;

	// Baskets (eq. 2.16)
	igraph_vector_int_t basket_flags;
	if (igraph_vector_int_init(&basket_flags, n) != IGRAPH_SUCCESS) {
		free(c);
		free(height);
		free(depth);
		free(lnW);
		free(lnX);
		crit_pending_clear();
		return true;
	}
	igraph_integer_t zero_basket_size = 0;
	for (igraph_integer_t v = 0; v < n; v++) {
		int member = c[v] <= eps ? 1 : 0;
		VECTOR(basket_flags)[v] = member;
		zero_basket_size += member;
	}

	double budget_threshold = crit_budget_threshold(c, n, CRIT_BASKET_BUDGET);
	igraph_integer_t budget_basket_size = 0;
	for (igraph_integer_t v = 0; v < n; v++)
		if (c[v] <= budget_threshold)
			budget_basket_size++;

	// Main path, and its coverage by each basket (eq. 2.17)
	igraph_vector_int_t *path = crit_extract_main_path(&graph->g, height, depth, lnW, lnX, c, n, weight_mode, H, eps);
	igraph_vector_int_t main_path_flags;
	igraph_vector_int_init(&main_path_flags, n);
	double coverage_zero = 0.0, coverage_budget = 0.0;
	igraph_integer_t path_len = 0;
	if (path) {
		path_len = igraph_vector_int_size(path);
		igraph_integer_t in_zero = 0, in_budget = 0;
		for (igraph_integer_t i = 0; i < path_len; i++) {
			igraph_integer_t v = VECTOR(*path)[i];
			VECTOR(main_path_flags)[v] = 1;
			if (VECTOR(basket_flags)[v])
				in_zero++;
			if (c[v] <= budget_threshold)
				in_budget++;
		}
		if (path_len > 0) {
			coverage_zero = (double)in_zero / (double)path_len;
			coverage_budget = (double)in_budget / (double)path_len;
		}
		igraph_vector_int_destroy(path);
		free(path);
	}

	crit_store_results(graph, weight_mode, c, &basket_flags, &main_path_flags);
	crit_store_path_count_weights(graph, lnW, lnX);

	const char *mode_label = weight_mode == CRIT_WEIGHT_SPE ? "SPE" : "unit";
	printf("Criticality (%s): H = %.4f, zero basket %lld/%lld nodes (%.1f%%), "
		   "B(%.0f%%) = %lld nodes, main path %lld nodes, coverage zero = %.3f, budget = %.3f\n",
		   mode_label, H, (long long)zero_basket_size, (long long)n, 100.0 * (double)zero_basket_size / (double)n, 100.0 * CRIT_BASKET_BUDGET, (long long)budget_basket_size, (long long)path_len, coverage_zero, coverage_budget);

	char msg[192];
	snprintf(msg, sizeof(msg), "Criticality (%s): basket %lld nodes, main path %lld, coverage %.2f", mode_label, (long long)zero_basket_size, (long long)path_len, coverage_zero);
	worker_thread_set_status_message(msg);

	// Reveal the core first, and dim everything outside the basket so it
	// stays visually distinct once the reveal finishes.
	graph_reset_emphasis(graph);
	renderer_anim_reset_nodes(r, graph);
	renderer_anim_reset_edges(r);
	int *ranks = crit_ranks_by_criticality(c, n);
	if (ranks) {
		uint32_t *from = malloc(sizeof(uint32_t) * graph->edge_count);
		if (from) {
			for (uint32_t i = 0; i < graph->edge_count; i++)
				from[i] = graph->edges[i].from;
			renderer_anim_upload_node_ranks(r, ranks, graph->node_count, 3.0f);
			renderer_anim_upload_edge_from(r, from, graph->edge_count);
			renderer_anim_reset_timer(r);
			free(from);
		}
		free(ranks);
	}

	for (igraph_integer_t v = 0; v < n; v++)
		if (!VECTOR(basket_flags)[v])
			graph->nodes[v].emphasis = EMPHASIS_DIMMED;

	graph_detect_filterable_attrs(graph);
	menu_populate_attribute_filters(state->app_ctx.menu.root, graph);
	renderer_update_graph(r, graph);
	r->label.tree_needs_rebuild = true;

	igraph_vector_int_destroy(&basket_flags);
	igraph_vector_int_destroy(&main_path_flags);
	free(c);
	free(height);
	free(depth);
	free(lnW);
	free(lnX);
	crit_pending_clear();
	return true;
}

void free_criticality_result(void *result_data)
{
	if (!result_data)
		return;
	CritPrep *prep = (CritPrep *)result_data;
	igraph_vector_int_destroy(&prep->levels);
	free(prep);
}
