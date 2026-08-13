/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GRAPH_WRAPPERS_FOLLOW_SPEED_H
#define GRAPH_WRAPPERS_FOLLOW_SPEED_H

#include "interaction/state.h"

void apply_follow_speed_3sec(ExecutionContext *ctx, void *result_data);
void apply_follow_speed_9sec(ExecutionContext *ctx, void *result_data);
void apply_follow_speed_18sec(ExecutionContext *ctx, void *result_data);
void apply_follow_speed_27sec(ExecutionContext *ctx, void *result_data);

#endif
