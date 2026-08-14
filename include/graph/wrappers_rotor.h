/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef WRAPPERS_ROTOR_H
#define WRAPPERS_ROTOR_H

#include "interaction/state.h"

void *compute_rotor_walk(ExecutionContext *ctx);
void *compute_rotor_aggregation(ExecutionContext *ctx);
void apply_rotor_routing(ExecutionContext *ctx, void *result_data);
void free_rotor_routing_result(void *result_data);

#endif
