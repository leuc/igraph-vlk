#include "interaction/input.h"
#include "graph/graph_actions.h"
#include "interaction/camera.h"
#include "interaction/gamepad.h"
#include "interaction/menu.h"
#include "interaction/picking.h"
#include "interaction/spatial.h"
#include <GLFW/glfw3.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>

// Define min/max macros since we're using integers
#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))

// Edge routing mode count (must match renderer.h enum count)
#define EDGE_ROUTING_COUNT 2

static bool window_focused = true;
static int gamepad_id = -1;

static void key_callback(GLFWwindow *window, int key, int scancode, int action, int mods);
static void mouse_button_callback(GLFWwindow *window, int button, int action, int mods);
static void mouse_callback(GLFWwindow *window, double xpos, double ypos);
static void focus_callback(GLFWwindow *window, int focused);
static void joystick_callback(int jid, int event);
static void framebuffer_size_callback(GLFWwindow *window, int width, int height);

void interaction_init(GLFWwindow *window)
{
	glfwSetKeyCallback(window, key_callback);
	glfwSetMouseButtonCallback(window, mouse_button_callback);
	glfwSetCursorPosCallback(window, mouse_callback);
	glfwSetWindowFocusCallback(window, focus_callback);
	glfwSetJoystickCallback(joystick_callback);
	glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

	gamepad_id = gamepad_get_first_active();
	if (gamepad_id >= 0) {
		printf("[INPUT] Gamepad detected on joystick %d\n", gamepad_id);
	}
}

static void joystick_callback(int jid, int event)
{
	if (event == GLFW_DISCONNECTED && jid == gamepad_id) {
		printf("[INPUT] Gamepad %d disconnected\n", jid);
		gamepad_id = -1;
	} else if (event == GLFW_CONNECTED && gamepad_id < 0) {
		gamepad_id = gamepad_get_first_active();
	}
}

static void focus_callback(GLFWwindow *window, int focused)
{
	window_focused = focused;
	if (focused) {
		AppState *state = (AppState *)glfwGetWindowUserPointer(window);
		if (state)
			state->camera.first_mouse = true;
		glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	} else {
		glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
	}
}

void interaction_process_continuous_input(AppState *state, float delta_time)
{
	GLFWwindow *window = state->window;

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
			state->is_fullscreen = !state->is_fullscreen;
			if (state->is_fullscreen) {
				glfwGetWindowPos(window, &state->win_x, &state->win_y);
				glfwGetWindowSize(window, &state->win_w, &state->win_h);
				GLFWmonitor *monitor = glfwGetWindowMonitor(window);
				if (!monitor)
					monitor = glfwGetPrimaryMonitor();
				const GLFWvidmode *mode = glfwGetVideoMode(monitor);
				glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
			} else {
				glfwSetWindowMonitor(window, NULL, state->win_x, state->win_y, state->win_w, state->win_h, 0);
			}
			break;
		}
		// fall through for unmodified Enter
	case GLFW_KEY_LEFT:
		if ((mods & GLFW_MOD_ALT) && state->is_fullscreen) {
			GLFWmonitor *current = glfwGetWindowMonitor(window);
			if (!current)
				break;
			int count;
			GLFWmonitor **monitors = glfwGetMonitors(&count);
			int cx, cy;
			glfwGetMonitorPos(current, &cx, &cy);
			const GLFWvidmode *cmode = glfwGetVideoMode(current);
			int ccx = cx + cmode->width / 2;
			int best = -1;
			int best_dist = INT32_MAX;
			for (int i = 0; i < count; i++) {
				if (monitors[i] == current)
					continue;
				int mx, my;
				glfwGetMonitorPos(monitors[i], &mx, &my);
				const GLFWvidmode *m = glfwGetVideoMode(monitors[i]);
				int mcx = mx + m->width / 2;
				if (mcx < ccx) {
					int dist = ccx - mcx;
					if (dist < best_dist) {
						best_dist = dist;
						best = i;
					}
				}
			}
			if (best >= 0) {
				const GLFWvidmode *mode = glfwGetVideoMode(monitors[best]);
				glfwSetWindowMonitor(window, monitors[best], 0, 0, mode->width, mode->height, mode->refreshRate);
			}
			break;
		}
		break;
	case GLFW_KEY_RIGHT:
		if ((mods & GLFW_MOD_ALT) && state->is_fullscreen) {
			GLFWmonitor *current = glfwGetWindowMonitor(window);
			if (!current)
				break;
			int count;
			GLFWmonitor **monitors = glfwGetMonitors(&count);
			int cx, cy;
			glfwGetMonitorPos(current, &cx, &cy);
			const GLFWvidmode *cmode = glfwGetVideoMode(current);
			int ccx = cx + cmode->width / 2;
			int best = -1;
			int best_dist = INT32_MAX;
			for (int i = 0; i < count; i++) {
				if (monitors[i] == current)
					continue;
				int mx, my;
				glfwGetMonitorPos(monitors[i], &mx, &my);
				const GLFWvidmode *m = glfwGetVideoMode(monitors[i]);
				int mcx = mx + m->width / 2;
				if (mcx > ccx) {
					int dist = mcx - ccx;
					if (dist < best_dist) {
						best_dist = dist;
						best = i;
					}
				}
			}
			if (best >= 0) {
				const GLFWvidmode *mode = glfwGetVideoMode(monitors[best]);
				glfwSetWindowMonitor(window, monitors[best], 0, 0, mode->width, mode->height, mode->refreshRate);
			}
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
		state->renderer.labelTreeNeedsRebuild = true;
		break;
	case GLFW_KEY_KP_SUBTRACT:
	case GLFW_KEY_MINUS:
		state->renderer.layoutScale /= 1.2f;
		renderer_update_graph(&state->renderer, &state->current_graph);
		state->renderer.labelTreeNeedsRebuild = true;
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
	if (!window_focused)
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

static void framebuffer_size_callback(GLFWwindow *window, int width, int height)
{
	AppState *state = (AppState *)glfwGetWindowUserPointer(window);
	if (!state)
		return;
	state->win_w = width;
	state->win_h = height;
	state->renderer.framebufferResized = true;
}
