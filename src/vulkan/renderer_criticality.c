/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "vulkan/renderer_criticality.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "graph/graph_core.h"
#include "vulkan/buffers.h"
#include "vulkan/renderer_anim.h"
#include "vulkan/utils.h"

#define CRIT_WORKGROUP_SIZE 64

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
	uint32_t *cursor = calloc((size_t)n, sizeof(uint32_t));
	if (!edges || !cursor) {
		free(cursor);
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

static bool crit_build_level_permutation(CritComputeContext *ctx, const igraph_vector_int_t *levels, igraph_integer_t n, int num_levels, uint32_t **out_perm)
{
	uint32_t *perm = malloc(sizeof(uint32_t) * (size_t)n);
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
		int level = (int)VECTOR(*levels)[i];
		if (level >= 0 && level < num_levels)
			ctx->level_sizes[level]++;
	}
	uint32_t running = 0;
	for (int level = 0; level < num_levels; level++) {
		ctx->level_offsets[level] = running;
		running += ctx->level_sizes[level];
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
		int level = (int)VECTOR(*levels)[i];
		if (level >= 0 && level < num_levels)
			perm[ctx->level_offsets[level] + cursor[level]++] = (uint32_t)i;
	}
	free(cursor);
	*out_perm = perm;
	return true;
}

static void crit_destroy_buffer(VkDevice device, VkBuffer *buffer, VkDeviceMemory *memory)
{
	if (*buffer != VK_NULL_HANDLE)
		vkDestroyBuffer(device, *buffer, NULL);
	if (*memory != VK_NULL_HANDLE)
		vkFreeMemory(device, *memory, NULL);
	*buffer = VK_NULL_HANDLE;
	*memory = VK_NULL_HANDLE;
}

void renderer_destroy_criticality_buffers(Renderer *r)
{
	CritComputeContext *ctx = &r->crit;
	VkDevice device = r->core.device;
	crit_destroy_buffer(device, &ctx->out_nodes_buffer, &ctx->out_nodes_memory);
	crit_destroy_buffer(device, &ctx->out_edges_buffer, &ctx->out_edges_memory);
	crit_destroy_buffer(device, &ctx->in_nodes_buffer, &ctx->in_nodes_memory);
	crit_destroy_buffer(device, &ctx->in_edges_buffer, &ctx->in_edges_memory);
	crit_destroy_buffer(device, &ctx->level_buffer, &ctx->level_memory);
	crit_destroy_buffer(device, &ctx->lnw_buffer, &ctx->lnw_memory);
	crit_destroy_buffer(device, &ctx->lnx_buffer, &ctx->lnx_memory);
	crit_destroy_buffer(device, &ctx->height_buffer, &ctx->height_memory);
	crit_destroy_buffer(device, &ctx->depth_buffer, &ctx->depth_memory);
	crit_destroy_buffer(device, &ctx->result_buffer, &ctx->result_memory);
	free(ctx->level_offsets);
	free(ctx->level_sizes);
	ctx->level_offsets = NULL;
	ctx->level_sizes = NULL;
	ctx->num_levels = 0;
	ctx->node_count = 0;
	ctx->graph_edge_count = 0;
	ctx->active = false;
	ctx->readback_pending = false;
}

void renderer_cancel_main_path(Renderer *r)
{
	if (!r)
		return;
	r->crit.active = false;
	r->crit.readback_pending = false;
}

