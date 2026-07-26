/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef WRAPPERS_FILTER_H
#define WRAPPERS_FILTER_H

#include "interaction/state.h"

// Worker: returns a dummy pointer to signal "apply me"
void *compute_inline_pass(ExecutionContext *ctx);

// Worker: copies command params into a heap FilterParams struct
void *compute_filter_by_attr(ExecutionContext *ctx);

// Apply: reset all nodes to visible
void apply_filter_reset(ExecutionContext *ctx, void *result_data);

// Apply: filter by attribute (reads FilterParams from result_data)
void apply_filter_by_attr(ExecutionContext *ctx, void *result_data);

// Apply: reset all edges to visible
void apply_filter_edge_reset(ExecutionContext *ctx, void *result_data);

// Apply: filter edges by attribute (reads FilterParams from result_data)
void apply_filter_by_edge_attr(ExecutionContext *ctx, void *result_data);

// Free: frees FilterParams
void free_filter_params(void *result_data);

#endif // WRAPPERS_FILTER_H
