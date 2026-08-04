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
 * SPC and SPE live main-path weighting commands. The worker prepares the DAG
 * and levels; the renderer advances the shared GPU pipeline one level per
 * tick and persists the final edge weights for the explicit Selection menu.
 */

// Worker-thread preparation (forces a DAG, computes levels)
void *compute_main_path_spc(ExecutionContext *ctx);
void *compute_main_path_spe(ExecutionContext *ctx);
void *compute_main_path_unit(ExecutionContext *ctx);

// Main-thread live GPU setup
void apply_main_path_weighting(ExecutionContext *ctx, void *result_data);

// Per-frame GPU lifecycle; returns true after live weighting readback
bool poll_main_path_weighting(ExecutionContext *ctx);

void free_main_path_weighting_result(void *result_data);

#endif
