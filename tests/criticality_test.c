/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "criticality_test_harness.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define BASE_NODES 8
#define BASE_EDGES 10
#define BASE_LEVELS 5
#define LN2 0.69314718056f
#define LN5 1.60943791243f

static const CritNode base_out_nodes[BASE_NODES] = {{0, 3}, {3, 1}, {4, 1}, {5, 2}, {7, 1}, {8, 1}, {9, 0}, {9, 1}};
static const CritEdge base_out_edges[BASE_EDGES] = {{1, 0}, {2, 1}, {7, 2}, {3, 3}, {3, 4}, {4, 5}, {5, 6}, {6, 7}, {6, 8}, {6, 9}};
static const CritNode base_in_nodes[BASE_NODES] = {{0, 0}, {0, 1}, {1, 1}, {2, 2}, {4, 1}, {5, 1}, {6, 3}, {9, 1}};
static const CritEdge base_in_edges[BASE_EDGES] = {{0, 0}, {0, 1}, {1, 3}, {2, 4}, {3, 5}, {3, 6}, {4, 7}, {5, 8}, {7, 9}, {0, 2}};
static const uint32_t base_level_nodes[BASE_NODES] = {0, 1, 2, 7, 3, 4, 5, 6};
static const uint32_t base_level_offsets[BASE_LEVELS] = {0, 1, 4, 5, 7};
static const uint32_t base_level_sizes[BASE_LEVELS] = {1, 3, 1, 2, 1};
static const GraphSpec base_graph = {BASE_NODES, BASE_EDGES, BASE_LEVELS, base_out_nodes, base_out_edges, base_in_nodes, base_in_edges, base_level_nodes, base_level_offsets, base_level_sizes};

#define PAPER_NODES 7
#define PAPER_EDGES 9
#define PAPER_LEVELS 6

static const CritNode paper_out_nodes[PAPER_NODES] = {{0, 2}, {2, 1}, {3, 2}, {5, 1}, {6, 2}, {8, 1}, {9, 0}};
static const CritEdge paper_out_edges[PAPER_EDGES] = {{1, 0}, {5, 1}, {2, 2}, {3, 3}, {4, 4}, {6, 5}, {5, 6}, {6, 7}, {6, 8}};
static const CritNode paper_in_nodes[PAPER_NODES] = {{0, 0}, {0, 1}, {1, 1}, {2, 1}, {3, 1}, {4, 2}, {6, 3}};
static const CritEdge paper_in_edges[PAPER_EDGES] = {{0, 0}, {1, 2}, {2, 3}, {2, 4}, {0, 1}, {4, 6}, {3, 5}, {4, 7}, {5, 8}};
static const uint32_t paper_level_nodes[PAPER_NODES] = {0, 1, 2, 3, 4, 5, 6};
static const uint32_t paper_level_offsets[PAPER_LEVELS] = {0, 1, 2, 3, 5, 6};
static const uint32_t paper_level_sizes[PAPER_LEVELS] = {1, 1, 1, 2, 1, 1};
static const GraphSpec paper_graph = {PAPER_NODES, PAPER_EDGES, PAPER_LEVELS, paper_out_nodes, paper_out_edges, paper_in_nodes, paper_in_edges, paper_level_nodes, paper_level_offsets, paper_level_sizes};

