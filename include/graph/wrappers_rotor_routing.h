/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GRAPH_WRAPPERS_ROTOR_ROUTING_H
#define GRAPH_WRAPPERS_ROTOR_ROUTING_H

#include "interaction/state.h"

void *compute_rotor_routing_trigger(ExecutionContext *ctx);
void apply_rotor_routing_trigger(ExecutionContext *ctx, void *result_data);
void free_rotor_routing_result(void *result_data);

void *compute_rotor_aggregation_trigger(ExecutionContext *ctx);
void apply_rotor_aggregation_trigger(ExecutionContext *ctx, void *result_data);
void apply_rotor_aggregation_preview(ExecutionContext *ctx, void *result_data);
void free_rotor_aggregation_result(void *result_data);

#endif
