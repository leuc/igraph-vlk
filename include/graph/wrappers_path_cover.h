/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GRAPH_WRAPPERS_PATH_COVER_H
#define GRAPH_WRAPPERS_PATH_COVER_H

#include "interaction/state.h"

// Minimum path cover: reveals vertices in path-concatenation order.
void *compute_min_path_cover_trigger(ExecutionContext *ctx);

// Maximum antichain (via the solved min-path-cover flow network): antichain
// vertices lead the reveal, the rest of the graph fills in right after.
void *compute_max_antichain_trigger(ExecutionContext *ctx);

// Minimum chain cover (built from a minimum path cover): reveals vertices in
// chain-concatenation order.
void *compute_min_chain_cover_trigger(ExecutionContext *ctx);

// Shared apply: resets to the grey baseline, rebuilds edges (the worker may
// have deleted feedback-arc-set edges in place to force a DAG, like SPLC),
// then uploads the ranks so the shader animates the reveal.
void apply_path_cover_result(ExecutionContext *ctx, void *result_data);

void free_path_cover_result(void *result_data);

#endif // GRAPH_WRAPPERS_PATH_COVER_H
