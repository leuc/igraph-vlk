/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GRAPH_WRAPPERS_CYCLES_H
#define GRAPH_WRAPPERS_CYCLES_H

#include "interaction/state.h"
#include <igraph.h>

// Cycle analysis: remove feedback arc set to make graph acyclic
void *compute_remove_feedback_arc_set(ExecutionContext *ctx);
void apply_remove_feedback_arc_set(ExecutionContext *ctx, void *result_data);

// Graph simplification: remove multi-edges and loops
void *compute_igraph_simplify(ExecutionContext *ctx);
void apply_igraph_simplify(ExecutionContext *ctx, void *result_data);

void free_noop(void *result_data);

#endif
