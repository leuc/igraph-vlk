/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "xr/openxr_frame.h"
#include "app_state.h"
#include "interaction/input.h"
#include "interaction/menu.h"
#include "interaction/state.h"
#include "ui/menu.h"
#include "vulkan/menu.h"

#include <stdio.h>

#include "vulkan/renderer_draw.h"
#include "vulkan/renderer_lifecycle.h"
#include "vulkan/renderer_xr.h"
#include "vulkan/utils.h"

bool xr_init_vr(AppState *app)
{
	if (!xr_context_create_session(&app->xr_ctx, app->renderer.core.instance, app->renderer.core.physicalDevice, app->renderer.core.device, app->renderer.core.graphicsQueueFamily, 0)) {
		fprintf(stderr, "Failed to create XR session\n");
		return false;
	}
	renderer_setup_xr(&app->renderer, &app->xr_ctx);
	if (!xr_context_init_input(&app->xr_ctx)) {
		fprintf(stderr, "Failed to initialize XR input\n");
		return false;
	}
	return true;
}

void xr_process_input(AppState *app, float deltaTime)
{
	xr_context_poll_events(&app->xr_ctx);
	xr_context_sync_input(&app->xr_ctx);
	if (xr_context_is_action_pressed(&app->xr_ctx, app->xr_ctx.menu_action, 0)) {
		interaction_menu_toggle(app);
	}

	float tx = xr_context_get_thumbstick(&app->xr_ctx, 0, 0);
	float ty = xr_context_get_thumbstick(&app->xr_ctx, 0, 1);
	float rx = xr_context_get_thumbstick(&app->xr_ctx, 1, 0);
	float ry = xr_context_get_thumbstick(&app->xr_ctx, 1, 1);

	if (tx != 0.0f || ty != 0.0f || rx != 0.0f || ry != 0.0f) {

		if (rx != 0.0f) {
			app->vr_play_yaw += rx * deltaTime * 2.0f;
		}

		mat4 head_view;
		xr_context_get_view_matrix(&app->xr_ctx, 0, (vec3){0, 0, 0}, app->vr_play_yaw, head_view);

		vec3 right = {head_view[0][0], head_view[1][0], head_view[2][0]};
		vec3 forward = {-head_view[0][2], -head_view[1][2], -head_view[2][2]};

		right[1] = 0.0f;
		forward[1] = 0.0f;
		glm_vec3_normalize(right);
		glm_vec3_normalize(forward);

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

		move_dir[1] += ry * deltaTime * 20.0f;

		glm_vec3_add(app->vr_play_offset, move_dir, app->vr_play_offset);
	}
}

