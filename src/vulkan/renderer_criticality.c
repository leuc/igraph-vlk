/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "vulkan/renderer_criticality.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "graph/graph_core.h"
#include "vulkan/buffers.h"
#include "vulkan/renderer_anim.h"
#include "vulkan/utils.h"

#define CRIT_WORKGROUP_SIZE 64

static float *crit_copy_values(Renderer *r, VkDeviceMemory mem, uint32_t n);

// ============================================================================
// Topology construction
// ============================================================================

// Builds a CSR adjacency in one direction. The same two-pass shape produces
// both the forward (IGRAPH_OUT) and reverse (IGRAPH_IN) adjacency needed by
// the gather sweeps.
static bool crit_build_csr(const igraph_t *g, igraph_integer_t n, igraph_neimode_t mode, CritNode **out_nodes, CritEdge **out_edges, uint32_t *out_edge_count)
{
	CritNode *nodes = calloc((size_t)n, sizeof(CritNode));
	if (!nodes)
		return false;

	igraph_integer_t m = igraph_ecount(g);
	for (igraph_integer_t e = 0; e < m; e++) {
		igraph_integer_t from, to;
		igraph_edge(g, e, &from, &to);
		nodes[mode == IGRAPH_OUT ? from : to].degree++;
	}

	uint32_t offset = 0;
	for (igraph_integer_t i = 0; i < n; i++) {
		nodes[i].edge_offset = offset;
		offset += nodes[i].degree;
	}

	CritEdge *edges = calloc(offset > 0 ? offset : 1, sizeof(CritEdge));
	if (!edges) {
		free(nodes);
		return false;
	}
	uint32_t *cursor = calloc((size_t)n, sizeof(uint32_t));
	if (!cursor) {
		free(edges);
		free(nodes);
		return false;
	}
	for (igraph_integer_t e = 0; e < m; e++) {
		igraph_integer_t from, to;
		igraph_edge(g, e, &from, &to);
		igraph_integer_t node = mode == IGRAPH_OUT ? from : to;
		igraph_integer_t neighbour = mode == IGRAPH_OUT ? to : from;
		uint32_t index = nodes[node].edge_offset + cursor[node]++;
		edges[index] = (CritEdge){(uint32_t)neighbour, (uint32_t)e};
	}
	free(cursor);

	*out_nodes = nodes;
	*out_edges = edges;
	*out_edge_count = offset;
	return true;
}

// Flattens the levels into a single permutation of node ids grouped by level,
// plus per-level (offset, size). Uploading this once is what lets every
// dispatch of the run be recorded into one command buffer.
static bool crit_build_level_permutation(CritComputeContext *ctx, const igraph_vector_int_t *levels, igraph_integer_t n, int num_levels, uint32_t **out_perm)
{
	uint32_t *perm = malloc(sizeof(uint32_t) * (size_t)(n > 0 ? n : 1));
	ctx->level_offsets = calloc((size_t)num_levels, sizeof(uint32_t));
	ctx->level_sizes = calloc((size_t)num_levels, sizeof(uint32_t));
	if (!perm || !ctx->level_offsets || !ctx->level_sizes) {
		free(perm);
		free(ctx->level_offsets);
		free(ctx->level_sizes);
		ctx->level_offsets = NULL;
		ctx->level_sizes = NULL;
		return false;
	}

	for (igraph_integer_t i = 0; i < n; i++) {
		int lvl = (int)VECTOR(*levels)[i];
		if (lvl >= 0 && lvl < num_levels)
			ctx->level_sizes[lvl]++;
	}

	uint32_t running = 0;
	for (int l = 0; l < num_levels; l++) {
		ctx->level_offsets[l] = running;
		running += ctx->level_sizes[l];
	}

	uint32_t *cursor = calloc((size_t)num_levels, sizeof(uint32_t));
	if (!cursor) {
		free(perm);
		free(ctx->level_offsets);
		free(ctx->level_sizes);
		ctx->level_offsets = NULL;
		ctx->level_sizes = NULL;
		return false;
	}
	for (igraph_integer_t i = 0; i < n; i++) {
		int lvl = (int)VECTOR(*levels)[i];
		if (lvl >= 0 && lvl < num_levels)
			perm[ctx->level_offsets[lvl] + cursor[lvl]++] = (uint32_t)i;
	}
	free(cursor);

	*out_perm = perm;
	return true;
}

