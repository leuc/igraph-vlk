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
#include "vulkan/animation_manager.h"
#include "vulkan/menu.h"
#include "vulkan/renderer.h"
#include <GLFW/glfw3.h>

#include <getopt.h>
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
	// Parse command line arguments
	int opt;
	static struct option long_options[] = {{"layout", 1, 0, 'l'}, {"node-attr", 1, 0, 1}, {"edge-attr", 1, 0, 2}, {0, 0, 0, 0}};

	AppState app = {0};

	// Set defaults
	app.current_layout = LAYOUT_OPENORD_3D;
	app.current_cluster = CLUSTER_FASTGREEDY;
	app.current_comm_arrangement = COMMUNITY_ARRANGEMENT_NONE;
	app.last_picked_node = -1;
	app.last_picked_edge = -1;
	app.win_w = 3440;
	app.win_h = 1440;

	while ((opt = getopt_long(argc, argv, "l:", long_options, NULL)) != -1) {
		switch (opt) {
		case 'l':
			if (strcmp(optarg, "fr") == 0)
				app.current_layout = LAYOUT_FR_3D;
			else if (strcmp(optarg, "kk") == 0)
				app.current_layout = LAYOUT_KK_3D;
			else if (strcmp(optarg, "umap") == 0)
				app.current_layout = LAYOUT_UMAP_3D;
			break;
		case 1:
			app.node_attr = optarg;
			break;
		case 2:
			app.edge_attr = optarg;
			break;
		}
	}

	if (optind >= argc) {
		fprintf(stderr,
				"Usage: %s [--layout <fr|kk|umap>] [--node-attr <attr>] "
				"[--edge-attr <attr>] <graph.graphml>\n",
				argv[0]);
		return EXIT_FAILURE;
	}

	app.current_filename = argv[optind];

	// Initialize graph data
	app.current_graph.graph_initialized = false;
	if (graph_load_graphml(app.current_filename, &app.current_graph, app.current_layout, app.node_attr, app.edge_attr) != 0) {
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

	// Initialize OpenXR (Display Only)
	app.vr_enabled = xr_context_init(&app.xr_ctx, "igraph-vlk");
	if (app.vr_enabled) {
		printf("OpenXR HMD detected, enabling VR mode.\n");
	} else {
		printf("No HMD detected, running in desktop mode.\n");
	}

	// Initialize renderer
	if (renderer_init(&app.renderer, app.window, &app.current_graph, app.vr_enabled ? &app.xr_ctx : NULL) != 0) {
		fprintf(stderr, "Failed to initialize renderer\n");
		glfwDestroyWindow(app.window);
		glfwTerminate();
		return EXIT_FAILURE;
	}

	if (app.vr_enabled) {
		if (!xr_context_create_session(&app.xr_ctx, app.renderer.instance, app.renderer.physicalDevice, app.renderer.device, 0, 0)) {
			fprintf(stderr, "Failed to create XR session\n");
			app.vr_enabled = false;
		} else {
			renderer_setup_xr(&app.renderer, &app.xr_ctx);
			xr_context_init_input(&app.xr_ctx);
		}
	}

	// Initialize animation manager
	animation_manager_init(&app.anim_manager, &app.renderer, &app.current_graph);

	// Initialize camera
	camera_init(&app.camera);

	// Initialize UI/HUD
	ui_hud_init();

	// Initialize FSM menu system
	MenuNode *root_menu = (MenuNode *)malloc(sizeof(MenuNode));
	init_menu_tree(root_menu);
	app_context_init(&app.app_ctx, &app.current_graph.g, root_menu);
	app.renderer.app_ctx_ptr = &app.app_ctx;

	// Initialize worker thread for long-running operations
	if (worker_thread_init(&app.worker_ctx, 10) != 0) {
		fprintf(stderr, "Failed to initialize worker thread\n");
		destroy_menu_tree(root_menu);
		graph_free_data(&app.current_graph);
		animation_manager_cleanup(&app.anim_manager);
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

		// Update animations
		animation_manager_update(&app.anim_manager, deltaTime);
		if (app.anim_manager.num_animations > 0) {
			renderer_update_graph(&app.renderer, &app.current_graph);
		}

		// Step background layout
		graph_action_step_background_layout(&app);

		// Generate menu buffers if menu is open or processing
		if (app.app_ctx.current_state == STATE_MENU_OPEN || app.app_ctx.current_state == STATE_JOB_IN_PROGRESS || app.app_ctx.current_state == STATE_EXECUTING) {
			generate_vulkan_menu_buffers(&app.app_ctx, &app.renderer);
		} else {
			app.renderer.menuNodeCount = 0;
			app.renderer.menuTextCharCount = 0;
		}

		if (app.vr_enabled) {
			xr_context_poll_events(&app.xr_ctx);
			xr_context_sync_input(&app.xr_ctx);
			if (xr_context_is_action_pressed(&app.xr_ctx, app.xr_ctx.menu_action, 0)) { // 0 = Left hand
				interaction_menu_toggle(&app);
			}
		}

		if (app.vr_enabled && app.xr_ctx.session_running) {
			// Sync with XR frame
			XrFrameState frameState = {.type = XR_TYPE_FRAME_STATE};
			xr_context_begin_frame(&app.xr_ctx, &frameState);

			// Separate Desktop Presentation logic
			uint32_t ii = -1;
			VkResult res = vkAcquireNextImageKHR(app.renderer.device, app.renderer.swapchain, 0, app.renderer.imageAvailableSemaphores[app.renderer.currentFrame], VK_NULL_HANDLE, &ii);
			bool has_desktop = (res == VK_SUCCESS || res == VK_SUBOPTIMAL_KHR);

			vkWaitForFences(app.renderer.device, 1, &app.renderer.inFlightFences[app.renderer.currentFrame], VK_TRUE, UINT64_MAX);
			vkResetFences(app.renderer.device, 1, &app.renderer.inFlightFences[app.renderer.currentFrame]);

			vkResetCommandBuffer(app.renderer.commandBuffers[app.renderer.currentFrame], 0);
			VkCommandBufferBeginInfo bi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
			vkBeginCommandBuffer(app.renderer.commandBuffers[app.renderer.currentFrame], &bi);

			XrCompositionLayerProjectionView projectionViews[2]; // Stereo

			for (uint32_t i = 0; i < app.xr_ctx.view_count; i++) {
				uint32_t imageIndex;
				XrSwapchainImageAcquireInfo acquireInfo = {.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
				xrAcquireSwapchainImage(app.xr_ctx.swapchains[i].handle, &acquireInfo, &imageIndex);

				XrSwapchainImageWaitInfo waitInfo = {.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO, .timeout = XR_INFINITE_DURATION};
				xrWaitSwapchainImage(app.xr_ctx.swapchains[i].handle, &waitInfo);

				mat4 eye_view, eye_proj;
				xr_context_get_view_matrix(&app.xr_ctx, i, eye_view);
				xr_context_get_projection_matrix(&app.xr_ctx, i, 0.1f, 1000.0f, eye_proj);
				// Flip Y axis for Vulkan NDC (Y-down)
				eye_proj[1][1] *= -1.0f;

				// Render to XR Swapchain (use XR view/proj directly, no camera multiplication)
				VkRenderPass xrRP = app.renderer.renderPassXR != VK_NULL_HANDLE ? app.renderer.renderPassXR : app.renderer.renderPass;
				renderer_render_scene(&app.renderer, app.renderer.commandBuffers[app.renderer.currentFrame], xrRP, app.renderer.xrFramebuffers[i][imageIndex], (VkExtent2D){app.xr_ctx.swapchains[i].width, app.xr_ctx.swapchains[i].height}, eye_view, eye_proj, i);

				// Render to desktop companion (Left eye only) using camera view/proj, separate UBO slot
				if (i == 0 && has_desktop) {
					renderer_update_view(&app.renderer, app.camera.pos, app.camera.front, app.camera.up);
					renderer_render_scene(&app.renderer, app.renderer.commandBuffers[app.renderer.currentFrame], app.renderer.renderPass, app.renderer.framebuffers[ii], app.renderer.swapchainExtent, app.renderer.ubo.view, app.renderer.ubo.proj, 2);
				}

				XrSwapchainImageReleaseInfo releaseInfo = {.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
				xrReleaseSwapchainImage(app.xr_ctx.swapchains[i].handle, &releaseInfo);

				projectionViews[i] = (XrCompositionLayerProjectionView){.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW,
																		.pose = app.xr_ctx.views[i].pose,
																		.fov = app.xr_ctx.views[i].fov,
																		.subImage = {
																			.swapchain = app.xr_ctx.swapchains[i].handle,
																			.imageRect = {{0, 0}, {(int32_t)app.xr_ctx.swapchains[i].width, (int32_t)app.xr_ctx.swapchains[i].height}},
																			.imageArrayIndex = 0,
																		}};
			}

			vkEndCommandBuffer(app.renderer.commandBuffers[app.renderer.currentFrame]);

			VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &app.renderer.commandBuffers[app.renderer.currentFrame]};

			// Only use desktop semaphores if we successfully acquired a desktop image
			if (has_desktop) {
				si.waitSemaphoreCount = 1;
				si.pWaitSemaphores = &app.renderer.imageAvailableSemaphores[app.renderer.currentFrame];
				si.pWaitDstStageMask = &waitStage;
				si.signalSemaphoreCount = 1;
				si.pSignalSemaphores = &app.renderer.renderFinishedSemaphores[app.renderer.currentFrame];
			}

			vkQueueSubmit(app.renderer.graphicsQueue, 1, &si, app.renderer.inFlightFences[app.renderer.currentFrame]);

			if (has_desktop) {
				VkPresentInfoKHR pi = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, .waitSemaphoreCount = 1, .pWaitSemaphores = &app.renderer.renderFinishedSemaphores[app.renderer.currentFrame], .swapchainCount = 1, .pSwapchains = &app.renderer.swapchain, .pImageIndices = &ii};
				vkQueuePresentKHR(app.renderer.presentQueue, &pi);
			}

			XrCompositionLayerProjection layer = {
				.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION,
				.space = app.xr_ctx.stage_space,
				.viewCount = app.xr_ctx.view_count,
				.views = projectionViews,
			};
			XrCompositionLayerBaseHeader *layerPtr = (XrCompositionLayerBaseHeader *)&layer;
			xr_context_end_frame(&app.xr_ctx, &frameState, &layerPtr, 1);

			app.renderer.currentFrame = (app.renderer.currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
		} else {
			// Update base view matrix from camera
			renderer_update_view(&app.renderer, app.camera.pos, app.camera.front, app.camera.up);

			renderer_draw_frame(&app.renderer);
		}

		glfwPollEvents();
	}

	// Cleanup
	worker_thread_cleanup(&app.worker_ctx);
	app_context_destroy(&app.app_ctx);
	destroy_menu_tree(root_menu);
	graph_free_data(&app.current_graph);
	animation_manager_cleanup(&app.anim_manager);
	renderer_cleanup(&app.renderer);
	glfwDestroyWindow(app.window);
	glfwTerminate();

	return 0;
}
