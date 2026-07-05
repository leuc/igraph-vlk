/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "graph/wrappers_filter.h"

#include "app_state.h"
#include "graph/graph_filter_visibility.h"
#include "vulkan/renderer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Struct returned by compute_filter_by_attr, consumed by apply/free
typedef struct
{
	char *attr_name;
	char *attr_value;
} FilterParams;

void *compute_inline_pass(ExecutionContext *ctx)
{
	(void)ctx;
	return (void *)(uintptr_t)1;
}

void *compute_filter_by_attr(ExecutionContext *ctx)
{
	if (!ctx || !ctx->params)
		return NULL;
	FilterParams *fp = malloc(sizeof(FilterParams));
	if (!fp)
		return NULL;
	fp->attr_name = strdup(ctx->params[0].value.str_val ? ctx->params[0].value.str_val : "");
	fp->attr_value = strdup(ctx->params[1].value.str_val ? ctx->params[1].value.str_val : "");
	return fp;
}

void apply_filter_reset(ExecutionContext *ctx, void *result_data)
{
	if (!ctx || !ctx->app_state)
		return;
	(void)result_data;
	AppState *state = ctx->app_state;
	graph_filter_reset_visibility(&state->current_graph);
	state->renderer.needsAttributeUpload = VK_TRUE;
	renderer_update_graph(&state->renderer, &state->current_graph);
	state->renderer.label.tree_needs_rebuild = true;
	printf("[Filter] Show all nodes\n");
}

void apply_filter_by_attr(ExecutionContext *ctx, void *result_data)
{
	if (!ctx || !ctx->app_state || !result_data)
		return;
	AppState *state = ctx->app_state;
	FilterParams *fp = (FilterParams *)result_data;
	graph_filter_by_attribute(&state->current_graph, fp->attr_name, fp->attr_value);
	state->renderer.needsAttributeUpload = VK_TRUE;
	renderer_update_graph(&state->renderer, &state->current_graph);
	state->renderer.label.tree_needs_rebuild = true;
	printf("[Filter] Attribute '%s' = '%s'\n", fp->attr_name, fp->attr_value);
}

void free_filter_params(void *result_data)
{
	if (!result_data)
		return;
	FilterParams *fp = (FilterParams *)result_data;
	free(fp->attr_name);
	free(fp->attr_value);
	free(fp);
}