// ============================================================================
// GPU resources
// ============================================================================

static void crit_destroy_buffer(VkDevice dev, VkBuffer *buf, VkDeviceMemory *mem)
{
	if (*buf != VK_NULL_HANDLE)
		vkDestroyBuffer(dev, *buf, NULL);
	if (*mem != VK_NULL_HANDLE)
		vkFreeMemory(dev, *mem, NULL);
	*buf = VK_NULL_HANDLE;
	*mem = VK_NULL_HANDLE;
}

void renderer_destroy_criticality_buffers(Renderer *r)
{
	CritComputeContext *ctx = &r->crit;
	VkDevice dev = r->core.device;

	crit_destroy_buffer(dev, &ctx->out_nodes_buffer, &ctx->out_nodes_memory);
	crit_destroy_buffer(dev, &ctx->out_edges_buffer, &ctx->out_edges_memory);
	crit_destroy_buffer(dev, &ctx->in_nodes_buffer, &ctx->in_nodes_memory);
	crit_destroy_buffer(dev, &ctx->in_edges_buffer, &ctx->in_edges_memory);
	crit_destroy_buffer(dev, &ctx->level_buffer, &ctx->level_memory);
	crit_destroy_buffer(dev, &ctx->lnw_buffer, &ctx->lnw_memory);
	crit_destroy_buffer(dev, &ctx->lnx_buffer, &ctx->lnx_memory);
	crit_destroy_buffer(dev, &ctx->height_buffer, &ctx->height_memory);
	crit_destroy_buffer(dev, &ctx->depth_buffer, &ctx->depth_memory);
	crit_destroy_buffer(dev, &ctx->edge_weights_buffer, &ctx->edge_weights_memory);

	free(ctx->level_offsets);
	free(ctx->level_sizes);
	ctx->level_offsets = NULL;
	ctx->level_sizes = NULL;
	ctx->num_levels = 0;
	ctx->node_count = 0;
	ctx->graph_edge_count = 0;
	ctx->active = false;
	ctx->readback_pending = false;
	ctx->selection_run = false;
	ctx->selection_ready = false;
	free(ctx->selection_flags);
	ctx->selection_flags = NULL;
}

void renderer_cancel_main_path(Renderer *r)
{
	if (!r)
		return;
	r->crit.active = false;
	r->crit.readback_pending = false;
	r->crit.selection_ready = false;
	free(r->crit.selection_flags);
	r->crit.selection_flags = NULL;
}

