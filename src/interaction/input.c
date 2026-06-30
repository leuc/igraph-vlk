/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "interaction/input.h"
#include "graph/graph_actions.h"
#include "interaction/camera.h"
#include "interaction/gamepad.h"
#include "interaction/menu.h"
#include "interaction/picking.h"
#include "interaction/spatial.h"
#include "interaction/window.h"
#include "vulkan/app_path.h"
#include "vulkan/renderer_update_node_labels.h"
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>

// Edge routing mode count (must match renderer.h enum count)
#define EDGE_ROUTING_COUNT 2

static int gamepad_id = -1;

static void key_callback(GLFWwindow *window, int key, int scancode, int action, int mods);
static void mouse_button_callback(GLFWwindow *window, int button, int action, int mods);
static void mouse_callback(GLFWwindow *window, double xpos, double ypos);
static void joystick_callback(int jid, int event);

static void load_gamepad_mappings(void)
{
	const char *path = app_path_resolve("gamecontrollerdb.txt");
	FILE *fp = fopen(path, "rb");
	if (!fp)
		return;
	fseek(fp, 0, SEEK_END);
	long size = ftell(fp);
	rewind(fp);
	char *buf = malloc(size + 1);
	if (buf) {
		size_t nread = fread(buf, 1, size, fp);
		buf[nread] = '\0';
		if (glfwUpdateGamepadMappings(buf))
			printf("[INPUT] Loaded gamepad mappings from gamecontrollerdb.txt\n");
		free(buf);
	}
	fclose(fp);
}

void interaction_init(GLFWwindow *window)
{
	glfwSetKeyCallback(window, key_callback);
	glfwSetMouseButtonCallback(window, mouse_button_callback);
	glfwSetCursorPosCallback(window, mouse_callback);
	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

	// Load mappings before scanning — GLFW won't fire CONNECTED
	// for devices already present at startup.
	load_gamepad_mappings();
	gamepad_id = gamepad_get_first_active();
	if (gamepad_id >= 0) {
		const char *name = glfwGetGamepadName(gamepad_id);
		printf("[INPUT] Gamepad detected on joystick %d: %s\n", gamepad_id, name ? name : "Unknown");
	}

	// Register callback for future hot-plug events only
	glfwSetJoystickCallback(joystick_callback);
}

static void joystick_callback(int jid, int event)
{
	if (event == GLFW_CONNECTED) {
		// Use jid directly — no scanning needed
		if (glfwJoystickIsGamepad(jid) && gamepad_id < 0) {
			gamepad_id = jid;
			const char *name = glfwGetGamepadName(jid);
			printf("[INPUT] Gamepad connected on joystick %d: %s\n", jid, name ? name : "Unknown");
		}
	} else if (event == GLFW_DISCONNECTED && jid == gamepad_id) {
		printf("[INPUT] Gamepad %d disconnected\n", jid);
		gamepad_id = -1;
	}
}

void interaction_process_continuous_input(AppState *state, float delta_time)
{
	GLFWwindow *window = state->win.handle;

	if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) {
		glfwSetWindowShouldClose(window, true);
	}

	// Determine movement speed (Shift increases speed)
	float speed_multiplier = 1.0f;
	if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS) {
		speed_multiplier = 3.0f;
	}
	float adjusted_delta = delta_time * speed_multiplier;

	if (state->app_ctx.current_state != STATE_GRAPH_VIEW && state->app_ctx.current_state != STATE_MENU_OPEN && state->app_ctx.current_state != STATE_AWAITING_SELECTION && state->app_ctx.current_state != STATE_EXECUTING && state->app_ctx.current_state != STATE_DISPLAY_RESULTS && state->app_ctx.current_state != STATE_JOB_IN_PROGRESS)
		return;

	// Handle camera movement
	if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
		camera_process_keyboard(&state->camera, CAMERA_DIR_FORWARD, adjusted_delta);
	if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
		camera_process_keyboard(&state->camera, CAMERA_DIR_BACKWARD, adjusted_delta);
	if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
		camera_process_keyboard(&state->camera, CAMERA_DIR_LEFT, adjusted_delta);
	if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
		camera_process_keyboard(&state->camera, CAMERA_DIR_RIGHT, adjusted_delta);

	// Process gamepad input if one is connected
	if (gamepad_id >= 0) {
		if (!process_gamepad_input(gamepad_id, state, delta_time)) {
			gamepad_id = -1;
		}
	}
}

