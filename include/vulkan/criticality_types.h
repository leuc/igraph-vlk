/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef VULKAN_CRITICALITY_TYPES_H
#define VULKAN_CRITICALITY_TYPES_H

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

typedef enum { CRIT_STAGE_LNW = 0, CRIT_STAGE_LNX = 1, CRIT_STAGE_MATERIALIZE = 2, CRIT_STAGE_HEIGHT = 3, CRIT_STAGE_DEPTH = 4, CRIT_STAGE_REDUCE = 5, CRIT_STAGE_FLAGS = 6, CRIT_STAGE_PATH_TRACE = 7 } CritStage;

typedef enum { CRIT_WEIGHT_SPLC = 0, CRIT_WEIGHT_UNIT = 1, CRIT_WEIGHT_SPC = 2, CRIT_WEIGHT_SPE = 3 } CritWeightMode;

typedef struct
{
	uint32_t level_offset;
	uint32_t num_nodes_in_level;
	uint32_t stage;
	uint32_t weight_mode;
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

static inline size_t crit_result_path_offset(uint32_t edge_count, uint32_t node_count)
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

#endif
