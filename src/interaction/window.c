/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "interaction/window.h"
#include "app_state.h"
#include <limits.h>
#include <stdio.h>

/* Default window size fraction of monitor work area */
#define WINDOW_FRACTION 0.80f
#define WINDOW_MIN_W 640
#define WINDOW_MIN_H 480

static const char *glfw_error_desc = NULL;
static void glfw_error_cb(int code, const char *desc)
{
	glfw_error_desc = desc;
	fprintf(stderr, "[GLFW Error %d] %s\n", code, desc);
}

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
	state->win.w = width;
	state->win.h = height;
	state->renderer.framebufferResized = true;
}

static void window_content_scale_callback(GLFWwindow *window, float xscale, float yscale)
{
	AppState *state = (AppState *)glfwGetWindowUserPointer(window);
	if (!state)
		return;
	state->win.content_scale_x = xscale;
	state->win.content_scale_y = yscale;
}

static void window_init_callbacks(GLFWwindow *window)
{
	glfwSetWindowFocusCallback(window, window_focus_callback);
	glfwSetFramebufferSizeCallback(window, window_framebuffer_size_callback);
	glfwSetWindowContentScaleCallback(window, window_content_scale_callback);
}

bool window_create(AppState *state)
{
	glfwSetErrorCallback(glfw_error_cb);
	if (!glfwInit()) {
		fprintf(stderr, "Failed to initialize GLFW%s%s\n", glfw_error_desc ? ": " : "", glfw_error_desc ? glfw_error_desc : "");
		return false;
	}

	/* Query monitor work area — Wayland returns 0,0 for position */
	GLFWmonitor *primary = glfwGetPrimaryMonitor();
	if (!primary) {
		fprintf(stderr, "No primary monitor found\n");
		return false;
	}

	int wx = 0, wy = 0, ww, wh;
	glfwGetMonitorWorkarea(primary, &wx, &wy, &ww, &wh);

	ww = (int)((float)ww * WINDOW_FRACTION);
	wh = (int)((float)wh * WINDOW_FRACTION);
	if (ww < WINDOW_MIN_W)
		ww = WINDOW_MIN_W;
	if (wh < WINDOW_MIN_H)
		wh = WINDOW_MIN_H;

	/* Only compute center position on platforms that support window positioning (not Wayland) */
	int cx = 0, cy = 0;
	bool can_position = (wx != 0 || wy != 0);
	if (can_position) {
		const GLFWvidmode *mode = glfwGetVideoMode(primary);
		cx = wx + ((int)((float)(mode->width - ww) * 0.5f));
		cy = wy + ((int)((float)(mode->height - wh) * 0.5f));
	}

	/* Platform-specific GLFW window hints */
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
	glfwWindowHintString(GLFW_WAYLAND_APP_ID, "igraph-vlk");

#if defined(_GLFW_COCOA)
	glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_TRUE);
	glfwWindowHint(GLFW_COCOA_GRAPHICS_SWITCHING, GLFW_TRUE);
#elif defined(_GLFW_WIN32)
	glfwWindowHint(GLFW_WIN32_KEYBOARD_MENU, GLFW_TRUE);
#elif defined(_GLFW_X11) || defined(_GLFW_WAYLAND)
	glfwWindowHint(GLFW_SCALE_FRAMEBUFFER, GLFW_TRUE);
#endif

	state->win.handle = glfwCreateWindow(ww, wh, "igraph-vlk", NULL, NULL);
	if (!state->win.handle) {
		fprintf(stderr, "Failed to create GLFW window\n");
		return false;
	}

	glfwSetWindowUserPointer(state->win.handle, state);

	/* Query actual window size — compositors may clamp */
	glfwGetWindowSize(state->win.handle, &state->win.w, &state->win.h);

	/* Query initial content scale */
	float xscale = 1.0f, yscale = 1.0f;
	glfwGetWindowContentScale(state->win.handle, &xscale, &yscale);
	state->win.content_scale_x = xscale;
	state->win.content_scale_y = yscale;

	/* Save windowed position for fullscreen restore */
	state->win.x = cx;
	state->win.y = cy;
	state->win.is_fullscreen = false;
	state->win.can_position = can_position;

	/* Register callbacks and show */
	window_init_callbacks(state->win.handle);
	if (can_position)
		glfwSetWindowPos(state->win.handle, cx, cy);
	glfwShowWindow(state->win.handle);

	return true;
}

void window_toggle_fullscreen(WindowState *win)
{
	GLFWwindow *window = win->handle;
	win->is_fullscreen = !win->is_fullscreen;

	if (win->is_fullscreen) {
		if (win->can_position)
			glfwGetWindowPos(window, &win->x, &win->y);
		glfwGetWindowSize(window, &win->w, &win->h);
		GLFWmonitor *monitor = glfwGetWindowMonitor(window);
		if (!monitor)
			monitor = glfwGetPrimaryMonitor();
		const GLFWvidmode *mode = glfwGetVideoMode(monitor);
		glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
	} else {
		glfwSetWindowMonitor(window, NULL, win->x, win->y, win->w, win->h, 0);
	}
}

void window_cycle_monitor(WindowState *win, int direction)
{
	GLFWwindow *window = win->handle;

	if (!win->is_fullscreen)
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