static void key_callback(GLFWwindow *window, int key, int scancode, int action, int mods)
{
	if (action != GLFW_PRESS)
		return;

	AppState *state = (AppState *)glfwGetWindowUserPointer(window);
	if (!state)
		return;

	switch (key) {
	case GLFW_KEY_ENTER:
		if (mods & GLFW_MOD_ALT) {
			window_toggle_fullscreen(&state->win);
			break;
		}
		// fall through for unmodified Enter
	case GLFW_KEY_LEFT:
		if ((mods & GLFW_MOD_ALT) && state->win.is_fullscreen) {
			window_cycle_monitor(&state->win, -1);
			break;
		}
		break;
	case GLFW_KEY_RIGHT:
		if ((mods & GLFW_MOD_ALT) && state->win.is_fullscreen) {
			window_cycle_monitor(&state->win, 1);
			break;
		}
		break;
	case GLFW_KEY_N:
		state->renderer.showNodes = !state->renderer.showNodes;
		renderer_update_graph(&state->renderer, &state->current_graph);
		break;
	case GLFW_KEY_E:
		state->renderer.showEdges = !state->renderer.showEdges;
		renderer_update_graph(&state->renderer, &state->current_graph);
		break;
	case GLFW_KEY_M:
		// Cycle through edge routing modes
		state->renderer.currentRoutingMode = (state->renderer.currentRoutingMode + 1) % EDGE_ROUTING_COUNT;
		renderer_update_graph(&state->renderer, &state->current_graph);
		break;
	case GLFW_KEY_R:
		graph_action_reset(state);
		break;
	case GLFW_KEY_H:
		state->renderer.showUI = !state->renderer.showUI;
		break;
	case GLFW_KEY_SPACE:
		interaction_menu_toggle(state);
		break;
	case GLFW_KEY_1:
	case GLFW_KEY_2:
	case GLFW_KEY_3:
	case GLFW_KEY_4:
	case GLFW_KEY_5:
	case GLFW_KEY_6:
	case GLFW_KEY_7:
	case GLFW_KEY_8:
	case GLFW_KEY_9: {
		int min_deg = key - GLFW_KEY_0;
		if (min_deg > 0)
			graph_action_filter_degree(state, min_deg);
		break;
	}
	case GLFW_KEY_K: {
		int current_k = state->current_graph.props.coreness_filter;
		int new_k = current_k + 1;
		if (new_k > 20)
			new_k = 0;
		state->current_graph.props.coreness_filter = new_k;
		graph_action_filter_coreness(state, new_k);
		renderer_update_graph(&state->renderer, &state->current_graph);
		break;
	}
	case GLFW_KEY_J:
		graph_action_highlight_infrastructure(state);
		break;
	case GLFW_KEY_KP_ADD:
	case GLFW_KEY_EQUAL:
		state->renderer.layoutScale *= 1.2f;
		renderer_update_graph(&state->renderer, &state->current_graph);
		detail_card_update_position(&state->renderer, &state->current_graph);
		state->renderer.label.tree_needs_rebuild = true;
		break;
	case GLFW_KEY_KP_SUBTRACT:
	case GLFW_KEY_MINUS:
		state->renderer.layoutScale /= 1.2f;
		renderer_update_graph(&state->renderer, &state->current_graph);
		detail_card_update_position(&state->renderer, &state->current_graph);
		state->renderer.label.tree_needs_rebuild = true;
		break;
	}
}

static void mouse_button_callback(GLFWwindow *window, int button, int action, int mods)
{
	if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
		static double lastClickTime = 0;
		double currentTime = glfwGetTime();
		bool isDoubleClick = (currentTime - lastClickTime) < 0.3;
		lastClickTime = currentTime;

		AppState *state = (AppState *)glfwGetWindowUserPointer(window);
		if (state) {
			AppContext *app = &state->app_ctx;

			if (app->current_state == STATE_GRAPH_VIEW) {
				interaction_pick_object(state, isDoubleClick);
			} else if (app->current_state == STATE_MENU_OPEN) {
				double mx, my;
				glfwGetCursorPos(window, &mx, &my);
				MenuNode *selected = interaction_pick_menu_node(state, mx, my);
				if (selected) {
					handle_menu_selection(app, selected);
				}
			}
		}
	}
}

static void mouse_callback(GLFWwindow *window, double xpos, double ypos)
{
	if (!glfwGetWindowAttrib(window, GLFW_FOCUSED))
		return;

	AppState *state = (AppState *)glfwGetWindowUserPointer(window);
	if (!state)
		return;

	Camera *cam = &state->camera;
	AppContext *app = &state->app_ctx;

	// Initialize last position on first mouse movement to prevent jump
	if (cam->first_mouse) {
		cam->last_x = (float)xpos;
		cam->last_y = (float)ypos;
		cam->first_mouse = false;
	}

	float xoffset = (xpos - cam->last_x) * cam->sensitivity;
	float yoffset = (cam->last_y - ypos) * cam->sensitivity;

	cam->last_x = xpos;
	cam->last_y = ypos;

	cam->yaw += xoffset;
	cam->pitch += yoffset;

	if (cam->pitch > 89.0f)
		cam->pitch = 89.0f;
	if (cam->pitch < -89.0f)
		cam->pitch = -89.0f;

	camera_update_vectors(cam);
}