static void crit_write_descriptors(Renderer *r)
{
	CritComputeContext *ctx = &r->crit;
	VkDescriptorBufferInfo infos[] = {
		{ctx->out_nodes_buffer, 0, VK_WHOLE_SIZE}, {ctx->out_edges_buffer, 0, VK_WHOLE_SIZE}, {ctx->in_nodes_buffer, 0, VK_WHOLE_SIZE}, {ctx->in_edges_buffer, 0, VK_WHOLE_SIZE}, {ctx->level_buffer, 0, VK_WHOLE_SIZE}, {ctx->lnw_buffer, 0, VK_WHOLE_SIZE}, {ctx->lnx_buffer, 0, VK_WHOLE_SIZE}, {ctx->height_buffer, 0, VK_WHOLE_SIZE}, {ctx->depth_buffer, 0, VK_WHOLE_SIZE}, {r->anim.edge_buffer, 0, VK_WHOLE_SIZE}, {ctx->edge_weights_buffer, 0, VK_WHOLE_SIZE},
	};
	VkWriteDescriptorSet writes[11];
	for (uint32_t i = 0; i < 11; i++)
		writes[i] = VK_WRITE_DESC_BUFFER(r->descriptors.crit_set, i, &infos[i], VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
	vkUpdateDescriptorSets(r->core.device, 11, writes, 0, NULL);
}

bool renderer_init_criticality_buffers(Renderer *r, GraphData *graph, const igraph_vector_int_t *levels, int num_levels, uint32_t weight_mode, const igraph_vector_t *selection_weights)
{
	CritComputeContext *ctx = &r->crit;
	renderer_destroy_criticality_buffers(r);

	igraph_integer_t n = igraph_vcount(&graph->g);
	if (n == 0 || num_levels <= 0)
		return false;

	ctx->node_count = (uint32_t)n;
	ctx->num_levels = num_levels;
	ctx->weight_mode = weight_mode;
	ctx->graph_edge_count = (uint32_t)igraph_ecount(&graph->g);

	CritNode *out_nodes = NULL, *in_nodes = NULL;
	CritEdge *out_edges = NULL, *in_edges = NULL;
	uint32_t out_edge_count = 0, in_edge_count = 0;
	uint32_t *perm = NULL;

	if (!crit_build_csr(&graph->g, n, IGRAPH_OUT, &out_nodes, &out_edges, &out_edge_count))
		return false;
	if (!crit_build_csr(&graph->g, n, IGRAPH_IN, &in_nodes, &in_edges, &in_edge_count)) {
		free(out_nodes);
		free(out_edges);
		return false;
	}
	if (!crit_build_level_permutation(ctx, levels, n, num_levels, &perm)) {
		free(out_nodes);
		free(out_edges);
		free(in_nodes);
		free(in_edges);
		return false;
	}

	VkDevice dev = r->core.device;
	VkPhysicalDevice phys = r->core.physicalDevice;
	const VkBufferUsageFlags usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	VkDeviceSize node_size = sizeof(CritNode) * (size_t)n;
	VkDeviceSize out_edge_size = sizeof(CritEdge) * (out_edge_count > 0 ? out_edge_count : 1);
	VkDeviceSize in_edge_size = sizeof(CritEdge) * (in_edge_count > 0 ? in_edge_count : 1);
	VkDeviceSize level_size = sizeof(uint32_t) * (size_t)n;
	VkDeviceSize value_size = sizeof(float) * (size_t)n;

	VK_CREATE_HOST_BUFFER(dev, phys, node_size, usage, &ctx->out_nodes_buffer, &ctx->out_nodes_memory);
	VK_CREATE_HOST_BUFFER(dev, phys, out_edge_size, usage, &ctx->out_edges_buffer, &ctx->out_edges_memory);
	VK_CREATE_HOST_BUFFER(dev, phys, node_size, usage, &ctx->in_nodes_buffer, &ctx->in_nodes_memory);
	VK_CREATE_HOST_BUFFER(dev, phys, in_edge_size, usage, &ctx->in_edges_buffer, &ctx->in_edges_memory);
	VK_CREATE_HOST_BUFFER(dev, phys, level_size, usage, &ctx->level_buffer, &ctx->level_memory);
	VK_CREATE_HOST_BUFFER(dev, phys, value_size, usage, &ctx->lnw_buffer, &ctx->lnw_memory);
	VK_CREATE_HOST_BUFFER(dev, phys, value_size, usage, &ctx->lnx_buffer, &ctx->lnx_memory);
	VK_CREATE_HOST_BUFFER(dev, phys, value_size, usage, &ctx->height_buffer, &ctx->height_memory);
	VK_CREATE_HOST_BUFFER(dev, phys, value_size, usage, &ctx->depth_buffer, &ctx->depth_memory);
	VK_CREATE_HOST_BUFFER(dev, phys, sizeof(float) * (ctx->graph_edge_count > 0 ? ctx->graph_edge_count : 1), usage, &ctx->edge_weights_buffer, &ctx->edge_weights_memory);

	update_buffer(dev, ctx->out_nodes_memory, node_size, out_nodes);
	if (out_edge_count > 0)
		update_buffer(dev, ctx->out_edges_memory, sizeof(CritEdge) * out_edge_count, out_edges);
	update_buffer(dev, ctx->in_nodes_memory, node_size, in_nodes);
	if (in_edge_count > 0)
		update_buffer(dev, ctx->in_edges_memory, sizeof(CritEdge) * in_edge_count, in_edges);
	update_buffer(dev, ctx->level_memory, level_size, perm);

	float *zeros = calloc((size_t)n, sizeof(float));
	if (zeros) {
		update_buffer(dev, ctx->lnw_memory, value_size, zeros);
		update_buffer(dev, ctx->lnx_memory, value_size, zeros);
		update_buffer(dev, ctx->height_memory, value_size, zeros);
		update_buffer(dev, ctx->depth_memory, value_size, zeros);
		free(zeros);
	}
	if (selection_weights) {
		float *selection_values = calloc(ctx->graph_edge_count > 0 ? ctx->graph_edge_count : 1, sizeof(float));
		if (!selection_values) {
			renderer_destroy_criticality_buffers(r);
			return false;
		}
		for (uint32_t e = 0; e < ctx->graph_edge_count; e++)
			selection_values[e] = (float)VECTOR(*selection_weights)[e];
		update_buffer(dev, ctx->edge_weights_memory, sizeof(float) * (ctx->graph_edge_count > 0 ? ctx->graph_edge_count : 1), selection_values);
		free(selection_values);
	} else {
		float *zero_weights = calloc(ctx->graph_edge_count > 0 ? ctx->graph_edge_count : 1, sizeof(float));
		if (zero_weights) {
			update_buffer(dev, ctx->edge_weights_memory, sizeof(float) * (ctx->graph_edge_count > 0 ? ctx->graph_edge_count : 1), zero_weights);
			free(zero_weights);
		}
	}
	free(out_nodes);
	free(out_edges);
	free(in_nodes);
	free(in_edges);
	free(perm);

	crit_write_descriptors(r);

	return true;
}

static bool crit_start_animation(Renderer *r, const GraphData *graph, const igraph_vector_int_t *levels, RendererAnimOwner owner)
{
	CritComputeContext *ctx = &r->crit;
	uint32_t node_count = graph->node_count;
	uint32_t edge_count = graph->edge_count;
	RendererAnimNode *nodes = calloc(node_count > 0 ? node_count : 1, sizeof(RendererAnimNode));
	RendererAnimEdge *edges = calloc(edge_count > 0 ? edge_count : 1, sizeof(RendererAnimEdge));
	if (!nodes || !edges) {
		free(nodes);
		free(edges);
		return false;
	}
	for (uint32_t v = 0; v < node_count; v++) {
		int level = VECTOR(*levels)[v];
		nodes[v].reveal_at = renderer_anim_reveal_at(level, (float)ctx->level_interval);
	}
	bool all_zero = edge_count > 0;
	float max_strength = 0.0f;
	for (uint32_t e = 0; e < edge_count; e++) {
		edges[e].reveal_at = nodes[graph->edges[e].from].reveal_at;
		edges[e].strength = owner == RENDERER_ANIM_MAIN_PATH ? 0.0f : renderer_anim_host_strength(graph->edges[e].weight);
		if (edges[e].strength > 0.0f)
			all_zero = false;
		if (edges[e].strength > max_strength)
			max_strength = edges[e].strength;
	}
	if (owner == RENDERER_ANIM_HOST && all_zero) {
		max_strength = 1.0f;
		for (uint32_t e = 0; e < edge_count; e++)
			edges[e].strength = 1.0f;
	}
	RendererAnimClip clip = {
		.nodes = nodes,
		.edges = edges,
		.node_count = node_count,
		.edge_count = edge_count,
		.strength_max = max_strength,
		.fade = 0.3f,
		.reveal_mask = RENDERER_ANIM_REVEAL_NODES | RENDERER_ANIM_REVEAL_EDGES,
		.owner = owner,
	};
	renderer_anim_play(r, &clip);
	free(nodes);
	free(edges);
	crit_write_descriptors(r);
	return true;
}

bool renderer_start_main_path_weighting(Renderer *r, const GraphData *graph, const igraph_vector_int_t *levels)
{
	if (!r || !graph || !levels)
		return false;
	CritComputeContext *ctx = &r->crit;
	if (ctx->num_levels <= 0 || r->anim.edge_buffer == VK_NULL_HANDLE)
		return false;
	ctx->active = false;
	ctx->readback_pending = false;
	ctx->selection_run = false;
	ctx->stage = CRIT_STAGE_LNW;
	ctx->current_level = 0;
	ctx->level_interval = 5.0 / (double)ctx->num_levels;
	if (ctx->level_interval < 0.016)
		ctx->level_interval = 0.016;
	if (!crit_start_animation(r, graph, levels, RENDERER_ANIM_MAIN_PATH))
		return false;
	ctx->last_level_time = r->anim.data.time - ctx->level_interval;
	ctx->active = true;
	printf("[MainPath] start: method=%s levels=%d tick=%.3fs\n", ctx->weight_mode == CRIT_WEIGHT_SPLC ? "SPLC" : (ctx->weight_mode == CRIT_WEIGHT_UNIT ? "Unit" : (ctx->weight_mode == CRIT_WEIGHT_SPC ? "SPC" : "SPE")), ctx->num_levels, ctx->level_interval);
	return true;
}

bool renderer_start_main_path_selection(Renderer *r, const GraphData *graph, const igraph_vector_int_t *levels)
{
	if (!r || !graph || !levels)
		return false;
	CritComputeContext *ctx = &r->crit;
	if (ctx->num_levels <= 0 || r->anim.edge_buffer == VK_NULL_HANDLE)
		return false;
	ctx->active = false;
	ctx->readback_pending = false;
	ctx->selection_run = true;
	ctx->selection_ready = false;
	ctx->stage = CRIT_STAGE_HEIGHT;
	ctx->current_level = 0;
	ctx->level_interval = 5.0 / (double)ctx->num_levels;
	if (ctx->level_interval < 0.016)
		ctx->level_interval = 0.016;
	if (!crit_start_animation(r, graph, levels, RENDERER_ANIM_HOST))
		return false;
	ctx->last_level_time = r->anim.data.time - ctx->level_interval;
	ctx->active = true;
	printf("[MainPath] selection start: method=%u levels=%d tick=%.3fs\n", ctx->weight_mode, ctx->num_levels, ctx->level_interval);
	return true;
}

void renderer_dispatch_main_path_weight_level(Renderer *r, VkCommandBuffer cmd)
{
	CritComputeContext *ctx = &r->crit;
	if (!ctx->active)
		return;

	bool forward = ctx->stage == CRIT_STAGE_LNW || ctx->stage == CRIT_STAGE_HEIGHT;
	if ((forward && ctx->current_level >= ctx->num_levels) || (!forward && ctx->current_level < 0)) {
		if (ctx->selection_run && ctx->stage == CRIT_STAGE_HEIGHT) {
			ctx->stage = CRIT_STAGE_DEPTH;
			ctx->current_level = ctx->num_levels - 1;
			ctx->last_level_time = r->anim.data.time;
			return;
		}
		if (ctx->stage == CRIT_STAGE_LNW && (ctx->weight_mode == CRIT_WEIGHT_SPC || ctx->weight_mode == CRIT_WEIGHT_SPE)) {
			printf("[MainPath] forward sweep complete; reversing from sinks\n");
			ctx->stage = CRIT_STAGE_LNX;
			ctx->current_level = ctx->num_levels - 1;
		} else if ((ctx->stage == CRIT_STAGE_LNW || ctx->stage == CRIT_STAGE_LNX) && ctx->selection_run) {
			ctx->stage = CRIT_STAGE_HEIGHT;
			ctx->current_level = 0;
		} else if (ctx->stage == CRIT_STAGE_HEIGHT) {
			ctx->stage = CRIT_STAGE_DEPTH;
			ctx->current_level = ctx->num_levels - 1;
		} else {
			printf("[MainPath] compute complete; scheduling %s readback\n", ctx->selection_run ? "selection" : "weight");
			ctx->readback_pending = true;
			return;
		}
		ctx->last_level_time = r->anim.data.time;
		return;
	}

	int level = ctx->current_level;
	uint32_t count = ctx->level_sizes[level];
	printf("[MainPath] tick: method=%s phase=%s level=%d/%d nodes=%u\n", ctx->weight_mode == CRIT_WEIGHT_SPLC ? "SPLC" : (ctx->weight_mode == CRIT_WEIGHT_UNIT ? "Unit" : (ctx->weight_mode == CRIT_WEIGHT_SPC ? "SPC" : "SPE")), forward ? "forward" : "reverse", level, ctx->num_levels - 1, count);
	if (count > 0) {
		VkMemoryBarrier compute_barrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_SHADER_READ_BIT};
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &compute_barrier, 0, NULL, 0, NULL);
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, r->pipelines.compute_criticality);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, ctx->pipeline_layout, 0, 1, &r->descriptors.crit_set, 0, NULL);
		CritPushConstants pc = {.level_offset = ctx->level_offsets[level], .num_nodes_in_level = count, .stage = ctx->stage, .weight_mode = ctx->weight_mode};
		vkCmdPushConstants(cmd, ctx->pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
		vkCmdDispatch(cmd, (count + CRIT_WORKGROUP_SIZE - 1) / CRIT_WORKGROUP_SIZE, 1, 1);
		VkMemoryBarrier vertex_barrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_SHADER_READ_BIT};
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 1, &vertex_barrier, 0, NULL, 0, NULL);
	}
	ctx->current_level += forward ? 1 : -1;
	ctx->last_level_time = r->anim.data.time;
}