// Runs SPLC/UNIT/SPC/SPE on base_graph and checks lnW/lnX/height/depth/analysis/presentation/
// predecessor/basket/path/header against hand-derived expected values — the full pipeline for
// every non-NPPC weight mode, sharing one small fixture.
static int check_base_mode(Harness *h, uint32_t mode, const char *name)
{
	if (harness_run(h, &base_graph, mode) != 0)
		return 1;
	float lnw[BASE_NODES];
	float lnx[BASE_NODES];
	float height[BASE_NODES];
	float depth[BASE_NODES];
	unsigned char result_bytes[sizeof(CritResultHeader) + sizeof(uint32_t) * (BASE_EDGES + 3 * BASE_NODES)];
	unsigned char animation_bytes[sizeof(EdgeAnimHeader) + sizeof(EdgeAnim) * BASE_EDGES];
	if (download(h, BUF_LNW, lnw, sizeof(lnw)) || download(h, BUF_LNX, lnx, sizeof(lnx)) || download(h, BUF_HEIGHT, height, sizeof(height)) || download(h, BUF_DEPTH, depth, sizeof(depth)) || download(h, BUF_RESULT, result_bytes, sizeof(result_bytes)) || download(h, BUF_EDGE_ANIM, animation_bytes, sizeof(animation_bytes)))
		return 1;
	static const float regular_lnw[BASE_NODES] = {0, 0, 0, LN2, LN2, LN2, LN5, 0};
	static const float splc_lnw[BASE_NODES] = {0, LN2, LN2, LN5, 1.79175946923f, 1.79175946923f, 2.70805020110f, LN2};
	static const float suffix[BASE_NODES] = {LN5, LN2, LN2, LN2, 0, 0, 0, 0};
	static const float zero[BASE_NODES] = {0};
	static const float unit_height[BASE_NODES] = {0, 1, 1, 2, 3, 3, 4, 1};
	static const float unit_depth[BASE_NODES] = {4, 3, 3, 2, 1, 1, 0, 1};
	static const float splc_height[BASE_NODES] = {0, 1, 1, 3, 8, 8, 14, 1};
	static const float splc_depth[BASE_NODES] = {14, 13, 13, 11, 6, 6, 0, 2};
	static const float spc_height[BASE_NODES] = {0, 2, 2, 4, 6, 6, 8, 1};
	static const float spc_depth[BASE_NODES] = {8, 6, 6, 4, 2, 2, 0, 1};
	static const float spe_height[BASE_NODES] = {0, LN2, LN2, 2 * LN2, 3 * LN2, 3 * LN2, 4 * LN2, 0};
	static const float spe_depth[BASE_NODES] = {4 * LN2, 3 * LN2, 3 * LN2, 2 * LN2, LN2, LN2, 0, 0};
	const float *expected_height = mode == CRIT_WEIGHT_UNIT ? unit_height : (mode == CRIT_WEIGHT_SPLC ? splc_height : (mode == CRIT_WEIGHT_SPC ? spc_height : spe_height));
	const float *expected_depth = mode == CRIT_WEIGHT_UNIT ? unit_depth : (mode == CRIT_WEIGHT_SPLC ? splc_depth : (mode == CRIT_WEIGHT_SPC ? spc_depth : spe_depth));
	float maximum = mode == CRIT_WEIGHT_UNIT ? 4.0f : (mode == CRIT_WEIGHT_SPLC ? 14.0f : (mode == CRIT_WEIGHT_SPC ? 8.0f : 4 * LN2));
	CritResultHeader *header = (CritResultHeader *)result_bytes;
	uint32_t *data = result_data(result_bytes);
	EdgeAnimHeader *animation_header = (EdgeAnimHeader *)animation_bytes;
	EdgeAnim *animation_edges = (EdgeAnim *)(animation_bytes + sizeof(*animation_header));
	float expected_weights[BASE_EDGES];
	float expected_strengths[BASE_EDGES];
	float got_weights[BASE_EDGES];
	float got_strengths[BASE_EDGES];
	float expected_strength_max = 0.0f;
	for (uint32_t e = 0; e < BASE_EDGES; e++) {
		uint32_t source = e < 3 ? 0 : (e < 4 ? 1 : (e < 5 ? 2 : (e < 7 ? 3 : (e < 8 ? 4 : (e < 9 ? 5 : 7)))));
		uint32_t target = base_out_edges[e].node;
		float log_weight = mode == CRIT_WEIGHT_SPLC ? splc_lnw[source] : regular_lnw[source] + ((mode == CRIT_WEIGHT_SPC || mode == CRIT_WEIGHT_SPE) ? suffix[target] : 0.0f);
		expected_weights[e] = mode == CRIT_WEIGHT_UNIT ? 1.0f : (mode == CRIT_WEIGHT_SPE ? log_weight : expf(log_weight));
		expected_strengths[e] = mode == CRIT_WEIGHT_UNIT ? 1.0f : (mode == CRIT_WEIGHT_SPE ? fmaxf(1.0f + log_weight, 0.0f) : (log_weight > 20.0f ? log_weight : logf(1.0f + expf(log_weight))));
		got_weights[e] = bits_float(data[crit_result_weight_offset() + e]);
		got_strengths[e] = animation_edges[e].strength;
		expected_strength_max = fmaxf(expected_strength_max, expected_strengths[e]);
	}
	static const uint32_t predecessors[BASE_NODES] = {UINT32_MAX, 0, 0, 1, 3, 3, 4, 0};
	static const uint32_t basket[BASE_NODES] = {1, 1, 1, 1, 1, 1, 1, 0};
	static const uint32_t path[BASE_NODES] = {1, 1, 0, 1, 1, 0, 1, 0};
	int failures = 0;
	failures += check_float_array("lnW", lnw, mode == CRIT_WEIGHT_SPLC ? splc_lnw : regular_lnw, BASE_NODES);
	failures += check_float_array("lnX", lnx, mode == CRIT_WEIGHT_SPC || mode == CRIT_WEIGHT_SPE ? suffix : zero, BASE_NODES);
	failures += check_float_array("analysis", got_weights, expected_weights, BASE_EDGES);
	failures += check_float_array("presentation", got_strengths, expected_strengths, BASE_EDGES);
	failures += check_float_array("height", height, expected_height, BASE_NODES);
	failures += check_float_array("depth", depth, expected_depth, BASE_NODES);
	failures += check_uint_array("predecessor", data + crit_result_predecessor_offset(BASE_EDGES), predecessors, BASE_NODES);
	failures += check_uint_array("basket", data + crit_result_basket_offset(BASE_EDGES, BASE_NODES), basket, BASE_NODES);
	failures += check_uint_array("path", data + crit_result_path_offset(BASE_EDGES, BASE_NODES), path, BASE_NODES);
	if (header->status != 0 || fabsf(bits_float(header->criticality_max_bits) - maximum) > TOLERANCE || fabsf(bits_float(header->sink_height_bits) - maximum) > TOLERANCE || header->sink_node != 6 || fabsf(bits_float(animation_header->strength_max_bits) - expected_strength_max) > TOLERANCE) {
		fprintf(stderr, "%s header mismatch\n", name);
		failures++;
	}
	printf("%s complete pipeline: %s\n", name, failures == 0 ? "ok" : "failed");
	return failures;
}

