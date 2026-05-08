#include "xr/openxr_frame.h"
#include "app_state.h"
#include "interaction/input.h"
#include "interaction/menu.h"
#include "interaction/state.h"
#include "ui/menu.h"
#include "vulkan/menu.h"

#include <stdio.h>

bool xr_init_vr(AppState *app)
{
	if (!xr_context_create_session(&app->xr_ctx, app->renderer.instance, app->renderer.physicalDevice, app->renderer.device, 0, 0)) {
		fprintf(stderr, "Failed to create XR session\n");
		return false;
	}
	renderer_setup_xr(&app->renderer, &app->xr_ctx);
	xr_context_init_input(&app->xr_ctx);
	return true;
}

void xr_process_input(AppState *app, float deltaTime)
{
	xr_context_poll_events(&app->xr_ctx);
	xr_context_sync_input(&app->xr_ctx);
	if (xr_context_is_action_pressed(&app->xr_ctx, app->xr_ctx.menu_action, 0)) {
		interaction_menu_toggle(app);
	}

	// Process thumbsticks for locomotion
	float tx = xr_context_get_thumbstick(&app->xr_ctx, 0, 0); // Left X (Strafe)
	float ty = xr_context_get_thumbstick(&app->xr_ctx, 0, 1); // Left Y (Forward/Back)
	float rx = xr_context_get_thumbstick(&app->xr_ctx, 1, 0); // Right X (Turn)
	float ry = xr_context_get_thumbstick(&app->xr_ctx, 1, 1); // Right Y (Up/Down)

	if (tx != 0.0f || ty != 0.0f || rx != 0.0f || ry != 0.0f) {

		// 1. Apply smooth turning to the virtual yaw
		if (rx != 0.0f) {
			app->vr_play_yaw += rx * deltaTime * 2.0f;
		}

		// 2. Get headset orientation AT the origin, but WITH our virtual yaw applied
		mat4 head_view;
		xr_context_get_view_matrix(&app->xr_ctx, 0, (vec3){0, 0, 0}, app->vr_play_yaw, head_view);

		// 3. Extract Forward and Right vectors from the View Matrix rows
		vec3 right = {head_view[0][0], head_view[1][0], head_view[2][0]};
		vec3 forward = {-head_view[0][2], -head_view[1][2], -head_view[2][2]};

		// 4. Project onto the XZ plane
		right[1] = 0.0f;
		forward[1] = 0.0f;
		glm_vec3_normalize(right);
		glm_vec3_normalize(forward);

		// 5. Calculate horizontal movement
		vec3 move_dir = {0, 0, 0};
		if (tx != 0.0f) {
			vec3 right_move;
			glm_vec3_scale(right, tx, right_move);
			glm_vec3_add(move_dir, right_move, move_dir);
		}
		if (ty != 0.0f) {
			vec3 fwd_move;
			glm_vec3_scale(forward, ty, fwd_move);
			glm_vec3_add(move_dir, fwd_move, move_dir);
		}

		glm_vec3_scale(move_dir, deltaTime * 20.0f, move_dir);

		// 6. Apply Vertical flying
		move_dir[1] += ry * deltaTime * 20.0f;

		// 7. Apply the final movement vector to the global offset
		glm_vec3_add(app->vr_play_offset, move_dir, app->vr_play_offset);
	}
}