void renderer_readback_main_path_selection(Renderer *r)
{
	CritComputeContext *ctx = &r->crit;
	float *height = crit_copy_values(r, ctx->height_memory, ctx->node_count);
	float *depth = crit_copy_values(r, ctx->depth_memory, ctx->node_count);
	if (!height || !depth)
		goto done;
	float maximum = 0.0f;
	for (uint32_t v = 0; v < ctx->node_count; v++)
		maximum = fmaxf(maximum, height[v] + depth[v]);
	ctx->selection_flags = calloc(ctx->node_count, sizeof(int));
	if (ctx->selection_flags)
		for (uint32_t v = 0; v < ctx->node_count; v++)
			ctx->selection_flags[v] = fabsf(maximum - height[v] - depth[v]) < 1e-4f ? 1 : 0;
done:
	free(height);
	free(depth);
	ctx->selection_ready = ctx->selection_flags != NULL;
}

static float *crit_copy_values(Renderer *r, VkDeviceMemory mem, uint32_t n)
{
	VkDeviceSize size = sizeof(float) * (size_t)n;
	void *mapped = NULL;
	if (vkMapMemory(r->core.device, mem, 0, size, 0, &mapped) != VK_SUCCESS)
		return NULL;
	float *values = malloc(size);
	if (values)
		memcpy(values, mapped, size);
	vkUnmapMemory(r->core.device, mem);
	return values;
}

