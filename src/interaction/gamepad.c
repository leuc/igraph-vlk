/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "interaction/gamepad.h"
#include "app_state.h"
#include "interaction/menu.h"
#include "interaction/state.h"
#include <GLFW/glfw3.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define GAMEPAD_DEADZONE 0.2f
#define MAX_RAW_BUTTONS 32

static const char *button_names[15] = {"A", "B", "X", "Y", "LEFT_BUMPER", "RIGHT_BUMPER", "BACK", "START", "GUIDE", "LEFT_THUMB", "RIGHT_THUMB", "DPAD_UP", "DPAD_RIGHT", "DPAD_DOWN", "DPAD_LEFT"};

static GLFWgamepadstate prev_state;
static unsigned char raw_prev_buttons[MAX_RAW_BUTTONS];
static bool first_frame = true;
static int last_joystick_id = -1;

static float apply_deadzone(float value)
{
	if (fabsf(value) < GAMEPAD_DEADZONE)
		return 0.0f;
	return (value > 0.0f) ? (value - GAMEPAD_DEADZONE) / (1.0f - GAMEPAD_DEADZONE) : (value + GAMEPAD_DEADZONE) / (1.0f - GAMEPAD_DEADZONE);
}

int gamepad_get_first_active(void)
{
	for (int i = GLFW_JOYSTICK_1; i <= GLFW_JOYSTICK_LAST; i++) {
		if (!glfwJoystickPresent(i))
			continue;

		// Mapped gamepads are gentle — no raw device queries needed
		if (glfwJoystickIsGamepad(i)) {
			const char *name = glfwGetGamepadName(i);
			printf("[GAMEPAD] Found mapped gamepad at %d: %s\n", i, name ? name : "Unknown");
			return i;
		}

		// Only query axes/buttons as a last resort for unmapped joysticks
		int axes = 0, buttons = 0;
		glfwGetJoystickAxes(i, &axes);
		glfwGetJoystickButtons(i, &buttons);
		if (axes >= 4 && buttons >= 8) {
			const char *name = glfwGetJoystickName(i);
			printf("[GAMEPAD] Using raw joystick %d: %s\n", i, name ? name : "Unknown");
			return i;
		}
	}

	return -1;
}

static bool process_axes_and_buttons(AppState *state, float lx, float ly, float rx, float ry, const unsigned char *buttons, bool is_gamepad, float delta_time)
{
	Camera *camera = &state->camera;
	AppContext *app = &state->app_ctx;

	// START button
	int start_idx = is_gamepad ? GLFW_GAMEPAD_BUTTON_START : 7;
	int a_idx = is_gamepad ? GLFW_GAMEPAD_BUTTON_A : 0;
	int max_btn = is_gamepad ? 15 : 11;

	if (!first_frame && start_idx < max_btn && buttons[start_idx] == GLFW_PRESS && (is_gamepad ? prev_state.buttons[start_idx] : raw_prev_buttons[start_idx]) == GLFW_RELEASE) {
		printf("[GAMEPAD] START pressed\n");
		interaction_menu_toggle(state);
	}

	if (!first_frame && a_idx < max_btn && buttons[a_idx] == GLFW_PRESS && (is_gamepad ? prev_state.buttons[a_idx] : raw_prev_buttons[a_idx]) == GLFW_RELEASE) {
		if (app->crosshair_hovered_node) {
			printf("[GAMEPAD] A pressed, activating: %s\n", app->crosshair_hovered_node->label);
			handle_menu_selection(app, app->crosshair_hovered_node);
		}
	}

	// Debug: log any button press
	for (int i = 0; i < max_btn && i <= GLFW_GAMEPAD_BUTTON_LAST; i++) {
		int prev = is_gamepad ? prev_state.buttons[i] : raw_prev_buttons[i];
		if (buttons[i] == GLFW_PRESS && prev == GLFW_RELEASE) {
			printf("[GAMEPAD] %s (%d) pressed\n", button_names[i], i);
		}
	}

	if (app->current_state == STATE_GRAPH_VIEW || app->current_state == STATE_MENU_OPEN || app->current_state == STATE_AWAITING_SELECTION || app->current_state == STATE_EXECUTING || app->current_state == STATE_DISPLAY_RESULTS || app->current_state == STATE_JOB_IN_PROGRESS) {
		float look_x = apply_deadzone(rx);
		float look_y = apply_deadzone(ry);

		if (look_x != 0.0f || look_y != 0.0f) {
			camera->yaw += look_x * camera->sensitivity * 150.0f * delta_time;
			camera->pitch -= look_y * camera->sensitivity * 150.0f * delta_time;

			if (camera->pitch > 89.0f)
				camera->pitch = 89.0f;
			if (camera->pitch < -89.0f)
				camera->pitch = -89.0f;
			camera_update_vectors(camera);
		}

		float move_x = apply_deadzone(lx);
		float move_y = apply_deadzone(ly);

		if (move_x != 0.0f || move_y != 0.0f) {
			if (move_y < 0.0f)
				camera_process_keyboard(camera, CAMERA_DIR_FORWARD, -move_y * delta_time);
			else if (move_y > 0.0f)
				camera_process_keyboard(camera, CAMERA_DIR_BACKWARD, move_y * delta_time);

			if (move_x < 0.0f)
				camera_process_keyboard(camera, CAMERA_DIR_LEFT, -move_x * delta_time);
			else if (move_x > 0.0f)
				camera_process_keyboard(camera, CAMERA_DIR_RIGHT, move_x * delta_time);
		}
	}

	return true;
}

