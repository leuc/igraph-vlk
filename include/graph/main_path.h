/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GRAPH_MAIN_PATH_H
#define GRAPH_MAIN_PATH_H

#include "interaction/state.h"

void *compute_main_path_splc_basket(ExecutionContext *ctx);
void *compute_main_path_spc_basket(ExecutionContext *ctx);
void *compute_main_path_spe_basket(ExecutionContext *ctx);
void *compute_main_path_splc_path(ExecutionContext *ctx);
void *compute_main_path_spc_path(ExecutionContext *ctx);
void *compute_main_path_spe_path(ExecutionContext *ctx);

void apply_main_path_selection(ExecutionContext *ctx, void *result_data);
void free_main_path_selection(void *result_data);

#endif