void renderer_readback_main_path_weights(Renderer *r, GraphData *graph)
{
	CritComputeContext *ctx = &r->crit;
	if (!graph || ctx->node_count == 0 || ctx->lnw_memory == VK_NULL_HANDLE || ctx->lnx_memory == VK_NULL_HANDLE)
		return;

	float *lnw = crit_copy_values(r, ctx->lnw_memory, ctx->node_count);
	float *lnx = crit_copy_values(r, ctx->lnx_memory, ctx->node_count);
	igraph_integer_t m = igraph_ecount(&graph->g);
	igraph_vector_t weights;
	if (!lnw || !lnx || igraph_vector_init(&weights, m) != IGRAPH_SUCCESS) {
		free(lnw);
		free(lnx);
		return;
	}

	float max_weight = 0.0f;
	double total = 0.0;
	for (igraph_integer_t e = 0; e < m; e++) {
		igraph_integer_t from, to;
		igraph_edge(&graph->g, e, &from, &to);
		float log_weight = lnw[from] + lnx[to];
		float weight;
		if (ctx->weight_mode == CRIT_WEIGHT_SPLC) {
			weight = lnw[from] > 80.0f ? INFINITY : expf(lnw[from]);
		} else if (ctx->weight_mode == CRIT_WEIGHT_UNIT) {
			weight = 1.0f;
		} else if (ctx->weight_mode == CRIT_WEIGHT_SPC) {
			weight = log_weight > 80.0f ? INFINITY : expf(log_weight);
		} else {
			weight = log_weight;
		}
		VECTOR(weights)[e] = weight;
		if (isfinite(weight) && weight > max_weight)
			max_weight = weight;
		total += weight;
	}

	const char *attr_name = ctx->weight_mode == CRIT_WEIGHT_SPLC ? "main-path-weight-splc" : (ctx->weight_mode == CRIT_WEIGHT_UNIT ? "main-path-weight-unit" : (ctx->weight_mode == CRIT_WEIGHT_SPC ? "main-path-weight-spc" : "main-path-weight-spe"));
	graph_cache_store_edge_attr(&graph->g, attr_name, &weights);
	printf("Main path %s readback: %lld edges, max weight: %.2f, total: %.2f\n", ctx->weight_mode == CRIT_WEIGHT_SPLC ? "SPLC" : (ctx->weight_mode == CRIT_WEIGHT_UNIT ? "Unit" : (ctx->weight_mode == CRIT_WEIGHT_SPC ? "SPC" : "SPE")), (long long)m, max_weight, total);
	igraph_vector_destroy(&weights);
	free(lnw);
	free(lnx);
}
