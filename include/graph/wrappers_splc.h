/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef WRAPPERS_SPLC_H
#define WRAPPERS_SPLC_H

#include "interaction/state.h"
#include <igraph/igraph.h>

/**
 * Calculate levels for each node in a DAG via topological sort.
 * level[0] for source nodes (in-degree 0), increasing downstream.
 * @param graph  The input graph (must be a DAG)
 * @param levels Output vector of size n, filled with per-node level indices
 * @return The maximum level (number of levels - 1), or -1 on error
 */
igraph_integer_t calculate_dag_levels(const igraph_t *graph, igraph_vector_int_t *levels);

typedef struct
{
	igraph_vector_int_t levels;
	int num_levels;
	igraph_integer_t node_count;
} MainPathPrep;

void *main_path_prepare(ExecutionContext *ctx);
void free_main_path_prep(void *result_data);

// SPLC animation command (menu-triggered with background worker)
void *compute_splc_animation(ExecutionContext *ctx);
void apply_splc_animation(ExecutionContext *ctx, void *result_data);
bool poll_splc_gpu(ExecutionContext *ctx);

#endif
