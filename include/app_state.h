/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "graph/graph_core.h"
#include "graph/graph_types.h"
#include "graph/worker_thread.h"
#include "interaction/camera.h"
#include "interaction/state.h"
#include "vulkan/renderer.h"

#ifdef USE_OPENXR
#include "xr/openxr_context.h"
#endif

/**
 * Central application state that glues together all modules.
 * This replaces all global variables previously in main.c.
 */
typedef struct AppState
{
	/* Core Subsystems */
	Renderer renderer;
#ifdef USE_OPENXR
	XrContext xr_ctx;
#endif
	bool vr_enabled;
	GraphData current_graph;
	Camera camera;

	/* Window / System State */
	GLFWwindow *window;
	bool is_fullscreen;
	int win_x, win_y, win_w, win_h;

	/* Application Logic State */
	char *current_filename;

	/* Interaction State */
	int last_picked_node;
	int last_picked_edge;
	int prev_left_mouse_button;
#ifdef USE_OPENXR
	vec3 vr_play_offset; // XR offset applied to stage space (thumbstick movement)
	float vr_play_yaw;	 // XR virtual body rotation (in radians)
#endif

	/* Timing */
	float last_frame_time;
	float fps_timer;
	int frame_count;
	float current_fps;

	/* FSM Menu System */
	AppContext app_ctx;

	/* Worker thread for long-running operations */
	WorkerThreadContext worker_ctx;

	/* Job tracking */
	WorkerJob *current_worker_job;
	bool job_in_progress;
	char job_status_message[256];
	float job_progress;
} AppState;
