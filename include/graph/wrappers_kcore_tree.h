/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GRAPH_WRAPPERS_KCORE_TREE_H
#define GRAPH_WRAPPERS_KCORE_TREE_H

#include "interaction/state.h"

// Builds the k-core hierarchy tree in chunks on the worker thread (reporting
// progress/status between chunks and honoring ctx->running for cancellation),
// then walks it to compute a peeling-order reveal rank per vertex. Returns a
// heap KCoreTreeResult* (see wrappers_kcore_tree.c) on success, NULL on
// failure.
void *compute_kcore_tree_trigger(ExecutionContext *ctx);

// Resets to the shared grey baseline and uploads the ranks so the shader
// animates the reveal, exactly matching apply_bfs_trigger's shape.
void apply_kcore_tree_trigger(ExecutionContext *ctx, void *result_data);

void free_kcore_tree_result(void *result_data);

#endif // GRAPH_WRAPPERS_KCORE_TREE_H