static void crit_write_descriptors(Renderer *r)
{
	CritComputeContext *ctx = &r->crit;
	VkDescriptorBufferInfo infos[] = {
		{ctx->out_nodes_buffer, 0, VK_WHOLE_SIZE}, {ctx->out_edges_buffer, 0, VK_WHOLE_SIZE}, {ctx->in_nodes_buffer, 0, VK_WHOLE_SIZE}, {ctx->in_edges_buffer, 0, VK_WHOLE_SIZE}, {ctx->level_buffer, 0, VK_WHOLE_SIZE}, {ctx->lnw_buffer, 0, VK_WHOLE_SIZE}, {ctx->lnx_buffer, 0, VK_WHOLE_SIZE}, {ctx->height_buffer, 0, VK_WHOLE_SIZE}, {ctx->depth_buffer, 0, VK_WHOLE_SIZE}, {r->anim.edge_buffer, 0, VK_WHOLE_SIZE}, {ctx->result_buffer, 0, VK_WHOLE_SIZE},
	};
	VkWriteDescriptorSet writes[11];
	for (uint32_t i = 0; i < 11; i++)
		writes[i] = VK_WRITE_DESC_BUFFER(r->descriptors.crit_set, i, &infos[i], VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
	vkUpdateDescriptorSets(r->core.device, 11, writes, 0, NULL);
}

bool renderer_init_criticality_buffers(Renderer *r, GraphData *graph, const igraph_vector_int_t *levels, int num_levels, uint32_t weight_mode)
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

	CritNode *out_nodes = NULL;
	CritNode *in_nodes = NULL;
	CritEdge *out_edges = NULL;
	CritEdge *in_edges = NULL;
	uint32_t out_edge_count = 0;
	uint32_t in_edge_count = 0;
	uint32_t *permutation = NULL;
	if (!crit_build_csr(&graph->g, n, IGRAPH_OUT, &out_nodes, &out_edges, &out_edge_count) || !crit_build_csr(&graph->g, n, IGRAPH_IN, &in_nodes, &in_edges, &in_edge_count) || !crit_build_level_permutation(ctx, levels, n, num_levels, &permutation)) {
		free(out_nodes);
		free(out_edges);
		free(in_nodes);
		free(in_edges);
		return false;
	}

	VkDeviceSize result_size = crit_result_buffer_size(ctx->graph_edge_count, ctx->node_count);
	float *zeros = calloc((size_t)n, sizeof(float));
	unsigned char *result_bytes = calloc(1, (size_t)result_size);
	if (!zeros || !result_bytes) {
		free(zeros);
		free(result_bytes);
		free(out_nodes);
		free(out_edges);
		free(in_nodes);
		free(in_edges);
		free(permutation);
		renderer_destroy_criticality_buffers(r);
		return false;
	}
	CritResultHeader *header = (CritResultHeader *)result_bytes;
	header->edge_count = ctx->graph_edge_count;
	header->node_count = ctx->node_count;
	header->sink_node = UINT32_MAX;
	uint32_t *data = (uint32_t *)(result_bytes + sizeof(*header));
	for (uint32_t v = 0; v < ctx->node_count; v++)
		data[crit_result_predecessor_offset(ctx->graph_edge_count) + v] = UINT32_MAX;

	VkDevice device = r->core.device;
	VkPhysicalDevice physical = r->core.physicalDevice;
	VkBufferUsageFlags usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	VkDeviceSize node_size = sizeof(CritNode) * (size_t)n;
	VkDeviceSize out_edge_size = sizeof(CritEdge) * (out_edge_count > 0 ? out_edge_count : 1);
	VkDeviceSize in_edge_size = sizeof(CritEdge) * (in_edge_count > 0 ? in_edge_count : 1);
	VkDeviceSize level_size = sizeof(uint32_t) * (size_t)n;
	VkDeviceSize value_size = sizeof(float) * (size_t)n;
	VK_CREATE_HOST_BUFFER(device, physical, node_size, usage, &ctx->out_nodes_buffer, &ctx->out_nodes_memory);
	VK_CREATE_HOST_BUFFER(device, physical, out_edge_size, usage, &ctx->out_edges_buffer, &ctx->out_edges_memory);
	VK_CREATE_HOST_BUFFER(device, physical, node_size, usage, &ctx->in_nodes_buffer, &ctx->in_nodes_memory);
	VK_CREATE_HOST_BUFFER(device, physical, in_edge_size, usage, &ctx->in_edges_buffer, &ctx->in_edges_memory);
	VK_CREATE_HOST_BUFFER(device, physical, level_size, usage, &ctx->level_buffer, &ctx->level_memory);
	VK_CREATE_HOST_BUFFER(device, physical, value_size, usage, &ctx->lnw_buffer, &ctx->lnw_memory);
	VK_CREATE_HOST_BUFFER(device, physical, value_size, usage, &ctx->lnx_buffer, &ctx->lnx_memory);
	VK_CREATE_HOST_BUFFER(device, physical, value_size, usage, &ctx->height_buffer, &ctx->height_memory);
	VK_CREATE_HOST_BUFFER(device, physical, value_size, usage, &ctx->depth_buffer, &ctx->depth_memory);
	VK_CREATE_HOST_BUFFER(device, physical, result_size, usage, &ctx->result_buffer, &ctx->result_memory);
	update_buffer(device, ctx->out_nodes_memory, node_size, out_nodes);
	if (out_edge_count > 0)
		update_buffer(device, ctx->out_edges_memory, sizeof(CritEdge) * out_edge_count, out_edges);
	update_buffer(device, ctx->in_nodes_memory, node_size, in_nodes);
	if (in_edge_count > 0)
		update_buffer(device, ctx->in_edges_memory, sizeof(CritEdge) * in_edge_count, in_edges);
	update_buffer(device, ctx->level_memory, level_size, permutation);
	update_buffer(device, ctx->lnw_memory, value_size, zeros);
	update_buffer(device, ctx->lnx_memory, value_size, zeros);
	update_buffer(device, ctx->height_memory, value_size, zeros);
	update_buffer(device, ctx->depth_memory, value_size, zeros);
	update_buffer(device, ctx->result_memory, result_size, result_bytes);

	free(zeros);
	free(result_bytes);
	free(out_nodes);
	free(out_edges);
	free(in_nodes);
	free(in_edges);
	free(permutation);
	crit_write_descriptors(r);
	return true;
}

