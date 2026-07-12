/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef WRAPPERS_FLOW_H
#define WRAPPERS_FLOW_H

#include "interaction/state.h"
#include <igraph.h>

void *compute_maxflow_sampling(ExecutionContext *ctx);
void apply_maxflow_sampling(ExecutionContext *ctx, void *result_data);
void free_maxflow_result(void *result_data);

#endif
