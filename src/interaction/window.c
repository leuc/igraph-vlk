/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "interaction/window.h"
#include <limits.h>

static void window_focus_callback(GLFWwindow *window, int focused)
{
	AppState *state = (AppState *)glfwGetWindowUserPointer(window);
	if (!state)
		return;

	if (focused) {
		state->camera.first_mouse = true;
		glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	} else {
		glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
	}
}

static void window_framebuffer_size_callback(GLFWwindow *window, int width, int height)
{
	AppState *state = (AppState *)glfwGetWindowUserPointer(window);
	if (!state)
		return;
	state->win_w = width;
	state->win_h = height;
	state->renderer.framebufferResized = true;
}

void window_init_callbacks(GLFWwindow *window)
{
	glfwSetWindowFocusCallback(window, window_focus_callback);
	glfwSetFramebufferSizeCallback(window, window_framebuffer_size_callback);
}

void window_toggle_fullscreen(AppState *state)
{
	GLFWwindow *window = state->window;
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
}

void window_cycle_monitor(AppState *state, int direction)
{
	GLFWwindow *window = state->window;

	if (!state->is_fullscreen)
		return;

	GLFWmonitor *current = glfwGetWindowMonitor(window);
	if (!current)
		return;

	int count;
	GLFWmonitor **monitors = glfwGetMonitors(&count);
	int cx, cy;
	glfwGetMonitorPos(current, &cx, &cy);
	const GLFWvidmode *cmode = glfwGetVideoMode(current);
	int ccx = cx + cmode->width / 2;

	int best = -1;
	int best_dist = INT_MAX;

	for (int i = 0; i < count; i++) {
		if (monitors[i] == current)
			continue;
		int mx, my;
		glfwGetMonitorPos(monitors[i], &mx, &my);
		const GLFWvidmode *m = glfwGetVideoMode(monitors[i]);
		int mcx = mx + m->width / 2;
		int delta = mcx - ccx;

		if (direction < 0 && delta >= 0)
			continue;
		if (direction > 0 && delta <= 0)
			continue;

		int dist = direction < 0 ? -delta : delta;
		if (dist < best_dist) {
			best_dist = dist;
			best = i;
		}
	}

	if (best >= 0) {
		const GLFWvidmode *mode = glfwGetVideoMode(monitors[best]);
		glfwSetWindowMonitor(window, monitors[best], 0, 0, mode->width, mode->height, mode->refreshRate);
	}
}