static bool crit_start_animation(Renderer *r, const GraphData *graph, const igraph_vector_int_t *levels)
{
	CritComputeContext *ctx = &r->crit;
	RendererAnimNode *nodes = calloc(graph->node_count > 0 ? graph->node_count : 1, sizeof(RendererAnimNode));
	RendererAnimEdge *edges = calloc(graph->edge_count > 0 ? graph->edge_count : 1, sizeof(RendererAnimEdge));
	if (!nodes || !edges) {
		free(nodes);
		free(edges);
		return false;
	}
	for (uint32_t v = 0; v < graph->node_count; v++)
		nodes[v].reveal_at = renderer_anim_reveal_at(VECTOR(*levels)[v], (float)ctx->level_interval);
	for (uint32_t e = 0; e < graph->edge_count; e++)
		edges[e].reveal_at = nodes[graph->edges[e].from].reveal_at;
	RendererAnimClip clip = {
		.nodes = nodes,
		.edges = edges,
		.node_count = graph->node_count,
		.edge_count = graph->edge_count,
		.strength_max = 0.0f,
		.fade = 0.3f,
		.reveal_mask = RENDERER_ANIM_REVEAL_NODES | RENDERER_ANIM_REVEAL_EDGES,
		.owner = RENDERER_ANIM_MAIN_PATH,
	};
	bool played = renderer_anim_play(r, &clip);
	free(nodes);
	free(edges);
	if (played)
		crit_write_descriptors(r);
	return played;
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
	ctx->stage = CRIT_STAGE_LNW;
	ctx->current_level = 0;
	ctx->level_interval = 5.0 / (double)ctx->num_levels;
	if (ctx->level_interval < 0.016)
		ctx->level_interval = 0.016;
	if (!crit_start_animation(r, graph, levels))
		return false;
	ctx->last_level_time = r->anim.data.time - ctx->level_interval;
	ctx->active = true;
	printf("[MainPath] start: method=%u levels=%d tick=%.3fs\n", ctx->weight_mode, ctx->num_levels, ctx->level_interval);
	return true;
}

static void crit_compute_barrier(VkCommandBuffer command_buffer, VkPipelineStageFlags destination_stages)
{
	VkMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_SHADER_READ_BIT};
	vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, destination_stages, 0, 1, &barrier, 0, NULL, 0, NULL);
}

