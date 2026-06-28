/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef COMMAND_REGISTRY_H
#define COMMAND_REGISTRY_H
#include "interaction/state.h"
#include <igraph.h>

// Forward declare for the apply function (bridge to UI)
struct AppContext;

// 1. Pure math function. Returns allocated result (e.g., igraph_matrix_t*)
//    Receives ExecutionContext with graph access via ctx->app_state->current_graph.g
typedef void *(*IgraphWorkerFunc)(ExecutionContext *ctx);

// 2. Main thread function to sync result to the visualization state
typedef void (*IgraphApplyFunc)(ExecutionContext *ctx, void *result_data);

// 3. Cleanup function to free the result_data
typedef void (*IgraphFreeFunc)(void *result_data);

// 4. Per-frame GPU poll function. Returns true when GPU work is complete.
typedef bool (*IgraphGpuPollFunc)(ExecutionContext *ctx);

typedef struct CommandParamDef
{
	const char *name;
	ParameterType type;
	igraph_real_t min_val;
	igraph_real_t max_val;
	const char **enum_values;
	int num_enum_values;
} CommandParamDef;

typedef struct CommandDef
{
	const char *category_path; // e.g. "Layout/Force-Directed"
	const char *command_id;	   // e.g. "lay_force_fr"
	const char *display_name;  // e.g. "Fruchterman-Reingold"
	IgraphWorkerFunc worker_func;
	IgraphApplyFunc apply_func;
	IgraphFreeFunc free_func;
	IgraphGpuPollFunc gpu_poll_func;   // NULL = CPU-only, non-NULL = GPU job
	const CommandParamDef *param_defs; // NULL if no params
	int num_params;
} CommandDef;

extern const CommandDef g_command_registry[];
extern const int g_command_registry_size;

#endif
