/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef CRITICALITY_TEST_HARNESS_H
#define CRITICALITY_TEST_HARNESS_H

#include "vulkan/criticality_types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

// Minimal, dependency-light compute-only Vulkan harness for exercising shaders/main_path.comp
// directly. Deliberately independent of src/vulkan/*.c (no window/swapchain/GLFW needed, any
// Vulkan ICD will do including the lavapipe software rasterizer) — see CMakeLists.txt.

#define BINDING_COUNT 14
#define TOLERANCE 1e-4f

typedef struct
{
	uint32_t strength_max_bits;
	uint32_t _reserved[3];
} EdgeAnimHeader;

typedef struct
{
	float reveal_at;
	float strength;
} EdgeAnim;

typedef struct
{
	uint32_t node_count;
	uint32_t edge_count;
	uint32_t num_levels;
	const CritNode *out_nodes;
	const CritEdge *out_edges;
	const CritNode *in_nodes;
	const CritEdge *in_edges;
	const uint32_t *level_nodes;
	const uint32_t *level_offsets;
	const uint32_t *level_sizes;
} GraphSpec;

typedef struct
{
	VkInstance instance;
	VkPhysicalDevice physical;
	VkDevice device;
	VkQueue queue;
	uint32_t queue_family;
	VkDescriptorSetLayout set_layout;
	VkDescriptorPool descriptor_pool;
	VkDescriptorSet descriptor_set;
	VkPipelineLayout pipeline_layout;
	VkPipeline pipeline;
	VkCommandPool command_pool;
	VkCommandBuffer command_buffer;
	VkBuffer buffers[BINDING_COUNT];
	VkDeviceMemory memories[BINDING_COUNT];
	uint32_t tile_word_count;
} Harness;

enum { BUF_OUT_NODES = 0, BUF_OUT_EDGES, BUF_IN_NODES, BUF_IN_EDGES, BUF_LEVELS, BUF_LNW, BUF_LNX, BUF_HEIGHT, BUF_DEPTH, BUF_EDGE_ANIM, BUF_RESULT, BUF_REACHABILITY, BUF_TOTAL_COUNT_FWD, BUF_TOTAL_COUNT_REV };

int harness_init(Harness *h);
void harness_destroy(Harness *h);
// Rebuilds all graph-shaped buffers/descriptors for `graph`, sizing the NPPC reachability tile
// from `nppc_tile_budget_bytes` (irrelevant for non-NPPC runs, but always computed/allocated).
int harness_upload_graph(Harness *h, const GraphSpec *graph, size_t nppc_tile_budget_bytes);
// Resets per-run buffers and records+submits the full shader pipeline for `mode` on `graph`,
// waiting for completion. For CRIT_WEIGHT_NPPC this includes the tiled batch-accumulation sweep
// mirroring renderer_criticality.c's host-side dispatch before the LNW/LNX finalize stages.
int harness_run(Harness *h, const GraphSpec *graph, uint32_t mode);
int download(Harness *h, uint32_t binding, void *data, size_t size);

float bits_float(uint32_t bits);
// A downloaded BUF_RESULT buffer is always a CritResultHeader followed by the packed data words.
uint32_t *result_data(unsigned char *bytes);
int check_float_array(const char *name, const float *got, const float *expected, uint32_t count);
int check_uint_array(const char *name, const uint32_t *got, const uint32_t *expected, uint32_t count);

// A dynamically-sized chain of `node_count` levels (one node per level, 0..node_count-1), where
// node v has `edges_per_step` parallel edges to v+1 (none at the last node).
typedef struct
{
	CritNode *out_nodes, *in_nodes;
	CritEdge *out_edges, *in_edges;
	uint32_t *levels, *offsets, *sizes;
} ChainGraph;

bool build_chain_graph(ChainGraph *cg, uint32_t node_count, uint32_t edges_per_step);
void free_chain_graph(ChainGraph *cg);

#endif
