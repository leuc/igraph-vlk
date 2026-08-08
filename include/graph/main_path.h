/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GRAPH_MAIN_PATH_H
#define GRAPH_MAIN_PATH_H

#include "interaction/state.h"

void *compute_main_path_splc(ExecutionContext *ctx);
void *compute_main_path_spc(ExecutionContext *ctx);
void *compute_main_path_unit(ExecutionContext *ctx);
void *compute_main_path_spe(ExecutionContext *ctx);
void *compute_main_path_nppc(ExecutionContext *ctx);
void apply_main_path_weighting(ExecutionContext *ctx, void *result_data);
bool poll_main_path_weighting(ExecutionContext *ctx);
void free_main_path_prep(void *result_data);

void *compute_main_path_splc_basket(ExecutionContext *ctx);
void *compute_main_path_spc_basket(ExecutionContext *ctx);
void *compute_main_path_spe_basket(ExecutionContext *ctx);
void *compute_main_path_unit_basket(ExecutionContext *ctx);
void *compute_main_path_nppc_basket(ExecutionContext *ctx);
void *compute_main_path_splc_global(ExecutionContext *ctx);
void *compute_main_path_spc_global(ExecutionContext *ctx);
void *compute_main_path_spe_global(ExecutionContext *ctx);
void *compute_main_path_unit_global(ExecutionContext *ctx);
void *compute_main_path_nppc_global(ExecutionContext *ctx);

// Local main path
void *compute_main_path_splc_local(ExecutionContext *ctx);
void *compute_main_path_spc_local(ExecutionContext *ctx);
void *compute_main_path_spe_local(ExecutionContext *ctx);
void *compute_main_path_unit_local(ExecutionContext *ctx);
void *compute_main_path_nppc_local(ExecutionContext *ctx);

// Backward local main path
void *compute_main_path_splc_backward_local(ExecutionContext *ctx);
void *compute_main_path_spc_backward_local(ExecutionContext *ctx);
void *compute_main_path_spe_backward_local(ExecutionContext *ctx);
void *compute_main_path_unit_backward_local(ExecutionContext *ctx);
void *compute_main_path_nppc_backward_local(ExecutionContext *ctx);

// Multiple main paths (20% tolerance preset)
void *compute_main_path_splc_multiple(ExecutionContext *ctx);
void *compute_main_path_spc_multiple(ExecutionContext *ctx);
void *compute_main_path_spe_multiple(ExecutionContext *ctx);
void *compute_main_path_unit_multiple(ExecutionContext *ctx);
void *compute_main_path_nppc_multiple(ExecutionContext *ctx);

// Key-route main path (K=10 preset)
void *compute_main_path_splc_key_route(ExecutionContext *ctx);
void *compute_main_path_spc_key_route(ExecutionContext *ctx);
void *compute_main_path_spe_key_route(ExecutionContext *ctx);
void *compute_main_path_unit_key_route(ExecutionContext *ctx);
void *compute_main_path_nppc_key_route(ExecutionContext *ctx);

// Network of main paths
void *compute_main_path_splc_network(ExecutionContext *ctx);
void *compute_main_path_spc_network(ExecutionContext *ctx);
void *compute_main_path_spe_network(ExecutionContext *ctx);
void *compute_main_path_unit_network(ExecutionContext *ctx);
void *compute_main_path_nppc_network(ExecutionContext *ctx);

// Valued network (Hummon & Carley 1993) -- SPLC-only, see main_path_search.h
void *compute_main_path_splc_valued_network(ExecutionContext *ctx);

void apply_main_path_selection(ExecutionContext *ctx, void *result_data);
void free_main_path_selection(void *result_data);

#endif