// Cross-checks against Hummon & Doreian's own published NPPC weights for their 1989 paper's
// 7-node DNA-citation DAG (Figure 3, pp. 49-51) — an external, hand-computable exactness oracle.
static int check_nppc_paper_oracle(Harness *h)
{
	if (harness_upload_graph(h, &paper_graph, CRIT_NPPC_TILE_BUDGET_BYTES) || harness_run(h, &paper_graph, CRIT_WEIGHT_NPPC))
		return 1;
	float lnw[PAPER_NODES];
	float lnx[PAPER_NODES];
	float height[PAPER_NODES];
	float depth[PAPER_NODES];
	unsigned char result_bytes[sizeof(CritResultHeader) + sizeof(uint32_t) * (PAPER_EDGES + 3 * PAPER_NODES)];
	if (download(h, BUF_LNW, lnw, sizeof(lnw)) || download(h, BUF_LNX, lnx, sizeof(lnx)) || download(h, BUF_HEIGHT, height, sizeof(height)) || download(h, BUF_DEPTH, depth, sizeof(depth)) || download(h, BUF_RESULT, result_bytes, sizeof(result_bytes)))
		return 1;

	static const float expected_lnw[PAPER_NODES] = {0.0f, 0.69314718056f, 1.09861228867f, 1.38629436112f, 1.38629436112f, 1.60943791243f, 1.94591014906f};
	static const float expected_lnx[PAPER_NODES] = {1.94591014906f, 1.79175946923f, 1.60943791243f, 0.69314718056f, 1.09861228867f, 0.69314718056f, 0.0f};
	static const float expected_height[PAPER_NODES] = {0.0f, 6.0f, 16.0f, 22.0f, 25.0f, 33.0f, 38.0f};
	static const float expected_depth[PAPER_NODES] = {38.0f, 32.0f, 22.0f, 4.0f, 13.0f, 5.0f, 0.0f};
	static const float expected_weights[PAPER_EDGES] = {6.0f, 2.0f, 10.0f, 6.0f, 9.0f, 4.0f, 8.0f, 4.0f, 5.0f};
	static const char *arc_names[PAPER_EDGES] = {"3->5", "3->21", "5->12", "12->15", "12->20", "15->22", "20->21", "20->22", "21->22"};
	static const uint32_t expected_predecessors[PAPER_NODES] = {UINT32_MAX, 0, 1, 2, 2, 4, 5};
	static const uint32_t expected_basket[PAPER_NODES] = {1, 1, 1, 0, 1, 1, 1};
	static const uint32_t expected_path[PAPER_NODES] = {1, 1, 1, 0, 1, 1, 1};

	CritResultHeader *header = (CritResultHeader *)result_bytes;
	uint32_t *data = result_data(result_bytes);
	int failures = 0;
	failures += check_float_array("NPPC lnW", lnw, expected_lnw, PAPER_NODES);
	failures += check_float_array("NPPC lnX", lnx, expected_lnx, PAPER_NODES);
	failures += check_float_array("NPPC height", height, expected_height, PAPER_NODES);
	failures += check_float_array("NPPC depth", depth, expected_depth, PAPER_NODES);
	for (uint32_t e = 0; e < PAPER_EDGES; e++) {
		float weight = bits_float(data[crit_result_weight_offset() + e]);
		if (fabsf(weight - expected_weights[e]) > TOLERANCE) {
			fprintf(stderr, "NPPC %s: got %.6f expected %.6f\n", arc_names[e], (double)weight, (double)expected_weights[e]);
			failures++;
		}
	}
	failures += check_uint_array("NPPC predecessor", data + crit_result_predecessor_offset(PAPER_EDGES), expected_predecessors, PAPER_NODES);
	failures += check_uint_array("NPPC basket", data + crit_result_basket_offset(PAPER_EDGES, PAPER_NODES), expected_basket, PAPER_NODES);
	failures += check_uint_array("NPPC path", data + crit_result_path_offset(PAPER_EDGES, PAPER_NODES), expected_path, PAPER_NODES);
	if (header->status != 0 || fabsf(bits_float(header->criticality_max_bits) - 38.0f) > TOLERANCE || fabsf(bits_float(header->sink_height_bits) - 38.0f) > TOLERANCE || header->sink_node != 6) {
		fprintf(stderr, "NPPC paper oracle header mismatch\n");
		failures++;
	}
	printf("NPPC 1989 paper oracle: %s\n", failures == 0 ? "ok" : "failed");
	return failures;
}

