/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "xr/openxr_context.h"
#include <math.h>
#include <stdio.h>

static void xr_fov_to_matrix(const XrFovf fov, float nearZ, float farZ, mat4 out)
{
	float tanLeft = tanf(fov.angleLeft);
	float tanRight = tanf(fov.angleRight);
	float tanUp = tanf(fov.angleUp);
	float tanDown = tanf(fov.angleDown);
	float tanWidth = tanRight - tanLeft;
	float tanHeight = tanUp - tanDown;

	if (tanWidth < 1e-6f)
		tanWidth = 1e-6f;
	if (tanHeight < 1e-6f)
		tanHeight = 1e-6f;

	float nearFarExtent = farZ - nearZ;
	if (nearFarExtent < 1e-6f)
		nearFarExtent = 1e-6f;

	glm_mat4_zero(out);
	out[0][0] = 2.0f / tanWidth;
	out[1][1] = 2.0f / tanHeight;
	out[2][0] = (tanRight + tanLeft) / tanWidth;
	out[2][1] = (tanUp + tanDown) / tanHeight;
	out[2][2] = -(farZ + nearZ) / nearFarExtent;
	out[2][3] = -1.0f;
	out[3][2] = -(2.0f * farZ * nearZ) / nearFarExtent;
}

bool xr_context_wait_frame(XrContext *ctx, XrFrameState *frame_state)
{
	XrFrameWaitInfo waitInfo = {.type = XR_TYPE_FRAME_WAIT_INFO};
	XrResult res = xrWaitFrame(ctx->session, &waitInfo, frame_state);
	XR_CHECK(res, "Failed to wait frame");
	return true;
}

bool xr_context_begin_frame(XrContext *ctx)
{
	XrFrameBeginInfo beginInfo = {.type = XR_TYPE_FRAME_BEGIN_INFO};
	XrResult res = xrBeginFrame(ctx->session, &beginInfo);
	XR_CHECK(res, "Failed to begin frame");
	return true;
}

bool xr_context_locate_views(XrContext *ctx, XrTime predictedDisplayTime)
{
	XrViewState viewState = {.type = XR_TYPE_VIEW_STATE};
	XrViewLocateInfo locateInfo = {
		.type = XR_TYPE_VIEW_LOCATE_INFO,
		.viewConfigurationType = ctx->view_config_type,
		.displayTime = predictedDisplayTime,
		.space = ctx->stage_space,
	};
	XrResult res = xrLocateViews(ctx->session, &locateInfo, &viewState, ctx->view_count, &ctx->view_count, ctx->views);
	XR_CHECK(res, "Failed to locate views");
	return true;
}

void xr_context_get_view_matrix(XrContext *ctx, uint32_t view_index, vec3 camera_pos, float camera_yaw, mat4 out)
{
	XrPosef pose = ctx->views[view_index].pose;
	versor q = {pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w};

	// 1. Inverse Headset Rotation (R_hmd^-1)
	mat4 rot_hmd;
	glm_quat_mat4(q, rot_hmd);
	glm_mat4_transpose(rot_hmd);

	// 2. Inverse Virtual Body Yaw (R_yaw^-1)
	mat4 rot_yaw;
	glm_mat4_identity(rot_yaw);
	glm_rotate(rot_yaw, -camera_yaw, (vec3){0.0f, 1.0f, 0.0f});

	// 3. Inverse Translation (T_offset^-1)
	mat4 trans;
	glm_mat4_identity(trans);
	vec3 neg_p = {-camera_pos[0], -camera_pos[1], -camera_pos[2]};
	glm_translate(trans, neg_p);

	// Combine: Out = rot_hmd * rot_yaw * trans
	mat4 temp;
	glm_mat4_mul(rot_yaw, trans, temp);
	glm_mat4_mul(rot_hmd, temp, out);
}

void xr_context_get_projection_matrix(XrContext *ctx, uint32_t view_index, float nearZ, float farZ, mat4 out)
{
	xr_fov_to_matrix(ctx->views[view_index].fov, nearZ, farZ, out);
}

bool xr_context_end_frame(XrContext *ctx, XrFrameState *frame_state, XrCompositionLayerBaseHeader **layers, uint32_t layer_count)
{
	XrFrameEndInfo endInfo = {
		.type = XR_TYPE_FRAME_END_INFO,
		.displayTime = frame_state->predictedDisplayTime,
		.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE,
		.layerCount = layer_count,
		.layers = (const XrCompositionLayerBaseHeader *const *)layers,
	};
	XrResult res = xrEndFrame(ctx->session, &endInfo);
	XR_CHECK(res, "Failed to end frame");
	return true;
}