bool process_gamepad_input(int joystick_id, void *app_state_ptr, float delta_time)
{
	AppState *state = (AppState *)app_state_ptr;

	if (!glfwJoystickPresent(joystick_id))
		return false;

	// Reset state on reconnect (hot-plug)
	if (joystick_id != last_joystick_id) {
		first_frame = true;
		last_joystick_id = joystick_id;
	}

	// ===== GAMEPAD API path (mapped controller) =====
	GLFWgamepadstate gp_state;
	if (glfwJoystickIsGamepad(joystick_id) && glfwGetGamepadState(joystick_id, &gp_state)) {
		if (first_frame) {
			const char *name = glfwGetJoystickName(joystick_id);
			const char *gamepad_map = glfwGetGamepadName(joystick_id);
			printf("[GAMEPAD] Gamepad %d: %s (%s)\n", joystick_id, name ? name : "Unknown", gamepad_map ? gamepad_map : "no mapping");
		}

		process_axes_and_buttons(state, gp_state.axes[GLFW_GAMEPAD_AXIS_LEFT_X], gp_state.axes[GLFW_GAMEPAD_AXIS_LEFT_Y], gp_state.axes[GLFW_GAMEPAD_AXIS_RIGHT_X], gp_state.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y], gp_state.buttons, true, delta_time);

		prev_state = gp_state;
		first_frame = false;
		return true;
	}

	// ===== RAW joystick path (no mapping) =====
	int axis_count, button_count;
	const float *axes = glfwGetJoystickAxes(joystick_id, &axis_count);
	const unsigned char *buttons = glfwGetJoystickButtons(joystick_id, &button_count);
	if (!axes || !buttons)
		return false;

	if (first_frame) {
		const char *name = glfwGetJoystickName(joystick_id);
		printf("[GAMEPAD] Raw joystick %d: %s (%d axes, %d buttons)\n", joystick_id, name ? name : "Unknown", axis_count, button_count);
	}

	// Assume standard Xbox axis layout for raw joysticks:
	//   0=left X, 1=left Y, 3=right X, 4=right Y
	float lx = (axis_count > 0) ? axes[0] : 0.0f;
	float ly = (axis_count > 1) ? axes[1] : 0.0f;
	float rx = (axis_count > 3) ? axes[3] : 0.0f;
	float ry = (axis_count > 4) ? axes[4] : 0.0f;

	unsigned char mapped_buttons[15] = {0};
	int max_btn = button_count < 15 ? button_count : 15;
	for (int i = 0; i < max_btn; i++)
		mapped_buttons[i] = buttons[i];

	process_axes_and_buttons(state, lx, ly, rx, ry, mapped_buttons, false, delta_time);

	memcpy(raw_prev_buttons, buttons, button_count < MAX_RAW_BUTTONS ? button_count : MAX_RAW_BUTTONS);
	first_frame = false;
	return true;
}
