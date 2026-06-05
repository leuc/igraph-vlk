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

void app_context_init(AppContext *ctx, igraph_t *graph, MenuNode *root_menu)
{
	ctx->current_state = STATE_GRAPH_VIEW;
	ctx->root_menu = root_menu;
	ctx->active_menu_level = root_menu;
	ctx->pending_command = NULL;
	ctx->selection_step = 0;
	ctx->target_graph = graph;
}

void app_context_destroy(AppContext *ctx)
{
	// Cleanup if anything was allocated
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
		int current_left_button = glfwGetMouseButton(state->window, GLFW_MOUSE_BUTTON_LEFT);
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
				exec_ctx.current_graph = app->target_graph;
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
			} else if (app->pending_command->execute) {
				// Instant main-thread UI action (File > Open, Save, Exit, etc.)
				printf("[State] Executing instant UI action: %s\n", app->pending_command->display_name);

				// Create execution context
				ExecutionContext exec_ctx;
				exec_ctx.current_graph = app->target_graph;
				exec_ctx.params = app->pending_command->params;
				exec_ctx.num_params = app->pending_command->num_params;
				exec_ctx.update_visuals_callback = NULL;
				exec_ctx.app_state = state;

				// Execute immediately on main thread
				app->pending_command->execute(&exec_ctx);

				// Reset and return to menu
				app->pending_command = NULL;
				app->current_state = STATE_MENU_OPEN;
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
		// Check job status and update progress
		if (state->current_worker_job) {
			WorkerJobStatus status = worker_thread_get_job_status(state->current_worker_job, &state->job_progress);

			// Update status message from igraph handler
			const char *job_msg = worker_thread_get_job_status_message(state->current_worker_job);
			if (job_msg && job_msg[0]) {
				snprintf(state->job_status_message, sizeof(state->job_status_message), "%s", job_msg);
			}

			if (status == JOB_STATUS_COMPLETED) {
				printf("[State] Job completed successfully\n");

				// Safely apply layout on main thread from worker's result
				WorkerJob *job = state->current_worker_job;
				if (job) {
					// Apply dynamic result if available
					if (job->apply_func && job->result_data) {
						job->apply_func(job->ctx, job->result_data);
					}

					// Free dynamic result if available
					if (job->free_func && job->result_data) {
						job->free_func(job->result_data);
					}

					// Cleanup job and its resources
					pthread_mutex_lock(&state->worker_ctx.queue_mutex);
					if (state->worker_ctx.current_job == job) {
						state->worker_ctx.current_job = NULL;
					}
					pthread_mutex_unlock(&state->worker_ctx.queue_mutex);

					pthread_mutex_destroy(&job->snapshot_mutex);
					if (job->snapshot_initialized) {
						igraph_matrix_destroy(&job->snapshot_matrix);
					}
					pthread_mutex_destroy(&job->mutex);
					if (job->ctx) {
						free(job->ctx);
					}
					free(job);
				}

				state->job_in_progress = false;
				state->current_worker_job = NULL;

				// Check if this command produced visual results
				if (app->pending_command && app->pending_command->produces_visual_output) {
					app->has_visual_results = true;
					app->current_state = STATE_DISPLAY_RESULTS;
				} else {
					// Reset after execution but keep menu open
					app->pending_command = NULL;
					app->current_state = STATE_MENU_OPEN;
				}
			} else if (status == JOB_STATUS_FAILED || status == JOB_STATUS_CANCELLED) {
				printf("[State] Job failed or was cancelled\n");

				WorkerJob *job = state->current_worker_job;
				if (job) {
					pthread_mutex_lock(&state->worker_ctx.queue_mutex);
					if (state->worker_ctx.current_job == job) {
						state->worker_ctx.current_job = NULL;
					}
					pthread_mutex_unlock(&state->worker_ctx.queue_mutex);

					pthread_mutex_destroy(&job->snapshot_mutex);
					if (job->snapshot_initialized) {
						igraph_matrix_destroy(&job->snapshot_matrix);
					}
					pthread_mutex_destroy(&job->mutex);
					if (job->ctx) {
						free(job->ctx);
					}
					free(job);
				}

				state->job_in_progress = false;
				state->current_worker_job = NULL;
				app->pending_command = NULL;
				app->current_state = STATE_MENU_OPEN;
			}
			// If still running, continue polling
		}
		break;

	case STATE_DISPLAY_RESULTS:
		// Results are displayed. Wait for user to close/dismiss them.
		// For now, any keypress or click will return to graph view
		// TODO: Implement proper dismissal (e.g., ESC key, or "Close" button in UI)
		break;
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