static int check_reachability_sizing(void)
{
	int failures = 0;
	size_t generous_budget = (size_t)1024 * 1024 * 1024;
	if (crit_reachability_word_count(7) != 1)
		failures++;
	if (crit_reachability_tile_word_count(7, generous_budget, generous_budget) != 1)
		failures++;
	if (crit_reachability_tile_word_count(33, generous_budget, generous_budget) != 2)
		failures++;
	// A budget that only fits one word/node (4*33 bytes) must clamp the tile width to 1, not 2.
	if (crit_reachability_tile_word_count(33, sizeof(uint32_t) * 33, generous_budget) != 1)
		failures++;
	// The device's maxStorageBufferRange must clamp just as the budget does.
	if (crit_reachability_tile_word_count(33, generous_budget, sizeof(uint32_t) * 33) != 1)
		failures++;
	if (crit_reachability_tile_buffer_size(33, 1) != 132 || crit_reachability_tile_buffer_size(33, 2) != 264)
		failures++;
	if (crit_total_count_buffer_size(33) != 132)
		failures++;
	printf("NPPC reachability tile sizing: %s\n", failures == 0 ? "ok" : "failed");
	return failures;
}

static int check_nppc_chain_parallel(Harness *h)
{
	static const CritNode out_nodes[4] = {{0, 2}, {2, 1}, {3, 1}, {4, 0}};
	static const CritEdge out_edges[4] = {{1, 0}, {1, 1}, {2, 2}, {3, 3}};
	static const CritNode in_nodes[4] = {{0, 0}, {0, 2}, {2, 1}, {3, 1}};
	static const CritEdge in_edges[4] = {{0, 0}, {0, 1}, {1, 2}, {2, 3}};
	static const uint32_t levels[4] = {0, 1, 2, 3};
	static const uint32_t offsets[4] = {0, 1, 2, 3};
	static const uint32_t sizes[4] = {1, 1, 1, 1};
	GraphSpec graph = {4, 4, 4, out_nodes, out_edges, in_nodes, in_edges, levels, offsets, sizes};
	if (harness_upload_graph(h, &graph, CRIT_NPPC_TILE_BUDGET_BYTES) || harness_run(h, &graph, CRIT_WEIGHT_NPPC))
		return 1;
	unsigned char bytes[sizeof(CritResultHeader) + sizeof(uint32_t) * 16];
	if (download(h, BUF_RESULT, bytes, sizeof(bytes)))
		return 1;
	uint32_t *data = result_data(bytes);
	static const float expected[4] = {3.0f, 3.0f, 4.0f, 3.0f};
	int failures = 0;
	for (uint32_t e = 0; e < 4; e++) {
		float weight = bits_float(data[crit_result_weight_offset() + e]);
		if (fabsf(weight - expected[e]) > TOLERANCE)
			failures++;
	}
	printf("NPPC chain and parallel edges: %s\n", failures == 0 ? "ok" : "failed");
	return failures;
}

