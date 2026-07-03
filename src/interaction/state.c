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
	ctx->current_state = STATE_GRAPH_VIEW;
	ctx->root_menu = root_menu;
	ctx->active_menu_level = root_menu;
	ctx->pending_command = NULL;
	ctx->selection_step = 0;
}

void app_context_destroy(AppContext *ctx)
{
	// Stub: AppContext owns no dynamically-allocated resources directly.
	// The menu tree, graph data, renderer, and worker thread are
	// cleaned up by their own dedicated functions in main.c shutdown.
	(void)ctx;
}

void update_app_state(AppState *state)
{
	AppContext *app = &state->app_ctx;

	// Perform crosshair raycasting to track hover in menu-related states
	if (app->current_state == STATE_MENU_OPEN || app->current_state == STATE_AWAITING_SELECTION) {

		// Raycast from camera through screen center (crosshair)
		MenuNode *hovered = raycast_menu_crosshair(state);
		app->crosshair_hovered_node = hovered;

		// Check for activation trigger (left mouse button press)
		int current_left_button = glfwGetMouseButton(state->win.handle, GLFW_MOUSE_BUTTON_LEFT);
		bool mouse_just_pressed = (current_left_button == GLFW_PRESS && state->prev_left_mouse_button == GLFW_RELEASE);

		if (hovered && mouse_just_pressed) {
			printf("[DEBUG] Triggered menu option: %s\n", hovered->label);
			handle_menu_selection(app, hovered);
		}

		state->prev_left_mouse_button = current_left_button;
	}

	switch (app->current_state) {
	case STATE_GRAPH_VIEW:
		// Normal navigation, handle "Menu Open" trigger (e.g., Space key)
		break;

	case STATE_MENU_OPEN:
		// Menu is visible, handle interactions with it
		break;

	case STATE_AWAITING_SELECTION:
		// Highlight nodes/edges for picking
		if (app->pending_command) {
			// Check if all parameters of type PARAM_TYPE_NODE_SELECTION or EDGE_SELECTION are filled
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
			// Check if this is a data-driven command with cmd_def (background worker)
			if (app->pending_command->cmd_def) {
				printf("[State] Executing data-driven command: %s\n", app->pending_command->display_name);

				// Create execution context
				ExecutionContext exec_ctx;
				exec_ctx.params = app->pending_command->params;
				exec_ctx.num_params = app->pending_command->num_params;
				exec_ctx.update_visuals_callback = NULL;
				exec_ctx.app_state = state;
				exec_ctx.running = true;

				// Submit job to worker thread
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
			// GPU polling phase: worker already completed, drive gpu_poll_func per frame
			if (state->gpu_polling) {
				WorkerJob *job = state->current_worker_job;
				ExecutionContext ec = {0};
				ec.app_state = state;

				bool done = job->gpu_poll_func(&ec);

				if (done) {
					// GPU work complete — finalize
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

			// CPU polling phase: wait for worker thread to complete
			WorkerJobStatus status = worker_thread_get_job_status(state->current_worker_job, &state->job_progress);

			const char *job_msg = worker_thread_get_job_status_message(state->current_worker_job);
			if (job_msg && job_msg[0]) {
				snprintf(state->job_status_message, sizeof(state->job_status_message), "%s", job_msg);
			}

			if (status == JOB_STATUS_COMPLETED) {
				WorkerJob *job = state->current_worker_job;
				if (job->gpu_poll_func) {
					// GPU job: apply_func does one-shot setup, then enter GPU polling phase
					if (job->apply_func && job->result_data) {
						job->apply_func(job->ctx, job->result_data);
					}
					state->gpu_polling = true;
					// State stays STATE_JOB_IN_PROGRESS — gpu_poll_func drives completion
				} else {
					// CPU-only job: apply + free immediately
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
		// Dismiss on left click or Escape key
		if (glfwGetMouseButton(state->win.handle, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS || glfwGetKey(state->win.handle, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
			app->has_visual_results = false;
			app->pending_command = NULL;
			app->current_state = STATE_GRAPH_VIEW;
		}
		break;
	}
	}
}
// TODO move
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

	app->info_card.is_visible = false;

	enforce_single_open_branch(app->root_menu, selected_node);

	if (selected_node->type == NODE_BRANCH) {
		selected_node->is_expanded = !selected_node->is_expanded;
		app->active_menu_level = selected_node;
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
		// No selection needed, can execute directly
		app->current_state = STATE_EXECUTING;
	}
}
