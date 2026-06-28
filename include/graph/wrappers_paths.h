/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef WRAPPERS_PATHS_H
#define WRAPPERS_PATHS_H

#include "interaction/state.h"
#include <igraph.h>

void *compute_igraph_diameter(ExecutionContext *ctx);
void *compute_igraph_radius(ExecutionContext *ctx);
void *compute_igraph_average_path_length(ExecutionContext *ctx);
void apply_info_card(ExecutionContext *ctx, void *result_data);
void info_card_free(void *result_data);

#endif