// A 40-node chain forces crit_reachability_word_count() to 2 words, and forcing the tile budget
// down to one word/node makes crit_reachability_tile_word_count() return 1 — genuinely exercising
// the tiles-outer/levels-inner multi-pass accumulation (not just the single-tile collapse every
// other NPPC test above exercises, since all of them have <=32 nodes). For a pure chain 0->1->...->N-1,
// |R^{inv*}(k)| = k+1 and |R^*(k+1)| = N-1-k, so edge (k, k+1)'s exact NPPC weight is a closed form:
// (k+1)*(N-1-k), independently checkable without a bitset.
static int check_nppc_multi_tile(Harness *h)
{
	const uint32_t node_count = 40;
	const uint32_t edge_count = node_count - 1;
	ChainGraph cg;
	if (!build_chain_graph(&cg, node_count, 1)) {
		free_chain_graph(&cg);
		return 1;
	}
	GraphSpec graph = {node_count, edge_count, node_count, cg.out_nodes, cg.out_edges, cg.in_nodes, cg.in_edges, cg.levels, cg.offsets, cg.sizes};
	int failures = harness_upload_graph(h, &graph, sizeof(uint32_t)) || harness_run(h, &graph, CRIT_WEIGHT_NPPC);
	if (!failures && h->tile_word_count != 1) {
		fprintf(stderr, "multi-tile NPPC: expected tile_word_count=1, got %u\n", h->tile_word_count);
		failures++;
	}
	unsigned char *result = malloc(crit_result_buffer_size(edge_count, node_count));
	if (!failures && result && !download(h, BUF_RESULT, result, crit_result_buffer_size(edge_count, node_count))) {
		uint32_t *data = result_data(result);
		for (uint32_t e = 0; e < edge_count; e++) {
			float expected = (float)(e + 1) * (float)(node_count - 1 - e);
			float weight = bits_float(data[crit_result_weight_offset() + e]);
			// Weights here (up to ~400) are far larger than the other NPPC fixtures (<=10), so the
			// same relative float32 log/exp round-trip error needs a scale-aware tolerance, not the
			// fixed absolute TOLERANCE used for small-magnitude checks elsewhere in this file.
			float scale_tolerance = fmaxf(TOLERANCE, expected * 1e-5f);
			if (fabsf(weight - expected) > scale_tolerance) {
				fprintf(stderr, "multi-tile NPPC edge %u: got %.6f expected %.6f\n", e, (double)weight, (double)expected);
				failures++;
			}
		}
	} else if (!failures) {
		failures++;
	}
	free(result);
	free_chain_graph(&cg);
	printf("NPPC forced multi-tile chain: %s\n", failures == 0 ? "ok" : "failed");
	return failures;
}

