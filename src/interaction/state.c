/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "interaction/state.h"
#include "app_state.h"
#include "graph/graph_core.h"
#include "graph/worker_thread.h"
#include "interaction/menu.h"
#include "interaction/picking.h"
#include "vulkan/renderer.h"
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void app_context_init(AppContext *ctx, MenuNode *root_menu)
{
	memset(ctx, 0, sizeof(*ctx));
	ctx->current_state = STATE_GRAPH_VIEW;
	ctx->menu.root = root_menu;
	ctx->menu.active_level = root_menu;
	ctx->menu.layout_revision = 1;
	ctx->menu.text_revision = 1;
	ctx->menu.scene_revision = 1;
}

void app_context_destroy(AppContext *ctx)
{
	(void)ctx;
}

void update_app_state(AppState *state)
{
	AppContext *app = &state->app_ctx;

	if (app->current_state == STATE_MENU_OPEN || app->current_state == STATE_AWAITING_SELECTION) {
#ifdef USE_OPENXR
		if (!state->vr_enabled)
#endif
			raycast_menu_crosshair(state);
	} else {
		menu_set_hovered(&app->menu, NULL);
	}

	switch (app->current_state) {
	case STATE_GRAPH_VIEW:
		break;

	case STATE_MENU_OPEN:
		break;

	case STATE_AWAITING_SELECTION:
		if (app->pending_command) {
			bool all_filled = true;
			for (int i = 0; i < app->pending_command->num_params; i++) {
				if ((app->pending_command->params[i].type == PARAM_TYPE_NODE_SELECTION || app->pending_command->params[i].type == PARAM_TYPE_EDGE_SELECTION) && app->pending_command->params[i].value.selection_id == -1) {
					all_filled = false;
					break;
				}
			}

			if (all_filled) {
				app->current_state = STATE_EXECUTING;
			}
		}
		break;

	case STATE_EXECUTING:
		if (app->pending_command) {
			if (app->pending_command->cmd_def) {
				printf("[State] Executing data-driven command: %s\n", app->pending_command->display_name);

				ExecutionContext exec_ctx = {0};
				exec_ctx.params = app->pending_command->params;
				exec_ctx.num_params = app->pending_command->num_params;
				exec_ctx.update_visuals_callback = NULL;
				exec_ctx.app_state = state;
				exec_ctx.running = true;
				exec_ctx.transition_duration = app->pending_command->cmd_def->transition_duration;

				if (state->worker_ctx.thread_running) {
					state->current_worker_job = worker_thread_submit_job(&state->worker_ctx, (CommandDef *)app->pending_command->cmd_def, &exec_ctx);

					if (state->current_worker_job) {
						state->job_in_progress = true;
						state->job_progress = 0.0f;
						snprintf(state->job_status_message, sizeof(state->job_status_message), "Processing %s...", app->pending_command->display_name);
						app->current_state = STATE_JOB_IN_PROGRESS;
						printf("[State] Submitted dynamic job to worker thread: %s\n", app->pending_command->display_name);
					} else {
						printf("[State] Dynamic job submission failed\n");
						app->pending_command = NULL;
						app->current_state = STATE_MENU_OPEN;
					}
				} else {
					printf("[State] Worker thread not available for data-driven command\n");
					app->pending_command = NULL;
					app->current_state = STATE_MENU_OPEN;
				}
			} else {
				printf("[State] Command '%s' has no implementation.\n", app->pending_command->display_name);
				app->pending_command = NULL;
				app->current_state = STATE_MENU_OPEN;
			}
		} else {
			app->current_state = STATE_MENU_OPEN;
		}
		break;

	case STATE_JOB_IN_PROGRESS:
		if (state->current_worker_job) {
			if (state->gpu_polling) {
				WorkerJob *job = state->current_worker_job;
				ExecutionContext ec = {0};
				ec.app_state = state;

				if (job->ctx && !job->ctx->running) {
					if (job->free_func && job->result_data) {
						job->free_func(job->result_data);
					}
					worker_job_free(&state->worker_ctx, job);
					state->current_worker_job = NULL;
					state->gpu_polling = false;
					state->job_in_progress = false;
					app->pending_command = NULL;
					app->current_state = STATE_MENU_OPEN;
					break;
				}

				bool done = job->gpu_poll_func(&ec);

				if (done) {
					if (job->free_func && job->result_data) {
						job->free_func(job->result_data);
					}
					worker_job_free(&state->worker_ctx, job);
					state->current_worker_job = NULL;
					state->gpu_polling = false;
					state->job_in_progress = false;
					app->pending_command = NULL;
					app->current_state = STATE_MENU_OPEN;
				}
				break;
			}

			WorkerJobStatus status = worker_thread_get_job_status(state->current_worker_job, &state->job_progress);

			const char *job_msg = worker_thread_get_job_status_message(state->current_worker_job);
			if (job_msg && job_msg[0]) {
				snprintf(state->job_status_message, sizeof(state->job_status_message), "%s", job_msg);
			}

			if (status == JOB_STATUS_COMPLETED) {
				WorkerJob *job = state->current_worker_job;
				if (job->gpu_poll_func) {
					if (job->apply_func && job->result_data) {
						job->apply_func(job->ctx, job->result_data);
					}
					state->gpu_polling = true;
				} else {
					if (job->apply_func && job->result_data) {
						job->apply_func(job->ctx, job->result_data);
						state->renderer.label.tree_needs_rebuild = true;
					}
					if (job->free_func && job->result_data) {
						job->free_func(job->result_data);
					}
					worker_job_free(&state->worker_ctx, job);
					state->current_worker_job = NULL;
					state->job_in_progress = false;
					app->pending_command = NULL;
					app->current_state = STATE_MENU_OPEN;
				}
			} else if (status == JOB_STATUS_FAILED || status == JOB_STATUS_CANCELLED) {
				printf("[State] Job failed or was cancelled\n");
				WorkerJob *failed_job = state->current_worker_job;
				if (failed_job->free_func && failed_job->result_data) {
					failed_job->free_func(failed_job->result_data);
				}
				worker_job_free(&state->worker_ctx, failed_job);
				state->current_worker_job = NULL;
				state->job_in_progress = false;
				app->pending_command = NULL;
				app->current_state = STATE_MENU_OPEN;
			}
		}
		break;

	case STATE_DISPLAY_RESULTS: {
		if (glfwGetMouseButton(state->win.handle, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS || glfwGetKey(state->win.handle, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
			app->has_visual_results = false;
			app->pending_command = NULL;
			app->current_state = STATE_GRAPH_VIEW;
		}
		break;
	}
	}
}
static bool enforce_single_open_branch(MenuNode *node, MenuNode *target)
{
	if (!node)
		return false;

	if (node == target) {
		return true;
	}

	bool found_in_subtree = false;
	MenuNode *child_containing_target = NULL;

	for (int i = 0; i < node->num_children; i++) {
		if (enforce_single_open_branch(node->children[i], target)) {
			found_in_subtree = true;
			child_containing_target = node->children[i];
		}
	}

	if (found_in_subtree) {
		for (int i = 0; i < node->num_children; i++) {
			if (node->children[i] != child_containing_target) {
				node->children[i]->is_expanded = false;
			}
		}
	}

	return found_in_subtree;
}

void handle_menu_selection(AppContext *app, MenuNode *selected_node)
{
	if (!selected_node)
		return;

	printf("[MENU] Activated: %s (%s)\n", selected_node->label, selected_node->type == NODE_BRANCH ? "branch" : "command");

	menu_set_info_card(&app->menu, NULL);

	enforce_single_open_branch(app->menu.root, selected_node);
	menu_invalidate(&app->menu, MENU_INVALIDATE_LAYOUT);

	if (selected_node->type == NODE_BRANCH) {
		selected_node->is_expanded = !selected_node->is_expanded;
		app->menu.active_level = selected_node;
	} else if (selected_node->type == NODE_LEAF_COMMAND) {
		app->pending_command = selected_node->command;
		app->selection_step = 0;

		for (int i = 0; i < app->pending_command->num_params; i++) {
			if (app->pending_command->params[i].type == PARAM_TYPE_NODE_SELECTION || app->pending_command->params[i].type == PARAM_TYPE_EDGE_SELECTION) {
				app->pending_command->params[i].value.selection_id = -1;
			}
		}

		check_pending_command_requirements(app);
	}
}

void apply_quit(ExecutionContext *ctx, void *result_data)
{
	(void)result_data;
	if (ctx && ctx->app_state && ctx->app_state->win.handle) {
		glfwSetWindowShouldClose(ctx->app_state->win.handle, GLFW_TRUE);
	}
}

void check_pending_command_requirements(AppContext *app)
{
	if (!app->pending_command)
		return;

	bool needs_selection = false;

	for (int i = 0; i < app->pending_command->num_params; i++) {
		ParameterType type = app->pending_command->params[i].type;
		if (type == PARAM_TYPE_NODE_SELECTION || type == PARAM_TYPE_EDGE_SELECTION) {
			needs_selection = true;
		}
	}

	if (needs_selection) {
		app->current_state = STATE_AWAITING_SELECTION;
	} else {
		app->current_state = STATE_EXECUTING;
	}
}
