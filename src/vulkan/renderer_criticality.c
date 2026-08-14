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

static void crit_start_nppc_batch(Renderer *r);

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

// Scale the NPPC reachability tile budget to the actual hardware instead of a fixed constant:
// use a conservative fraction of the largest device-local heap (leaving headroom for every other
// buffer/texture/framebuffer the renderer holds), clamped later against maxStorageBufferRange by
// crit_reachability_tile_word_count. Falls back to CRIT_NPPC_TILE_BUDGET_BYTES if heap detection
// comes back empty (e.g. an unusual driver reporting no DEVICE_LOCAL heap).
#define CRIT_NPPC_TILE_HEAP_FRACTION_DIVISOR 4

static size_t crit_nppc_tile_budget_bytes(VkPhysicalDevice physical)
{
	VkPhysicalDeviceMemoryProperties memory_properties;
	vkGetPhysicalDeviceMemoryProperties(physical, &memory_properties);
	VkDeviceSize largest_device_local_heap = 0;
	for (uint32_t i = 0; i < memory_properties.memoryHeapCount; i++)
		if ((memory_properties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0 && memory_properties.memoryHeaps[i].size > largest_device_local_heap)
			largest_device_local_heap = memory_properties.memoryHeaps[i].size;
	if (largest_device_local_heap == 0)
		return CRIT_NPPC_TILE_BUDGET_BYTES;
	size_t heap_budget = (size_t)(largest_device_local_heap / CRIT_NPPC_TILE_HEAP_FRACTION_DIVISOR);
	return heap_budget > CRIT_NPPC_TILE_BUDGET_BYTES ? heap_budget : CRIT_NPPC_TILE_BUDGET_BYTES;
}

// A tile's per-word work is O(edge_count) (one OR per edge per word, both sweep directions), so a
// wide tile on a large graph can take long enough to execute that it visibly delays that frame's
// presentation on the same queue, even though nothing blocks the CPU. Vulkan has no portable query
// for compute throughput, so this is a static, hardware-informed (not measured) estimate: it caps
// tile width so total per-tile word-work stays under a target GPU-time budget, using device type as
// a coarse throughput proxy (discrete GPUs get a much larger allowance than integrated/software
// devices). These throughput constants are deliberately conservative guesses, not benchmarked
// figures — overall run time isn't a priority here (the reveal animation dominates it regardless),
// only keeping any single tile from monopolizing the graphics queue long enough to be felt.
#define CRIT_NPPC_TILE_TARGET_SECONDS 0.2
#define CRIT_NPPC_DISCRETE_WORD_OPS_PER_SECOND ((double)5e9)
#define CRIT_NPPC_INTEGRATED_WORD_OPS_PER_SECOND ((double)2e8)

static uint32_t crit_nppc_tile_word_count_by_work(uint32_t edge_count, VkPhysicalDeviceType device_type)
{
	double ops_per_second = device_type == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? CRIT_NPPC_DISCRETE_WORD_OPS_PER_SECOND : CRIT_NPPC_INTEGRATED_WORD_OPS_PER_SECOND;
	double word_budget = ops_per_second * CRIT_NPPC_TILE_TARGET_SECONDS / (double)(edge_count > 0 ? edge_count : 1);
	if (word_budget < 1.0)
		word_budget = 1.0;
	return word_budget > (double)UINT32_MAX ? UINT32_MAX : (uint32_t)word_budget;
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
	// A tile submitted by renderer_tick_nppc_batch may still be executing on the GPU; wait for it
	// (at most one tile's worth of work, not the whole batch) before freeing buffers it references.
	if (ctx->nppc_batch_pending && ctx->nppc_batch_fence != VK_NULL_HANDLE)
		vkWaitForFences(device, 1, &ctx->nppc_batch_fence, VK_TRUE, UINT64_MAX);
	if (ctx->nppc_batch_fence != VK_NULL_HANDLE)
		vkDestroyFence(device, ctx->nppc_batch_fence, NULL);
	if (ctx->nppc_batch_command_buffer != VK_NULL_HANDLE)
		vkFreeCommandBuffers(device, r->commands.commandPool, 1, &ctx->nppc_batch_command_buffer);
	ctx->nppc_batch_fence = VK_NULL_HANDLE;
	ctx->nppc_batch_command_buffer = VK_NULL_HANDLE;
	ctx->nppc_batch_pending = false;
	ctx->nppc_batch_tile = 0;
	ctx->nppc_batch_tile_count = 0;
	crit_destroy_buffer(device, &ctx->out_nodes_buffer, &ctx->out_nodes_memory);
	crit_destroy_buffer(device, &ctx->out_edges_buffer, &ctx->out_edges_memory);
	crit_destroy_buffer(device, &ctx->in_nodes_buffer, &ctx->in_nodes_memory);
	crit_destroy_buffer(device, &ctx->in_edges_buffer, &ctx->in_edges_memory);
	crit_destroy_buffer(device, &ctx->level_buffer, &ctx->level_memory);
	crit_destroy_buffer(device, &ctx->lnw_buffer, &ctx->lnw_memory);
	crit_destroy_buffer(device, &ctx->lnx_buffer, &ctx->lnx_memory);
	crit_destroy_buffer(device, &ctx->height_buffer, &ctx->height_memory);
	crit_destroy_buffer(device, &ctx->depth_buffer, &ctx->depth_memory);
	crit_destroy_buffer(device, &ctx->reachability_buffer, &ctx->reachability_memory);
	crit_destroy_buffer(device, &ctx->total_count_fwd_buffer, &ctx->total_count_fwd_memory);
	crit_destroy_buffer(device, &ctx->total_count_rev_buffer, &ctx->total_count_rev_memory);
	crit_destroy_buffer(device, &ctx->result_buffer, &ctx->result_memory);
	free(ctx->level_offsets);
	free(ctx->level_sizes);
	ctx->level_offsets = NULL;
	ctx->level_sizes = NULL;
	ctx->num_levels = 0;
	ctx->node_count = 0;
	ctx->graph_edge_count = 0;
	ctx->reachability_tile_word_count = 0;
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
		{ctx->out_nodes_buffer, 0, VK_WHOLE_SIZE}, {ctx->out_edges_buffer, 0, VK_WHOLE_SIZE}, {ctx->in_nodes_buffer, 0, VK_WHOLE_SIZE}, {ctx->in_edges_buffer, 0, VK_WHOLE_SIZE}, {ctx->level_buffer, 0, VK_WHOLE_SIZE}, {ctx->lnw_buffer, 0, VK_WHOLE_SIZE}, {ctx->lnx_buffer, 0, VK_WHOLE_SIZE}, {ctx->height_buffer, 0, VK_WHOLE_SIZE}, {ctx->depth_buffer, 0, VK_WHOLE_SIZE}, {r->anim.edge_buffer, 0, VK_WHOLE_SIZE}, {ctx->result_buffer, 0, VK_WHOLE_SIZE}, {ctx->reachability_buffer, 0, VK_WHOLE_SIZE}, {ctx->total_count_fwd_buffer, 0, VK_WHOLE_SIZE}, {ctx->total_count_rev_buffer, 0, VK_WHOLE_SIZE},
	};
	VkWriteDescriptorSet writes[14];
	for (uint32_t i = 0; i < 14; i++)
		writes[i] = VK_WRITE_DESC_BUFFER(r->descriptors.crit_set, i, &infos[i], VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
	vkUpdateDescriptorSets(r->core.device, 14, writes, 0, NULL);
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
	VkDeviceSize max_storage_buffer_range = r->core.deviceProperties.limits.maxStorageBufferRange;
	ctx->reachability_tile_word_count = weight_mode == CRIT_WEIGHT_NPPC ? crit_reachability_tile_word_count((uint32_t)n, crit_nppc_tile_budget_bytes(physical), max_storage_buffer_range) : 0;
	VkDeviceSize reachability_size = weight_mode == CRIT_WEIGHT_NPPC ? crit_reachability_tile_buffer_size((uint32_t)n, ctx->reachability_tile_word_count) : sizeof(uint32_t);
	VkDeviceSize total_count_size = weight_mode == CRIT_WEIGHT_NPPC ? crit_total_count_buffer_size((uint32_t)n) : sizeof(uint32_t);
	if (weight_mode == CRIT_WEIGHT_NPPC && (reachability_size > max_storage_buffer_range || total_count_size > max_storage_buffer_range)) {
		fprintf(stderr, "[Main Path] NPPC requires at least %llu bytes even at the smallest reachability tile width (node_count=%u), exceeding the device limit of %llu bytes; tiling alone cannot make this graph fit on this device\n", (unsigned long long)reachability_size, ctx->node_count, (unsigned long long)max_storage_buffer_range);
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
	// Feasibility was already established against the memory-derived width above; shrinking it
	// further for GPU-time reasons can only make it fit more easily, never break feasibility.
	if (weight_mode == CRIT_WEIGHT_NPPC) {
		uint32_t work_tile_word_count = crit_nppc_tile_word_count_by_work(ctx->graph_edge_count, r->core.deviceProperties.deviceType);
		if (work_tile_word_count < ctx->reachability_tile_word_count)
			ctx->reachability_tile_word_count = work_tile_word_count;
		reachability_size = crit_reachability_tile_buffer_size((uint32_t)n, ctx->reachability_tile_word_count);
	}
	VkResult reachability_result = try_create_buffer(device, physical, reachability_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &ctx->reachability_buffer, &ctx->reachability_memory);
	VkBufferUsageFlags total_count_usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	VkResult total_count_fwd_result = reachability_result == VK_SUCCESS ? try_create_buffer(device, physical, total_count_size, total_count_usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &ctx->total_count_fwd_buffer, &ctx->total_count_fwd_memory) : reachability_result;
	VkResult total_count_rev_result = total_count_fwd_result == VK_SUCCESS ? try_create_buffer(device, physical, total_count_size, total_count_usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &ctx->total_count_rev_buffer, &ctx->total_count_rev_memory) : total_count_fwd_result;
	if (total_count_rev_result != VK_SUCCESS) {
		fprintf(stderr, "[Main Path] unable to allocate reachability/total-count storage (VkResult %d)\n", total_count_rev_result);
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
	if (weight_mode == CRIT_WEIGHT_NPPC) {
		size_t total_words = crit_reachability_word_count(ctx->node_count);
		uint32_t tile_count = (uint32_t)((total_words + ctx->reachability_tile_word_count - 1) / ctx->reachability_tile_word_count);
		printf("[MainPath] NPPC reachability storage: %llu bytes/tile (%.2f MiB), nodes=%u, words-per-node=%zu, tile-words=%u, tiles=%u\n", (unsigned long long)reachability_size, (double)reachability_size / (1024.0 * 1024.0), ctx->node_count, total_words, ctx->reachability_tile_word_count, tile_count);
	}
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

bool renderer_start_main_path_weighting(Renderer *r, const GraphData *graph, const igraph_vector_int_t *levels, float duration)
{
	if (!r || !graph || !levels)
		return false;
	CritComputeContext *ctx = &r->crit;
	if (ctx->num_levels <= 0 || r->anim.edge_buffer == VK_NULL_HANDLE || !isfinite(duration) || duration <= 0.0f)
		return false;
	ctx->active = false;
	ctx->readback_pending = false;
	ctx->stage = CRIT_STAGE_LNW;
	ctx->current_level = 0;
	ctx->level_interval = (double)duration / (double)ctx->num_levels;
	if (ctx->level_interval < 0.016)
		ctx->level_interval = 0.016;
	if (!crit_start_animation(r, graph, levels))
		return false;
	// Only start submitting NPPC batch tiles once every descriptor write for this run — including
	// crit_start_animation's rewrite of the shared edge-animation binding — has settled, so no
	// vkUpdateDescriptorSets ever races a tile's command buffer while it's still pending on the GPU.
	if (ctx->weight_mode == CRIT_WEIGHT_NPPC)
		crit_start_nppc_batch(r);
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

static void crit_dispatch_tile(Renderer *r, VkCommandBuffer command_buffer, uint32_t offset, uint32_t count, uint32_t stage, uint32_t tile_word_offset, uint32_t tile_word_count)
{
	if (count == 0)
		return;
	CritComputeContext *ctx = &r->crit;
	CritPushConstants constants = {.level_offset = offset, .num_nodes_in_level = count, .stage = stage, .weight_mode = ctx->weight_mode, .tile_word_offset = tile_word_offset, .tile_word_count = tile_word_count};
	vkCmdPushConstants(command_buffer, ctx->pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
	vkCmdDispatch(command_buffer, (count + CRIT_WORKGROUP_SIZE - 1) / CRIT_WORKGROUP_SIZE, 1, 1);
}

static void crit_dispatch(Renderer *r, VkCommandBuffer command_buffer, uint32_t offset, uint32_t count, uint32_t stage)
{
	crit_dispatch_tile(r, command_buffer, offset, count, stage, 0, 0);
}

// Exact NPPC reachability accumulation, ticked one tile per frame instead of run synchronously to
// completion (see renderer_tick_nppc_batch). Chunk-outer/level-inner: each word-tile gets a
// complete forward then reverse topological sweep, accumulating exact popcounts into
// total_count_fwd/total_count_rev, before the tile scratch buffer is reused for the next tile.
// Each tile is its own submission (fenced, polled non-blockingly) so no single uninterrupted
// submission covers more than one tile's worth of GPU work.
static void crit_record_nppc_tile(Renderer *r, VkCommandBuffer command_buffer, uint32_t tile_offset, uint32_t tile_count, bool zero_accumulators_first)
{
	CritComputeContext *ctx = &r->crit;
	if (zero_accumulators_first) {
		vkCmdFillBuffer(command_buffer, ctx->total_count_fwd_buffer, 0, VK_WHOLE_SIZE, 0u);
		vkCmdFillBuffer(command_buffer, ctx->total_count_rev_buffer, 0, VK_WHOLE_SIZE, 0u);
		VkMemoryBarrier zero_barrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
		vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &zero_barrier, 0, NULL, 0, NULL);
	}
	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, r->pipelines.compute_criticality);
	vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, ctx->pipeline_layout, 0, 1, &r->descriptors.crit_set, 0, NULL);
	for (int level = 0; level < ctx->num_levels; level++) {
		crit_dispatch_tile(r, command_buffer, ctx->level_offsets[level], ctx->level_sizes[level], CRIT_STAGE_NPPC_ACCUMULATE_FWD, tile_offset, tile_count);
		crit_compute_barrier(command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
	}
	for (int level = ctx->num_levels - 1; level >= 0; level--) {
		crit_dispatch_tile(r, command_buffer, ctx->level_offsets[level], ctx->level_sizes[level], CRIT_STAGE_NPPC_ACCUMULATE_REV, tile_offset, tile_count);
		crit_compute_barrier(command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
	}
}

static void crit_submit_nppc_batch_tile(Renderer *r)
{
	CritComputeContext *ctx = &r->crit;
	uint32_t total_words = (uint32_t)crit_reachability_word_count(ctx->node_count);
	uint32_t tile_words = ctx->reachability_tile_word_count;
	uint32_t offset = ctx->nppc_batch_tile * tile_words;
	uint32_t count = offset + tile_words <= total_words ? tile_words : total_words - offset;

	VK_CHECK(vkResetCommandBuffer(ctx->nppc_batch_command_buffer, 0), "Failed to reset NPPC batch command buffer");
	VK_CHECK(vkBeginCommandBuffer(ctx->nppc_batch_command_buffer, &VK_CMD_BEGIN_INFO_ONETIME), "Failed to begin NPPC batch command buffer");
	crit_record_nppc_tile(r, ctx->nppc_batch_command_buffer, offset, count, ctx->nppc_batch_tile == 0);
	VK_CHECK(vkEndCommandBuffer(ctx->nppc_batch_command_buffer), "Failed to end NPPC batch command buffer");

	VK_CHECK(vkResetFences(r->core.device, 1, &ctx->nppc_batch_fence), "Failed to reset NPPC batch fence");
	VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &ctx->nppc_batch_command_buffer};
	VK_CHECK(vkQueueSubmit(r->core.graphicsQueue, 1, &submit, ctx->nppc_batch_fence), "Failed to submit NPPC batch tile");
	printf("[MainPath] NPPC batch: tile %u/%u submitted (words %u..%u)\n", ctx->nppc_batch_tile + 1, ctx->nppc_batch_tile_count, offset, offset + count);
}

// Kicks off tile 0; renderer_tick_nppc_batch drives the remaining tiles from the frame loop.
static void crit_start_nppc_batch(Renderer *r)
{
	CritComputeContext *ctx = &r->crit;
	uint32_t total_words = (uint32_t)crit_reachability_word_count(ctx->node_count);
	uint32_t tile_words = ctx->reachability_tile_word_count;
	if (total_words == 0 || tile_words == 0)
		return;

	VkCommandBufferAllocateInfo alloc_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandPool = r->commands.commandPool, .commandBufferCount = 1};
	VK_CHECK(vkAllocateCommandBuffers(r->core.device, &alloc_info, &ctx->nppc_batch_command_buffer), "Failed to allocate NPPC batch command buffer");
	VkFenceCreateInfo fence_info = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
	VK_CHECK(vkCreateFence(r->core.device, &fence_info, NULL, &ctx->nppc_batch_fence), "Failed to create NPPC batch fence");

	ctx->nppc_batch_tile_count = (total_words + tile_words - 1) / tile_words;
	ctx->nppc_batch_tile = 0;
	ctx->nppc_batch_pending = true;
	crit_submit_nppc_batch_tile(r);
}

void renderer_tick_nppc_batch(Renderer *r)
{
	CritComputeContext *ctx = &r->crit;
	if (!ctx->nppc_batch_pending)
		return;
	VkResult status = vkGetFenceStatus(r->core.device, ctx->nppc_batch_fence);
	if (status == VK_NOT_READY)
		return; // tile still running on the GPU; render this frame normally and re-check next frame
	if (status != VK_SUCCESS) {
		fprintf(stderr, "[Main Path] NPPC batch tile fence error (VkResult %d)\n", status);
		ctx->nppc_batch_pending = false;
		return;
	}
	ctx->nppc_batch_tile++;
	if (ctx->nppc_batch_tile >= ctx->nppc_batch_tile_count) {
		ctx->nppc_batch_pending = false;
		ctx->last_level_time = r->anim.data.time - ctx->level_interval;
		printf("[MainPath] NPPC batch: all %u tiles complete\n", ctx->nppc_batch_tile_count);
		return;
	}
	crit_submit_nppc_batch_tile(r);
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
	crit_dispatch(r, command_buffer, 0, 1, CRIT_STAGE_GLOBAL_TRACE);
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
			if (ctx->weight_mode == CRIT_WEIGHT_SPC || ctx->weight_mode == CRIT_WEIGHT_SPE || ctx->weight_mode == CRIT_WEIGHT_NPPC || ctx->weight_mode == CRIT_WEIGHT_SPNP) {
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
	switch (weight_mode) {
	case CRIT_WEIGHT_SPLC:
		return "splc";
	case CRIT_WEIGHT_UNIT:
		return "unit";
	case CRIT_WEIGHT_SPC:
		return "spc";
	case CRIT_WEIGHT_SPE:
		return "spe";
	case CRIT_WEIGHT_NPPC:
		return "nppc";
	case CRIT_WEIGHT_SPNP:
		return "spnp";
	default:
		return "unknown";
	}
}

static float crit_float_from_bits(uint32_t bits)
{
	float value;
	memcpy(&value, &bits, sizeof(value));
	return value;
}

static void crit_print_readback(const char *method, const CritResultHeader *header, const uint32_t *data, const float *lnw, const float *lnx, const float *height, const float *depth)
{
	uint32_t basket_count = 0;
	uint32_t global_count = 0;
	uint32_t global_outside_basket = 0;
	size_t predecessor_offset = crit_result_predecessor_offset(header->edge_count);
	size_t basket_offset = crit_result_basket_offset(header->edge_count, header->node_count);
	size_t global_offset = crit_result_global_offset(header->edge_count, header->node_count);
	for (uint32_t v = 0; v < header->node_count; v++) {
		bool in_basket = data[basket_offset + v] != 0;
		bool in_global = data[global_offset + v] != 0;
		basket_count += in_basket;
		global_count += in_global;
		global_outside_basket += in_global && !in_basket;
	}
	printf("[MainPath] readback: method=%s status=0x%x maximum=%.9g sink=%u sink_height=%.9g basket=%u global=%u global_outside_basket=%u\n", method, header->status, crit_float_from_bits(header->criticality_max_bits), header->sink_node, crit_float_from_bits(header->sink_height_bits), basket_count, global_count, global_outside_basket);
	printf("[MainPath] basket nodes:");
	uint32_t printed = 0;
	for (uint32_t v = 0; v < header->node_count; v++)
		if (data[basket_offset + v] != 0 && printed++ < 128)
			printf(" %u", v);
	if (basket_count > 128)
		printf(" ... (%u more)", basket_count - 128);
	printf("\n[MainPath] global path nodes:");
	printed = 0;
	for (uint32_t v = 0; v < header->node_count; v++)
		if (data[global_offset + v] != 0 && printed++ < 128)
			printf(" %u", v);
	if (global_count > 128)
		printf(" ... (%u more)", global_count - 128);
	printf("\n");
	uint32_t detail_count = 0;
	for (uint32_t v = 0; v < header->node_count && detail_count < 128; v++) {
		if (data[basket_offset + v] == 0 && data[global_offset + v] == 0)
			continue;
		uint32_t predecessor = data[predecessor_offset + v];
		float slack = crit_float_from_bits(header->criticality_max_bits) - height[v] - depth[v];
		if (predecessor == UINT32_MAX)
			printf("[MainPath] node=%u lnW=%.9g lnX=%.9g height=%.9g depth=%.9g slack=%.9g predecessor=none basket=%u global=%u\n", v, lnw[v], lnx[v], height[v], depth[v], slack, data[basket_offset + v], data[global_offset + v]);
		else
			printf("[MainPath] node=%u lnW=%.9g lnX=%.9g height=%.9g depth=%.9g slack=%.9g predecessor=%u basket=%u global=%u\n", v, lnw[v], lnx[v], height[v], depth[v], slack, predecessor, data[basket_offset + v], data[global_offset + v]);
		detail_count++;
	}
	if (basket_count + global_outside_basket > detail_count)
		printf("[MainPath] selected-node detail truncated: showing %u entries\n", detail_count);
	if (global_outside_basket != 0)
		fprintf(stderr, "[Main Path] %s invariant failure: Global Path is not a subset of Basket\n", method);
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
	VkDeviceSize node_values_size = sizeof(float) * ctx->node_count;
	float *lnw = crit_copy_memory(r, ctx->lnw_memory, node_values_size);
	float *lnx = crit_copy_memory(r, ctx->lnx_memory, node_values_size);
	float *height = crit_copy_memory(r, ctx->height_memory, node_values_size);
	float *depth = crit_copy_memory(r, ctx->depth_memory, node_values_size);
	if (!result_bytes || !animation_bytes) {
		free(result_bytes);
		free(animation_bytes);
		free(lnw);
		free(lnx);
		free(height);
		free(depth);
		return;
	}
	CritResultHeader *header = (CritResultHeader *)result_bytes;
	if (header->node_count != ctx->node_count || header->edge_count != ctx->graph_edge_count) {
		free(result_bytes);
		free(animation_bytes);
		free(lnw);
		free(lnx);
		free(height);
		free(depth);
		return;
	}
	uint32_t *data = (uint32_t *)(result_bytes + sizeof(*header));
	RendererAnimEdge *animation_edges = (RendererAnimEdge *)(animation_bytes + sizeof(RendererAnimEdgeHeader));
	if (lnw && lnx && height && depth)
		crit_print_readback(crit_method_name(ctx->weight_mode), header, data, lnw, lnx, height, depth);
	else
		fprintf(stderr, "[Main Path] numerical readback diagnostics unavailable\n");
	igraph_vector_t weights;
	igraph_vector_t strengths;
	igraph_vector_int_t basket;
	igraph_vector_int_t global;
	bool weights_ok = igraph_vector_init(&weights, ctx->graph_edge_count) == IGRAPH_SUCCESS;
	bool strengths_ok = igraph_vector_init(&strengths, ctx->graph_edge_count) == IGRAPH_SUCCESS;
	bool basket_ok = igraph_vector_int_init(&basket, ctx->node_count) == IGRAPH_SUCCESS;
	bool global_ok = igraph_vector_int_init(&global, ctx->node_count) == IGRAPH_SUCCESS;
	if (!weights_ok || !strengths_ok || !basket_ok || !global_ok) {
		if (weights_ok)
			igraph_vector_destroy(&weights);
		if (strengths_ok)
			igraph_vector_destroy(&strengths);
		if (basket_ok)
			igraph_vector_int_destroy(&basket);
		if (global_ok)
			igraph_vector_int_destroy(&global);
		free(result_bytes);
		free(animation_bytes);
		free(lnw);
		free(lnx);
		free(height);
		free(depth);
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
		VECTOR(global)[v] = data[crit_result_global_offset(ctx->graph_edge_count, ctx->node_count) + v];
	}
	char weight_attr[64];
	char strength_attr[64];
	char basket_attr[64];
	char global_attr[64];
	const char *method = crit_method_name(ctx->weight_mode);
	snprintf(weight_attr, sizeof(weight_attr), "main-path-weight-%s", method);
	snprintf(strength_attr, sizeof(strength_attr), "main-path-strength-%s", method);
	snprintf(basket_attr, sizeof(basket_attr), "main-path-basket-%s", method);
	snprintf(global_attr, sizeof(global_attr), "main-path-global-%s", method);
	if ((header->status & CRIT_RESULT_INVALID) != 0) {
		fprintf(stderr, "[Main Path] %s result was invalid; cache not published\n", method);
	} else {
		graph_cache_store_edge_attr(&graph->g, weight_attr, &weights);
		graph_cache_store_edge_attr(&graph->g, strength_attr, &strengths);
		if ((header->status & CRIT_RESULT_OVERFLOW) == 0) {
			graph_cache_store_vertex_attr_int(&graph->g, basket_attr, &basket);
			graph_cache_store_vertex_attr_int(&graph->g, global_attr, &global);
		} else {
			fprintf(stderr, "[Main Path] %s overflowed; selection unavailable, use SPE\n", method);
		}
	}
	igraph_vector_destroy(&weights);
	igraph_vector_destroy(&strengths);
	igraph_vector_int_destroy(&basket);
	igraph_vector_int_destroy(&global);
	free(result_bytes);
	free(animation_bytes);
	free(lnw);
	free(lnx);
	free(height);
	free(depth);
}