static int check_zero_weight_tie(Harness *h)
{
	static const CritNode out_nodes[3] = {{0, 1}, {1, 1}, {2, 0}};
	static const CritEdge out_edges[2] = {{2, 1}, {2, 0}};
	static const CritNode in_nodes[3] = {{0, 0}, {0, 0}, {0, 2}};
	static const CritEdge in_edges[2] = {{1, 0}, {0, 1}};
	static const uint32_t levels[3] = {0, 1, 2};
	static const uint32_t offsets[2] = {0, 2};
	static const uint32_t sizes[2] = {2, 1};
	GraphSpec graph = {3, 2, 2, out_nodes, out_edges, in_nodes, in_edges, levels, offsets, sizes};
	if (harness_upload_graph(h, &graph, CRIT_NPPC_TILE_BUDGET_BYTES) || harness_run(h, &graph, CRIT_WEIGHT_SPE))
		return 1;
	unsigned char bytes[sizeof(CritResultHeader) + sizeof(uint32_t) * 11];
	if (download(h, BUF_RESULT, bytes, sizeof(bytes)))
		return 1;
	CritResultHeader *header = (CritResultHeader *)bytes;
	uint32_t *data = result_data(bytes);
	uint32_t expected_predecessors[3] = {UINT32_MAX, UINT32_MAX, 1};
	uint32_t expected_path[3] = {0, 1, 1};
	int failures = check_uint_array("zero predecessor", data + crit_result_predecessor_offset(2), expected_predecessors, 3) + check_uint_array("zero path", data + crit_result_path_offset(2, 3), expected_path, 3);
	if (header->status != 0 || header->sink_node != 2)
		failures++;
	printf("zero-weight SPE tie: %s\n", failures == 0 ? "ok" : "failed");
	return failures;
}

static int check_empty_edges(Harness *h)
{
	static const CritNode nodes[2] = {{0, 0}, {0, 0}};
	static const uint32_t levels[2] = {0, 1};
	static const uint32_t offsets[1] = {0};
	static const uint32_t sizes[1] = {2};
	GraphSpec graph = {2, 0, 1, nodes, NULL, nodes, NULL, levels, offsets, sizes};
	if (harness_upload_graph(h, &graph, CRIT_NPPC_TILE_BUDGET_BYTES) || harness_run(h, &graph, CRIT_WEIGHT_UNIT))
		return 1;
	unsigned char bytes[sizeof(CritResultHeader) + sizeof(uint32_t) * 6];
	if (download(h, BUF_RESULT, bytes, sizeof(bytes)))
		return 1;
	CritResultHeader *header = (CritResultHeader *)bytes;
	uint32_t *data = result_data(bytes);
	uint32_t expected_basket[2] = {1, 1};
	uint32_t expected_path[2] = {1, 0};
	int failures = check_uint_array("empty basket", data + crit_result_basket_offset(0, 2), expected_basket, 2) + check_uint_array("empty path", data + crit_result_path_offset(0, 2), expected_path, 2);
	if (header->status != 0 || header->sink_node != 0)
		failures++;
	printf("empty-edge sink tie: %s\n", failures == 0 ? "ok" : "failed");
	if (harness_run(h, &graph, CRIT_WEIGHT_NPPC) || download(h, BUF_RESULT, bytes, sizeof(bytes)))
		return failures + 1;
	header = (CritResultHeader *)bytes;
	data = result_data(bytes);
	failures += check_uint_array("empty NPPC basket", data + crit_result_basket_offset(0, 2), expected_basket, 2);
	failures += check_uint_array("empty NPPC path", data + crit_result_path_offset(0, 2), expected_path, 2);
	if (header->status != 0 || header->sink_node != 0)
		failures++;
	printf("empty-edge NPPC tie: %s\n", failures == 0 ? "ok" : "failed");
	return failures;
}

