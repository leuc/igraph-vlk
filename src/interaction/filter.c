/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "interaction/filter.h"

#include "app_state.h"
#include "graph/graph_filter_visibility.h"
#include "vulkan/renderer.h"
#include <stdio.h>

void execute_filter_reset(ExecutionContext *ctx)
{
	if (!ctx || !ctx->app_state)
		return;
	AppState *state = ctx->app_state;
	graph_filter_reset_visibility(&state->current_graph);
	state->renderer.needsAttributeUpload = VK_TRUE;
	renderer_update_graph(&state->renderer, &state->current_graph);
	state->renderer.label.tree_needs_rebuild = true;
	printf("[Filter] Show all nodes\n");
}

void execute_filter_by_attr(ExecutionContext *ctx)
{
	if (!ctx || !ctx->app_state)
		return;
	AppState *state = ctx->app_state;
	IgraphCommand *cmd = state->app_ctx.pending_command;
	if (!cmd || !cmd->user_data)
		return;
	FilterContext *fc = (FilterContext *)cmd->user_data;
	graph_filter_by_attribute(&state->current_graph, fc->attr_name, fc->attr_value);
	state->renderer.needsAttributeUpload = VK_TRUE;
	renderer_update_graph(&state->renderer, &state->current_graph);
	state->renderer.label.tree_needs_rebuild = true;
	printf("[Filter] Attribute '%s' = '%s'\n", fc->attr_name, fc->attr_value);
}
