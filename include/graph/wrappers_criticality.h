/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef WRAPPERS_CRITICALITY_H
#define WRAPPERS_CRITICALITY_H

#include "interaction/state.h"
#include <igraph/igraph.h>
#include <stdbool.h>

/**
 * Generalised criticality and baskets of nodes.
 *
 * Price & Evans, "Understanding Main Path Analysis", arXiv:2512.12355 §2.5.
 * A single "main path" is a brittle summary of a citation DAG: many
 * near-optimal paths pick out different key nodes. Instead, score every node
 * by how far it falls short of lying on an optimal path,
 *
 *     c(v) = H - h(v) - d(v)                                      (eq. 2.15)
 *
 * and report the *basket* of lowest-criticality nodes (eq. 2.16). The paper's
 * practical recommendation is the zero-criticality basket under unit weights.
 *
 * The height/depth dynamic programming runs on the GPU
 * (shaders/criticality.comp); these wrappers prepare the DAG on the worker
 * thread and finish the analysis once the GPU results land.
 *
 * Results written back to the graph:
 *   vertex "criticality-unit" / "criticality-spe"  continuous c(v)
 *   vertex "basket-unit" / "basket-spe"            1 = zero-criticality basket
 *   vertex "main-path"                             1 = on the extracted path
 */

// Worker-thread preparation (forces a DAG, computes levels)
void *compute_criticality_unit(ExecutionContext *ctx);
void *compute_criticality_spe(ExecutionContext *ctx);

// Main-thread one-shot GPU setup and submit
void apply_criticality_basket(ExecutionContext *ctx, void *result_data);

// Per-frame GPU lifecycle; finishes the analysis and returns true when done
bool poll_criticality_gpu(ExecutionContext *ctx);

void free_criticality_result(void *result_data);

#endif