// A 140-node chain with 2 parallel edges/step doubles the path count each level, forcing SPC/SPLC
// weights past float range — checks RESULT_OVERFLOW is flagged (not RESULT_INVALID) and every
// presentation strength stays finite despite the raw analysis weight overflowing.
static int check_overflow(Harness *h, uint32_t mode, const char *name)
{
	const uint32_t node_count = 140;
	const uint32_t edge_count = 2 * (node_count - 1);
	ChainGraph cg;
	if (!build_chain_graph(&cg, node_count, 2)) {
		free_chain_graph(&cg);
		return 1;
	}
	GraphSpec graph = {node_count, edge_count, node_count, cg.out_nodes, cg.out_edges, cg.in_nodes, cg.in_edges, cg.levels, cg.offsets, cg.sizes};
	int failures = harness_upload_graph(h, &graph, CRIT_NPPC_TILE_BUDGET_BYTES) || harness_run(h, &graph, mode);
	unsigned char *result = malloc(crit_result_buffer_size(edge_count, node_count));
	unsigned char *animation = malloc(sizeof(EdgeAnimHeader) + sizeof(EdgeAnim) * edge_count);
	if (!failures && result && animation && !download(h, BUF_RESULT, result, crit_result_buffer_size(edge_count, node_count)) && !download(h, BUF_EDGE_ANIM, animation, sizeof(EdgeAnimHeader) + sizeof(EdgeAnim) * edge_count)) {
		CritResultHeader *header = (CritResultHeader *)result;
		EdgeAnim *edges = (EdgeAnim *)(animation + sizeof(EdgeAnimHeader));
		if ((header->status & CRIT_RESULT_OVERFLOW) == 0 || (header->status & CRIT_RESULT_INVALID) != 0)
			failures++;
		for (uint32_t e = 0; e < edge_count; e++)
			if (!isfinite(edges[e].strength)) {
				failures++;
				break;
			}
	} else if (!failures) {
		failures++;
	}
	free(result);
	free(animation);
	free_chain_graph(&cg);
	printf("%s overflow: %s\n", name, failures == 0 ? "ok" : "failed");
	return failures;
}

int main(void)
{
	Harness harness;
	if (harness_init(&harness) != 0) {
		harness_destroy(&harness);
		return 1;
	}
	if (harness_upload_graph(&harness, &base_graph, CRIT_NPPC_TILE_BUDGET_BYTES) != 0) {
		harness_destroy(&harness);
		return 1;
	}
	int failures = 0;
	failures += check_base_mode(&harness, CRIT_WEIGHT_SPLC, "SPLC");
	failures += check_base_mode(&harness, CRIT_WEIGHT_UNIT, "Unit");
	failures += check_base_mode(&harness, CRIT_WEIGHT_SPC, "SPC");
	failures += check_base_mode(&harness, CRIT_WEIGHT_SPE, "SPE");
	failures += check_nppc_paper_oracle(&harness);
	failures += check_reachability_sizing();
	failures += check_nppc_chain_parallel(&harness);
	failures += check_nppc_multi_tile(&harness);
	failures += check_zero_weight_tie(&harness);
	failures += check_empty_edges(&harness);
	failures += check_overflow(&harness, CRIT_WEIGHT_SPLC, "SPLC");
	failures += check_overflow(&harness, CRIT_WEIGHT_SPC, "SPC");
	harness_destroy(&harness);
	if (failures != 0) {
		fprintf(stderr, "criticality_test: %d failures\n", failures);
		return 1;
	}
	printf("criticality_test: all checks passed\n");
	return 0;
}
