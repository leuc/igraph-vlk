#include "app_state.h"
#include "graph/graph_actions.h"
#include "graph/graph_io.h"
#include "graph/wrappers_layout.h"
#include "interaction/camera.h"
#include "interaction/input.h"
#include "interaction/menu.h"
#include "interaction/state.h"
#include "ui/hud.h"
#include "ui/menu.h"
#include "vulkan/app_path.h"
#include "vulkan/menu.h"
#include "vulkan/renderer.h"

#ifdef USE_OPENXR
#include "xr/openxr_frame.h"
#endif

#include <GLFW/glfw3.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * igraph-vlk: Vulkan-based 3D graph visualization
 *
 * A modular refactoring of main.c that uses the AppState pattern
 * to cleanly separate concerns between modules.
 */

int main(int argc, char **argv)
{
	app_path_init();

	AppState app = {0};

	// Set defaults
	app.last_picked_node = -1;
	app.last_picked_edge = -1;
	app.win_w = 3440;
	app.win_h = 1440;

#ifdef USE_OPENXR
	bool vr_requested = false;
#endif
	char *filename = NULL;

	for (int i = 1; i < argc; i++) {
#ifdef USE_OPENXR
		if (strcmp(argv[i], "--vr") == 0) {
			vr_requested = true;
			continue;
		}
#endif
		if (filename == NULL) {
			filename = argv[i];
		} else {
			fprintf(stderr, "Usage: %s <graph.graphml>\n", argv[0]);
			return EXIT_FAILURE;
		}
	}

	if (filename == NULL) {
		fprintf(stderr, "Usage: %s <graph.graphml>\n", argv[0]);
		return EXIT_FAILURE;
	}

	app.current_filename = filename;

	// Initialize graph data
	app.current_graph.graph_initialized = false;
	if (!graph_load_graphml(app.current_filename, &app.current_graph, LAYOUT_GRID_3D, NULL, NULL)) {
		fprintf(stderr, "Failed to load graph: %s\n", app.current_filename);
		return EXIT_FAILURE;
	}

	// Initialize GLFW
	if (!glfwInit()) {
		fprintf(stderr, "Failed to initialize GLFW\n");
		return EXIT_FAILURE;
	}

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	// Create window
	app.window = glfwCreateWindow(app.win_w, app.win_h, "Graph Sphere", NULL, NULL);
	if (!app.window) {
		fprintf(stderr, "Failed to create GLFW window\n");
		glfwTerminate();
		return EXIT_FAILURE;
	}

	// Store user pointer for callbacks
	glfwSetWindowUserPointer(app.window, &app);

	// Initialize input handling (registers GLFW callbacks)
	interaction_init(app.window);

	// Initialize VR (only when --vr is explicitly requested)
#ifdef USE_OPENXR
	if (vr_requested) {
		app.vr_enabled = xr_context_init(&app.xr_ctx, "igraph-vlk");
		if (!app.vr_enabled) {
			fprintf(stderr, "Failed to initialize OpenXR context.\n");
			graph_free_data(&app.current_graph);
			glfwDestroyWindow(app.window);
			glfwTerminate();
			return EXIT_FAILURE;
		}
		printf("OpenXR initialized, enabling VR mode.\n");
	} else {
		app.vr_enabled = false;
	}
#else
	app.vr_enabled = false;
#endif

	// Initialize renderer
#ifdef USE_OPENXR
	if (!renderer_init(&app.renderer, app.window, &app.current_graph, app.vr_enabled ? (void *)&app.xr_ctx : NULL)) {
#else
	if (!renderer_init(&app.renderer, app.window, &app.current_graph, NULL)) {
#endif
		fprintf(stderr, "Failed to initialize renderer\n");
		glfwDestroyWindow(app.window);
		glfwTerminate();
		return EXIT_FAILURE;
	}

#ifdef USE_OPENXR
	if (app.vr_enabled) {
		if (!xr_init_vr(&app)) {
			fprintf(stderr, "Failed to initialize VR session.\n");
			xr_context_cleanup(&app.xr_ctx);
			graph_free_data(&app.current_graph);
			renderer_cleanup(&app.renderer);
			glfwDestroyWindow(app.window);
			glfwTerminate();
			return EXIT_FAILURE;
		}
	}
#endif

	// Initialize camera
	camera_init(&app.camera);

	// Initialize UI/HUD
	ui_hud_init();

	// Initialize FSM menu system
	MenuNode *root_menu = (MenuNode *)malloc(sizeof(MenuNode));
	init_menu_tree(root_menu);
	app_context_init(&app.app_ctx, &app.current_graph.g, root_menu);

	// Initialize worker thread for long-running operations
	if (!worker_thread_init(&app.worker_ctx, 10)) {
		fprintf(stderr, "Failed to initialize worker thread\n");
		menu_tree_destroy(root_menu);
		graph_free_data(&app.current_graph);
		renderer_cleanup(&app.renderer);
		glfwDestroyWindow(app.window);
		glfwTerminate();
		return EXIT_FAILURE;
	}
	app.current_worker_job = NULL;
	app.job_in_progress = false;
	app.job_progress = 0.0f;
	strcpy(app.job_status_message, "Ready");

	// Initialize timing
	float lastFrame = 0.0f;
	float fpsTimer = 0.0f;
	int frameCount = 0;
	float currentFps = 0.0f;

#ifdef USE_OPENXR
	XrTime last_predicted_display_time = 0;
	int consecutive_missed_frames = 0;
#endif

	// Main loop
	while (!glfwWindowShouldClose(app.window)) {
		float currentFrame = (float)glfwGetTime();
		float deltaTime = currentFrame - lastFrame;
		lastFrame = currentFrame;

		// FPS calculation
		fpsTimer += deltaTime;
		frameCount++;
		if (fpsTimer >= 1.0f) {
			currentFps = frameCount / fpsTimer;
			frameCount = 0;
			fpsTimer = 0.0f;
		}

		// Process input (WASD movement)
		interaction_process_continuous_input(&app, deltaTime);

		// Update HUD text
		ui_hud_update(&app, currentFps);

		// Update App FSM and Menu transforms
		update_app_state(&app);
		update_menu_transforms(app.app_ctx.root_menu, &app.app_ctx.menu_spawn_basis);

		// Poll real-time layout snapshots from worker thread
		if (app.current_worker_job && app.current_graph.graph_initialized) {
			igraph_matrix_t snap;
			igraph_matrix_init(&snap, 0, 0);
			if (worker_thread_poll_snapshot(app.current_worker_job, &snap)) {
				if (snap.nrow == app.current_graph.node_count && snap.nrow > 0) {
					ExecutionContext ec = {0};
					ec.app_state = &app;
					ec.current_graph = &app.current_graph.g;
					apply_layout_matrix(&ec, &snap);
				}
			}
			igraph_matrix_destroy(&snap);
		}

		// Generate menu buffers if menu is open or processing
		if (app.app_ctx.current_state == STATE_MENU_OPEN || app.app_ctx.current_state == STATE_JOB_IN_PROGRESS || app.app_ctx.current_state == STATE_EXECUTING) {
			generate_vulkan_menu_buffers(&app.app_ctx, &app.renderer);
		} else {
			app.renderer.menuNodeCount = 0;
			app.renderer.textQuadInstanceCount = 0;
		}

#ifdef USE_OPENXR
		if (app.vr_enabled) {
			xr_process_input(&app, deltaTime);
		}

		if (app.vr_enabled && app.xr_ctx.session_running) {
			xr_render_frame(&app, &last_predicted_display_time, &consecutive_missed_frames);
		} else {
#endif
			{
				// Update base view matrix from camera
				renderer_update_view(&app.renderer, app.camera.pos, app.camera.front, app.camera.up);

				renderer_draw_frame(&app.renderer);
			}
#ifdef USE_OPENXR
		}

		glfwPollEvents();
	}

	// Cleanup
#else
		glfwPollEvents();
	}

	// Cleanup
#endif
	worker_thread_cleanup(&app.worker_ctx);
	app_context_destroy(&app.app_ctx);
	menu_tree_destroy(root_menu);
	graph_free_data(&app.current_graph);
	renderer_cleanup(&app.renderer);
#ifdef USE_OPENXR
	xr_context_cleanup(&app.xr_ctx);
#endif
	glfwDestroyWindow(app.window);
	glfwTerminate();

	return 0;
}