void xr_render_frame(AppState *app, XrTime *last_predicted_display_time, int *consecutive_missed_frames)
{
	XrFrameState frameState = {.type = XR_TYPE_FRAME_STATE};
	xr_context_wait_frame(&app->xr_ctx, &frameState);
	xr_context_begin_frame(&app->xr_ctx);

	// OpenXR dictates we MUST ONLY render if shouldRender is true
	if (frameState.shouldRender) {
		// --- FRAME DROP DETECTION ---
		if (*last_predicted_display_time != 0) {
			XrDuration time_delta = frameState.predictedDisplayTime - *last_predicted_display_time;

			// If the time between frames is more than 1.5x the hardware refresh period, we missed a vsync
			if (time_delta > (frameState.predictedDisplayPeriod * 1.5)) {
				// Calculate exactly how many frames were dropped
				int dropped_count = (int)((time_delta + (frameState.predictedDisplayPeriod / 2)) / frameState.predictedDisplayPeriod - 1);
				*consecutive_missed_frames += dropped_count;

				printf("WARNING: Dropped %d frame(s). Delta: %.2f ms\n", dropped_count, time_delta / 1000000.0f);
			} else {
				// Frame hit successfully, reset missed count
				*consecutive_missed_frames = 0;
			}
		}
		*last_predicted_display_time = frameState.predictedDisplayTime;
		// ----------------------------

		// 1. Ensure GPU slot is ready AFTER xrWaitFrame has unblocked
		vkWaitForFences(app->renderer.device, 1, &app->renderer.inFlightFences[app->renderer.currentFrame], VK_TRUE, UINT64_MAX);
		vkResetFences(app->renderer.device, 1, &app->renderer.inFlightFences[app->renderer.currentFrame]);

		xr_context_locate_views(&app->xr_ctx, frameState.predictedDisplayTime);

		// 2. Acquire & Wait for ALL eyes first to prevent blocking CPU during command buffer recording
		uint32_t imageIndices[2];
		for (uint32_t i = 0; i < app->xr_ctx.view_count; i++) {
			XrSwapchainImageAcquireInfo acquireInfo = {.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
			xrAcquireSwapchainImage(app->xr_ctx.swapchains[i].handle, &acquireInfo, &imageIndices[i]);

			XrSwapchainImageWaitInfo waitInfo = {.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO, .timeout = XR_INFINITE_DURATION};
			xrWaitSwapchainImage(app->xr_ctx.swapchains[i].handle, &waitInfo);
		}

		// 3. Record command buffers uninterrupted
		vkResetCommandBuffer(app->renderer.commandBuffers[app->renderer.currentFrame], 0);
		VkCommandBufferBeginInfo bi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
		vkBeginCommandBuffer(app->renderer.commandBuffers[app->renderer.currentFrame], &bi);

		XrPosef head_pose = app->xr_ctx.views[0].pose;
		vec3 head_pos = {head_pose.position.x, head_pose.position.y, head_pose.position.z};

		// Evaluate hand tracking ONCE per frame
		XrPosef hand_pose;
		bool has_ray = xr_context_get_hand_pose(&app->xr_ctx, 1, frameState.predictedDisplayTime, &hand_pose);
		vec3 ray_origin = {0}, ray_dir = {0};

		if (has_ray) {
			// Raw tracking space data (relative to head)
			vec3 raw_pos;
			glm_vec3_sub((vec3){hand_pose.position.x, hand_pose.position.y, hand_pose.position.z}, head_pos, raw_pos);

			XrQuaternionf rot = hand_pose.orientation;
			mat4 rot_mat;
			glm_quat_mat4((float *)&rot, rot_mat);
			vec3 base_dir = {-rot_mat[2][0], -rot_mat[2][1], -rot_mat[2][2]};

			// Rotate tracking data by the player's virtual yaw
			mat4 yaw_mat;
			glm_mat4_identity(yaw_mat);
			glm_rotate(yaw_mat, app->vr_play_yaw, (vec3){0.0f, 1.0f, 0.0f});

			// Multiply direction (w=0) and position (w=1)
			vec4 dir4 = {base_dir[0], base_dir[1], base_dir[2], 0.0f};
			vec4 pos4 = {raw_pos[0], raw_pos[1], raw_pos[2], 1.0f};
			glm_mat4_mulv(yaw_mat, dir4, dir4);
			glm_mat4_mulv(yaw_mat, pos4, pos4);

			// Apply rotated vectors and add global locomotion offset
			ray_dir[0] = dir4[0];
			ray_dir[1] = dir4[1];
			ray_dir[2] = dir4[2];
			glm_vec3_normalize(ray_dir);

			// Add head position and locomotion offset
			ray_origin[0] = pos4[0] + head_pos[0] + app->vr_play_offset[0];
			ray_origin[1] = pos4[1] + head_pos[1] + app->vr_play_offset[1];
			ray_origin[2] = pos4[2] + head_pos[2] + app->vr_play_offset[2];
		}

		XrCompositionLayerProjectionView projectionViews[2]; // Stereo

		for (uint32_t i = 0; i < app->xr_ctx.view_count; i++) {
			mat4 eye_view, eye_proj;
			xr_context_get_view_matrix(&app->xr_ctx, i, app->vr_play_offset, app->vr_play_yaw, eye_view);
			xr_context_get_projection_matrix(&app->xr_ctx, i, 0.1f, 1000.0f, eye_proj);
			// Flip Y axis for Vulkan NDC (Y-down)
			eye_proj[1][1] *= -1.0f;

			VkRenderPass xrRP = app->renderer.renderPassXR != VK_NULL_HANDLE ? app->renderer.renderPassXR : app->renderer.renderPass;

			if (has_ray && i == 0) {
				MenuNode *hit = raycast_menu_vr(app, ray_origin, ray_dir);
				if (hit && hit->hovered && xr_context_is_action_pressed(&app->xr_ctx, app->xr_ctx.select_action, 1)) {
					handle_menu_selection(&app->app_ctx, hit);
				}
			}

			// Ensure menu state is flushed to GPU for current eye
			generate_vulkan_menu_buffers(&app->app_ctx, &app->renderer);

			renderer_render_scene(&app->renderer, app->renderer.commandBuffers[app->renderer.currentFrame], xrRP, app->renderer.xrFramebuffers[i][imageIndices[i]], (VkExtent2D){app->xr_ctx.swapchains[i].width, app->xr_ctx.swapchains[i].height}, eye_view, eye_proj, i, has_ray, ray_origin, ray_dir);

			projectionViews[i] = (XrCompositionLayerProjectionView){.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW,
																	.pose = app->xr_ctx.views[i].pose,
																	.fov = app->xr_ctx.views[i].fov,
																	.subImage = {
																		.swapchain = app->xr_ctx.swapchains[i].handle,
																		.imageRect = {{0, 0}, {(int32_t)app->xr_ctx.swapchains[i].width, (int32_t)app->xr_ctx.swapchains[i].height}},
																		.imageArrayIndex = 0,
																	}};
		}

		vkEndCommandBuffer(app->renderer.commandBuffers[app->renderer.currentFrame]);

		// 4. Submit to GPU
		VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &app->renderer.commandBuffers[app->renderer.currentFrame]};
		vkQueueSubmit(app->renderer.graphicsQueue, 1, &si, app->renderer.inFlightFences[app->renderer.currentFrame]);

		// 5. Release images back to Compositor
		for (uint32_t i = 0; i < app->xr_ctx.view_count; i++) {
			XrSwapchainImageReleaseInfo releaseInfo = {.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
			xrReleaseSwapchainImage(app->xr_ctx.swapchains[i].handle, &releaseInfo);
		}

		XrCompositionLayerProjection layer = {
			.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION,
			.space = app->xr_ctx.stage_space,
			.viewCount = app->xr_ctx.view_count,
			.views = projectionViews,
		};
		XrCompositionLayerBaseHeader *layerPtr = (XrCompositionLayerBaseHeader *)&layer;
		xr_context_end_frame(&app->xr_ctx, &frameState, &layerPtr, 1);

		app->renderer.currentFrame = (app->renderer.currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
	} else {
		// Compositor requested skip. End frame without layers, do NOT advance Vulkan fences
		xr_context_end_frame(&app->xr_ctx, &frameState, NULL, 0);
	}
}
