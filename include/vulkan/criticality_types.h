/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef VULKAN_CRITICALITY_TYPES_H
#define VULKAN_CRITICALITY_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct
{
	uint32_t edge_offset;
	uint32_t degree;
} CritNode;

typedef struct
{
	uint32_t node;
	uint32_t pad;
} CritEdge;

typedef enum { CRIT_STAGE_LNW = 0, CRIT_STAGE_LNX = 1, CRIT_STAGE_MATERIALIZE = 2, CRIT_STAGE_HEIGHT = 3, CRIT_STAGE_DEPTH = 4, CRIT_STAGE_REDUCE = 5, CRIT_STAGE_FLAGS = 6, CRIT_STAGE_GLOBAL_TRACE = 7, CRIT_STAGE_NPPC_ACCUMULATE_FWD = 8, CRIT_STAGE_NPPC_ACCUMULATE_REV = 9 } CritStage;

typedef enum { CRIT_WEIGHT_SPLC = 0, CRIT_WEIGHT_UNIT = 1, CRIT_WEIGHT_SPC = 2, CRIT_WEIGHT_SPE = 3, CRIT_WEIGHT_NPPC = 4 } CritWeightMode;

typedef struct
{
	uint32_t level_offset;
	uint32_t num_nodes_in_level;
	uint32_t stage;
	uint32_t weight_mode;
	uint32_t tile_word_offset;
	uint32_t tile_word_count;
} CritPushConstants;

enum {
	CRIT_RESULT_OVERFLOW = 1u << 0,
	CRIT_RESULT_INVALID = 1u << 1,
};

typedef struct
{
	uint32_t edge_count;
	uint32_t node_count;
	uint32_t status;
	uint32_t criticality_max_bits;
	uint32_t sink_height_bits;
	uint32_t sink_node;
	uint32_t _reserved[2];
} CritResultHeader;

_Static_assert(sizeof(CritResultHeader) == 32, "CritResultHeader must match the std430 block in main_path.comp");

static inline size_t crit_result_weight_offset(void)
{
	return 0;
}

static inline size_t crit_result_predecessor_offset(uint32_t edge_count)
{
	return edge_count;
}

static inline size_t crit_result_basket_offset(uint32_t edge_count, uint32_t node_count)
{
	return edge_count + node_count;
}

static inline size_t crit_result_global_offset(uint32_t edge_count, uint32_t node_count)
{
	return edge_count + 2u * node_count;
}

static inline size_t crit_result_data_word_count(uint32_t edge_count, uint32_t node_count)
{
	size_t count = edge_count + 3u * node_count;
	return count > 0 ? count : 1;
}

static inline size_t crit_result_buffer_size(uint32_t edge_count, uint32_t node_count)
{
	return sizeof(CritResultHeader) + sizeof(uint32_t) * crit_result_data_word_count(edge_count, node_count);
}

static inline size_t crit_reachability_word_count(uint32_t node_count)
{
	return ((size_t)node_count + 31u) / 32u;
}

// Preferred scratch budget for the NPPC reachability tile buffer; the actual tile width is also
// clamped to the device's maxStorageBufferRange, whichever is smaller.
#define CRIT_NPPC_TILE_BUDGET_BYTES ((size_t)256 * 1024 * 1024)

// Largest word-tile width that keeps a node_count-row scratch buffer within budget, floored at 1
// word/node (4*node_count bytes) and capped at the full matrix width (today's whole-buffer case).
static inline uint32_t crit_reachability_tile_word_count(uint32_t node_count, size_t budget_bytes, size_t max_storage_buffer_range)
{
	size_t total_words = crit_reachability_word_count(node_count);
	if (total_words == 0 || node_count == 0)
		return 0;
	size_t budget = budget_bytes < max_storage_buffer_range ? budget_bytes : max_storage_buffer_range;
	size_t max_words = budget / (sizeof(uint32_t) * (size_t)node_count);
	if (max_words == 0)
		max_words = 1;
	return (uint32_t)(max_words < total_words ? max_words : total_words);
}

static inline size_t crit_reachability_tile_buffer_size(uint32_t node_count, uint32_t tile_word_count)
{
	return sizeof(uint32_t) * (size_t)node_count * (size_t)tile_word_count;
}

static inline size_t crit_total_count_buffer_size(uint32_t node_count)
{
	return sizeof(uint32_t) * (size_t)(node_count > 0 ? node_count : 1);
}

#endif