static void crit_dispatch(Renderer *r, VkCommandBuffer command_buffer, uint32_t offset, uint32_t count, uint32_t stage)
{
	if (count == 0)
		return;
	CritComputeContext *ctx = &r->crit;
	CritPushConstants constants = {.level_offset = offset, .num_nodes_in_level = count, .stage = stage, .weight_mode = ctx->weight_mode};
	vkCmdPushConstants(command_buffer, ctx->pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
	vkCmdDispatch(command_buffer, (count + CRIT_WORKGROUP_SIZE - 1) / CRIT_WORKGROUP_SIZE, 1, 1);
}

static void crit_record_postprocess(Renderer *r, VkCommandBuffer command_buffer)
{
	CritComputeContext *ctx = &r->crit;
	crit_dispatch(r, command_buffer, 0, ctx->node_count, CRIT_STAGE_MATERIALIZE);
	crit_compute_barrier(command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT);
	for (int level = 0; level < ctx->num_levels; level++) {
		crit_dispatch(r, command_buffer, ctx->level_offsets[level], ctx->level_sizes[level], CRIT_STAGE_HEIGHT);
		crit_compute_barrier(command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
	}
	for (int level = ctx->num_levels - 1; level >= 0; level--) {
		crit_dispatch(r, command_buffer, ctx->level_offsets[level], ctx->level_sizes[level], CRIT_STAGE_DEPTH);
		crit_compute_barrier(command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
	}
	crit_dispatch(r, command_buffer, 0, ctx->node_count, CRIT_STAGE_REDUCE);
	crit_compute_barrier(command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
	crit_dispatch(r, command_buffer, 0, ctx->node_count, CRIT_STAGE_FLAGS);
	crit_compute_barrier(command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
	crit_dispatch(r, command_buffer, 0, 1, CRIT_STAGE_PATH_TRACE);
	VkMemoryBarrier host_barrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
	vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host_barrier, 0, NULL, 0, NULL);
	ctx->readback_pending = true;
	printf("[MainPath] compute complete; scheduling result readback\n");
}

void renderer_dispatch_main_path_weight_level(Renderer *r, VkCommandBuffer command_buffer)
{
	CritComputeContext *ctx = &r->crit;
	if (!ctx->active || ctx->readback_pending)
		return;
	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, r->pipelines.compute_criticality);
	vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, ctx->pipeline_layout, 0, 1, &r->descriptors.crit_set, 0, NULL);
	int level = ctx->current_level;
	uint32_t count = ctx->level_sizes[level];
	crit_compute_barrier(command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
	crit_dispatch(r, command_buffer, ctx->level_offsets[level], count, ctx->stage);
	crit_compute_barrier(command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT);
	printf("[MainPath] tick: method=%u phase=%s level=%d/%d nodes=%u\n", ctx->weight_mode, ctx->stage == CRIT_STAGE_LNW ? "forward" : "reverse", level, ctx->num_levels - 1, count);

	if (ctx->stage == CRIT_STAGE_LNW) {
		ctx->current_level++;
		if (ctx->current_level == ctx->num_levels) {
			if (ctx->weight_mode == CRIT_WEIGHT_SPC || ctx->weight_mode == CRIT_WEIGHT_SPE) {
				ctx->stage = CRIT_STAGE_LNX;
				ctx->current_level = ctx->num_levels - 1;
			} else {
				crit_record_postprocess(r, command_buffer);
			}
		}
	} else {
		ctx->current_level--;
		if (ctx->current_level < 0)
			crit_record_postprocess(r, command_buffer);
	}
	ctx->last_level_time = r->anim.data.time;
}

static void *crit_copy_memory(Renderer *r, VkDeviceMemory memory, VkDeviceSize size)
{
	void *mapped = NULL;
	if (vkMapMemory(r->core.device, memory, 0, size, 0, &mapped) != VK_SUCCESS)
		return NULL;
	void *copy = malloc((size_t)size);
	if (copy)
		memcpy(copy, mapped, (size_t)size);
	vkUnmapMemory(r->core.device, memory);
	return copy;
}

static const char *crit_method_name(uint32_t weight_mode)
{
	return weight_mode == CRIT_WEIGHT_SPLC ? "splc" : (weight_mode == CRIT_WEIGHT_UNIT ? "unit" : (weight_mode == CRIT_WEIGHT_SPC ? "spc" : "spe"));
}

void renderer_readback_main_path_result(Renderer *r, GraphData *graph)
{
	CritComputeContext *ctx = &r->crit;
	if (!graph || graph->node_count != ctx->node_count || graph->edge_count != ctx->graph_edge_count)
		return;
	VkDeviceSize result_size = crit_result_buffer_size(ctx->graph_edge_count, ctx->node_count);
	unsigned char *result_bytes = crit_copy_memory(r, ctx->result_memory, result_size);
	VkDeviceSize animation_size = sizeof(RendererAnimEdgeHeader) + sizeof(RendererAnimEdge) * ctx->graph_edge_count;
	unsigned char *animation_bytes = crit_copy_memory(r, r->anim.edge_memory, animation_size);
	if (!result_bytes || !animation_bytes) {
		free(result_bytes);
		free(animation_bytes);
		return;
	}
	CritResultHeader *header = (CritResultHeader *)result_bytes;
	if (header->node_count != ctx->node_count || header->edge_count != ctx->graph_edge_count) {
		free(result_bytes);
		free(animation_bytes);
		return;
	}
	uint32_t *data = (uint32_t *)(result_bytes + sizeof(*header));
	RendererAnimEdge *animation_edges = (RendererAnimEdge *)(animation_bytes + sizeof(RendererAnimEdgeHeader));
	igraph_vector_t weights;
	igraph_vector_t strengths;
	igraph_vector_int_t basket;
	igraph_vector_int_t path;
	bool weights_ok = igraph_vector_init(&weights, ctx->graph_edge_count) == IGRAPH_SUCCESS;
	bool strengths_ok = igraph_vector_init(&strengths, ctx->graph_edge_count) == IGRAPH_SUCCESS;
	bool basket_ok = igraph_vector_int_init(&basket, ctx->node_count) == IGRAPH_SUCCESS;
	bool path_ok = igraph_vector_int_init(&path, ctx->node_count) == IGRAPH_SUCCESS;
	if (!weights_ok || !strengths_ok || !basket_ok || !path_ok) {
		if (weights_ok)
			igraph_vector_destroy(&weights);
		if (strengths_ok)
			igraph_vector_destroy(&strengths);
		if (basket_ok)
			igraph_vector_int_destroy(&basket);
		if (path_ok)
			igraph_vector_int_destroy(&path);
		free(result_bytes);
		free(animation_bytes);
		return;
	}
	for (uint32_t e = 0; e < ctx->graph_edge_count; e++) {
		uint32_t bits = data[crit_result_weight_offset() + e];
		float weight;
		memcpy(&weight, &bits, sizeof(weight));
		VECTOR(weights)[e] = weight;
		VECTOR(strengths)[e] = animation_edges[e].strength;
	}
	for (uint32_t v = 0; v < ctx->node_count; v++) {
		VECTOR(basket)[v] = data[crit_result_basket_offset(ctx->graph_edge_count, ctx->node_count) + v];
		VECTOR(path)[v] = data[crit_result_path_offset(ctx->graph_edge_count, ctx->node_count) + v];
	}
	char weight_attr[64];
	char strength_attr[64];
	char basket_attr[64];
	char path_attr[64];
	const char *method = crit_method_name(ctx->weight_mode);
	snprintf(weight_attr, sizeof(weight_attr), "main-path-weight-%s", method);
	snprintf(strength_attr, sizeof(strength_attr), "main-path-strength-%s", method);
	snprintf(basket_attr, sizeof(basket_attr), "main-path-basket-%s", method);
	snprintf(path_attr, sizeof(path_attr), "main-path-path-%s", method);
	if ((header->status & CRIT_RESULT_INVALID) != 0) {
		fprintf(stderr, "[Main Path] %s result was invalid; cache not published\n", method);
	} else {
		graph_cache_store_edge_attr(&graph->g, weight_attr, &weights);
		graph_cache_store_edge_attr(&graph->g, strength_attr, &strengths);
		if ((header->status & CRIT_RESULT_OVERFLOW) == 0) {
			graph_cache_store_vertex_attr_int(&graph->g, basket_attr, &basket);
			graph_cache_store_vertex_attr_int(&graph->g, path_attr, &path);
		} else {
			fprintf(stderr, "[Main Path] %s overflowed; selection unavailable, use SPE\n", method);
		}
	}
	igraph_vector_destroy(&weights);
	igraph_vector_destroy(&strengths);
	igraph_vector_int_destroy(&basket);
	igraph_vector_int_destroy(&path);
	free(result_bytes);
	free(animation_bytes);
}