void xr_render_frame(AppState *app, XrTime *last_predicted_display_time, int *consecutive_missed_frames)
{
	XrFrameState frameState = {.type = XR_TYPE_FRAME_STATE};
	if (!xr_context_wait_frame(&app->xr_ctx, &frameState)) {
		fprintf(stderr, "XR: xrWaitFrame failed\n");
		return;
	}
	if (!xr_context_begin_frame(&app->xr_ctx)) {
		fprintf(stderr, "XR: xrBeginFrame failed\n");
		return;
	}

	if (frameState.shouldRender) {
		if (*last_predicted_display_time != 0) {
			XrDuration time_delta = frameState.predictedDisplayTime - *last_predicted_display_time;

			if (time_delta > (frameState.predictedDisplayPeriod * 1.5)) {
				int dropped_count = (int)((time_delta + (frameState.predictedDisplayPeriod / 2)) / frameState.predictedDisplayPeriod - 1);
				*consecutive_missed_frames += dropped_count;

				printf("WARNING: Dropped %d frame(s). Delta: %.2f ms\n", dropped_count, time_delta / 1000000.0f);
			} else {
				*consecutive_missed_frames = 0;
			}
		}
		*last_predicted_display_time = frameState.predictedDisplayTime;

		VkResult vkRes = vkWaitForFences(app->renderer.core.device, 1, &app->renderer.commands.inFlightFences[app->renderer.commands.currentFrame], VK_TRUE, UINT64_MAX);
		if (vkRes != VK_SUCCESS) {
			fprintf(stderr, "XR: vkWaitForFences failed: %d\n", vkRes);
			xr_context_end_frame(&app->xr_ctx, &frameState, NULL, 0);
			return;
		}
		if (!xr_context_locate_views(&app->xr_ctx, frameState.predictedDisplayTime)) {
			fprintf(stderr, "XR: xrLocateViews failed\n");
			xr_context_end_frame(&app->xr_ctx, &frameState, NULL, 0);
			return;
		}

		XrPosef head_pose = app->xr_ctx.views[0].pose;
		vec3 head_pos = {head_pose.position.x, head_pose.position.y, head_pose.position.z};
		XrPosef hand_pose;
		bool has_ray = xr_context_get_hand_pose(&app->xr_ctx, 1, frameState.predictedDisplayTime, &hand_pose);
		vec3 ray_origin = {0};
		vec3 ray_dir = {0};

		if (has_ray) {
			vec3 raw_pos;
			glm_vec3_sub((vec3){hand_pose.position.x, hand_pose.position.y, hand_pose.position.z}, head_pos, raw_pos);

			XrQuaternionf rot = hand_pose.orientation;
			mat4 rot_mat;
			glm_quat_mat4((float *)&rot, rot_mat);
			vec3 base_dir = {-rot_mat[2][0], -rot_mat[2][1], -rot_mat[2][2]};

			mat4 yaw_mat;
			glm_mat4_identity(yaw_mat);
			glm_rotate(yaw_mat, app->vr_play_yaw, (vec3){0.0f, 1.0f, 0.0f});

			vec4 dir4 = {base_dir[0], base_dir[1], base_dir[2], 0.0f};
			vec4 pos4 = {raw_pos[0], raw_pos[1], raw_pos[2], 1.0f};
			glm_mat4_mulv(yaw_mat, dir4, dir4);
			glm_mat4_mulv(yaw_mat, pos4, pos4);

			ray_dir[0] = dir4[0];
			ray_dir[1] = dir4[1];
			ray_dir[2] = dir4[2];
			glm_vec3_normalize(ray_dir);

			ray_origin[0] = pos4[0] + head_pos[0] + app->vr_play_offset[0];
			ray_origin[1] = pos4[1] + head_pos[1] + app->vr_play_offset[1];
			ray_origin[2] = pos4[2] + head_pos[2] + app->vr_play_offset[2];
		}

		if (app->app_ctx.current_state == STATE_MENU_OPEN && has_ray) {
			MenuNode *hit = raycast_menu_vr(app, ray_origin, ray_dir);
			if (hit && xr_context_is_action_pressed(&app->xr_ctx, app->xr_ctx.select_action, 1))
				handle_menu_selection(&app->app_ctx, hit);
		} else {
			menu_set_hovered(&app->app_ctx.menu, NULL);
		}
		menu_update_layout(&app->app_ctx.menu);
		bool menu_visible = app->app_ctx.current_state == STATE_MENU_OPEN || app->app_ctx.current_state == STATE_JOB_IN_PROGRESS || app->app_ctx.current_state == STATE_EXECUTING;
		renderer_menu_update(&app->renderer, &app->app_ctx.menu, menu_visible);

		uint32_t imageIndices[2];
		for (uint32_t i = 0; i < app->xr_ctx.view_count; i++) {
			XrSwapchainImageAcquireInfo acquireInfo = {.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
			XrResult xrRes = xrAcquireSwapchainImage(app->xr_ctx.swapchains[i].handle, &acquireInfo, &imageIndices[i]);
			if (XR_FAILED(xrRes)) {
				fprintf(stderr, "XR: xrAcquireSwapchainImage failed for view %u: %d\n", i, xrRes);
				xr_context_end_frame(&app->xr_ctx, &frameState, NULL, 0);
				return;
			}

			XrSwapchainImageWaitInfo waitInfo = {.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO, .timeout = XR_INFINITE_DURATION};
			xrRes = xrWaitSwapchainImage(app->xr_ctx.swapchains[i].handle, &waitInfo);
			if (XR_FAILED(xrRes)) {
				fprintf(stderr, "XR: xrWaitSwapchainImage failed for view %u: %d\n", i, xrRes);
				xr_context_end_frame(&app->xr_ctx, &frameState, NULL, 0);
				return;
			}
		}

		VK_CHECK(vkResetCommandBuffer(app->renderer.commands.commandBuffers[app->renderer.commands.currentFrame], 0), "XR: Failed to reset command buffer");
		VkCommandBufferBeginInfo bi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
		vkRes = vkBeginCommandBuffer(app->renderer.commands.commandBuffers[app->renderer.commands.currentFrame], &bi);
		if (vkRes != VK_SUCCESS) {
			fprintf(stderr, "XR: vkBeginCommandBuffer failed: %d\n", vkRes);
			xr_context_end_frame(&app->xr_ctx, &frameState, NULL, 0);
			return;
		}

		XrCompositionLayerProjectionView projectionViews[2];

		for (uint32_t i = 0; i < app->xr_ctx.view_count; i++) {
			mat4 eye_view, eye_proj;
			xr_context_get_view_matrix(&app->xr_ctx, i, app->vr_play_offset, app->vr_play_yaw, eye_view);
			xr_context_get_projection_matrix(&app->xr_ctx, i, 0.1f, 1000.0f, eye_proj);
			eye_proj[1][1] *= -1.0f;

			VkRenderPass xrRP = app->renderer.renderPassXR != VK_NULL_HANDLE ? app->renderer.renderPassXR : app->renderer.renderPass.renderPass;

			renderer_render_scene(&app->renderer, app->renderer.commands.commandBuffers[app->renderer.commands.currentFrame], xrRP, app->renderer.xrFramebuffers[i][imageIndices[i]], (VkExtent2D){app->xr_ctx.swapchains[i].width, app->xr_ctx.swapchains[i].height}, eye_view, eye_proj, i, has_ray, ray_origin, ray_dir);

			projectionViews[i] = (XrCompositionLayerProjectionView){.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW,
																	.pose = app->xr_ctx.views[i].pose,
																	.fov = app->xr_ctx.views[i].fov,
																	.subImage = {
																		.swapchain = app->xr_ctx.swapchains[i].handle,
																		.imageRect = {{0, 0}, {(int32_t)app->xr_ctx.swapchains[i].width, (int32_t)app->xr_ctx.swapchains[i].height}},
																		.imageArrayIndex = 0,
																	}};
		}

		VK_CHECK(vkEndCommandBuffer(app->renderer.commands.commandBuffers[app->renderer.commands.currentFrame]), "XR: Failed to end command buffer");

		VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &app->renderer.commands.commandBuffers[app->renderer.commands.currentFrame]};
		VK_CHECK(vkResetFences(app->renderer.core.device, 1, &app->renderer.commands.inFlightFences[app->renderer.commands.currentFrame]), "XR: Failed to reset fences");
		vkRes = vkQueueSubmit(app->renderer.core.graphicsQueue, 1, &si, app->renderer.commands.inFlightFences[app->renderer.commands.currentFrame]);
		if (vkRes != VK_SUCCESS) {
			fprintf(stderr, "XR: vkQueueSubmit failed: %d\n", vkRes);
			VK_CHECK(vkResetFences(app->renderer.core.device, 1, &app->renderer.commands.inFlightFences[app->renderer.commands.currentFrame]), "XR: Failed to reset fences after submit failure");
			xr_context_end_frame(&app->xr_ctx, &frameState, NULL, 0);
			app->renderer.commands.currentFrame = (app->renderer.commands.currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
			return;
		}

		for (uint32_t i = 0; i < app->xr_ctx.view_count; i++) {
			XrSwapchainImageReleaseInfo releaseInfo = {.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
			XrResult xrRes = xrReleaseSwapchainImage(app->xr_ctx.swapchains[i].handle, &releaseInfo);
			if (XR_FAILED(xrRes)) {
				fprintf(stderr, "XR: xrReleaseSwapchainImage failed for view %u: %d\n", i, xrRes);
			}
		}

		XrCompositionLayerProjection layer = {
			.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION,
			.space = app->xr_ctx.stage_space,
			.viewCount = app->xr_ctx.view_count,
			.views = projectionViews,
		};
		XrCompositionLayerBaseHeader *layerPtr = (XrCompositionLayerBaseHeader *)&layer;
		if (!xr_context_end_frame(&app->xr_ctx, &frameState, &layerPtr, 1)) {
			fprintf(stderr, "XR: xrEndFrame failed\n");
		}

		app->renderer.commands.currentFrame = (app->renderer.commands.currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
	} else {
		xr_context_end_frame(&app->xr_ctx, &frameState, NULL, 0);
	}
}
