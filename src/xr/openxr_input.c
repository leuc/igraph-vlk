#include "xr/openxr_context.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static XrResult create_action(XrActionSet action_set, XrPath *subaction_paths, XrAction *action, const char *name, const char *localized_name, XrActionType type)
{
	XrActionCreateInfo actionInfo = {
		.type = XR_TYPE_ACTION_CREATE_INFO,
		.actionType = type,
		.countSubactionPaths = 2,
		.subactionPaths = subaction_paths,
	};
	strncpy(actionInfo.actionName, name, XR_MAX_ACTION_NAME_SIZE - 1);
	strncpy(actionInfo.localizedActionName, localized_name, XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
	return xrCreateAction(action_set, &actionInfo, action);
}

static bool suggest_bindings(XrInstance instance, const char *profile_path, XrActionSuggestedBinding *bindings, uint32_t count)
{
	XrPath profile;
	if (XR_FAILED(xrStringToPath(instance, profile_path, &profile)))
		return false;

	XrInteractionProfileSuggestedBinding suggested = {
		.type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING,
		.interactionProfile = profile,
		.suggestedBindings = bindings,
		.countSuggestedBindings = count,
	};
	XrResult res = xrSuggestInteractionProfileBindings(instance, &suggested);
	if (XR_FAILED(res)) {
		if (res != XR_ERROR_PATH_UNSUPPORTED) {
			fprintf(stderr, "[OpenXR] Warning: Failed to suggest bindings for %s (Result: %d)\n", profile_path, res);
		}
		return false;
	}
	return true;
}

bool xr_context_init_input(XrContext *ctx)
{
	XrActionSetCreateInfo actionSetInfo = {
		.type = XR_TYPE_ACTION_SET_CREATE_INFO,
		.next = NULL,
		.actionSetName = "main",
		.localizedActionSetName = "Main Actions",
		.priority = 0,
	};
	XrResult res = xrCreateActionSet(ctx->instance, &actionSetInfo, &ctx->action_set);
	XR_CHECK_GOTO(res, "Failed to create action set", cleanup);

	res = xrStringToPath(ctx->instance, "/user/hand/left", &ctx->hand_paths[0]);
	XR_CHECK_GOTO(res, "Failed to create hand path left", cleanup);
	res = xrStringToPath(ctx->instance, "/user/hand/right", &ctx->hand_paths[1]);
	XR_CHECK_GOTO(res, "Failed to create hand path right", cleanup);

	XR_CHECK_GOTO(create_action(ctx->action_set, ctx->hand_paths, &ctx->select_action, "select", "Select", XR_ACTION_TYPE_BOOLEAN_INPUT), "Failed to create select action", cleanup);
	XR_CHECK_GOTO(create_action(ctx->action_set, ctx->hand_paths, &ctx->menu_action, "menu", "Menu", XR_ACTION_TYPE_BOOLEAN_INPUT), "Failed to create menu action", cleanup);
	XR_CHECK_GOTO(create_action(ctx->action_set, ctx->hand_paths, &ctx->button_a_action, "button_a", "Button A", XR_ACTION_TYPE_BOOLEAN_INPUT), "Failed to create button A action", cleanup);
	XR_CHECK_GOTO(create_action(ctx->action_set, ctx->hand_paths, &ctx->button_b_action, "button_b", "Button B", XR_ACTION_TYPE_BOOLEAN_INPUT), "Failed to create button B action", cleanup);
	XR_CHECK_GOTO(create_action(ctx->action_set, ctx->hand_paths, &ctx->button_x_action, "button_x", "Button X", XR_ACTION_TYPE_BOOLEAN_INPUT), "Failed to create button X action", cleanup);
	XR_CHECK_GOTO(create_action(ctx->action_set, ctx->hand_paths, &ctx->button_y_action, "button_y", "Button Y", XR_ACTION_TYPE_BOOLEAN_INPUT), "Failed to create button Y action", cleanup);

	XrActionCreateInfo thumbstickLeftInfo = {
		.type = XR_TYPE_ACTION_CREATE_INFO,
		.actionType = XR_ACTION_TYPE_VECTOR2F_INPUT,
		.countSubactionPaths = 1,
		.subactionPaths = &ctx->hand_paths[0],
	};
	strncpy(thumbstickLeftInfo.actionName, "thumbstick_left", XR_MAX_ACTION_NAME_SIZE - 1);
	strncpy(thumbstickLeftInfo.localizedActionName, "Left Thumbstick", XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
	res = xrCreateAction(ctx->action_set, &thumbstickLeftInfo, &ctx->thumbstick_left_action);
	XR_CHECK_GOTO(res, "Failed to create left thumbstick action", cleanup);

	XrActionCreateInfo thumbstickRightInfo = {
		.type = XR_TYPE_ACTION_CREATE_INFO,
		.actionType = XR_ACTION_TYPE_VECTOR2F_INPUT,
		.countSubactionPaths = 1,
		.subactionPaths = &ctx->hand_paths[1],
	};
	strncpy(thumbstickRightInfo.actionName, "thumbstick_right", XR_MAX_ACTION_NAME_SIZE - 1);
	strncpy(thumbstickRightInfo.localizedActionName, "Right Thumbstick", XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
	res = xrCreateAction(ctx->action_set, &thumbstickRightInfo, &ctx->thumbstick_right_action);
	XR_CHECK_GOTO(res, "Failed to create right thumb stick action", cleanup);

	XrActionCreateInfo rightHandPoseInfo = {
		.type = XR_TYPE_ACTION_CREATE_INFO,
		.actionType = XR_ACTION_TYPE_POSE_INPUT,
		.countSubactionPaths = 1,
		.subactionPaths = &ctx->hand_paths[1],
	};
	strncpy(rightHandPoseInfo.actionName, "right_hand_pose", XR_MAX_ACTION_NAME_SIZE - 1);
	strncpy(rightHandPoseInfo.localizedActionName, "Right Hand Pose", XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
	res = xrCreateAction(ctx->action_set, &rightHandPoseInfo, &ctx->right_hand_pose_action);
	XR_CHECK_GOTO(res, "Failed to create right hand pose action", cleanup);

	XrActionSpaceCreateInfo spaceInfo = {
		.type = XR_TYPE_ACTION_SPACE_CREATE_INFO,
		.action = ctx->right_hand_pose_action,
		.poseInActionSpace = {{0, 0, 0, 1}, {0, 0, 0}},
	};
	res = xrCreateActionSpace(ctx->session, &spaceInfo, &ctx->right_hand_space);
	XR_CHECK_GOTO(res, "Failed to create right hand action space", cleanup);

	XrPath selectLeft, selectRight, menuLeft, menuRight, thumbstickLeft, thumbstickRight, rightHandPose;
	res = xrStringToPath(ctx->instance, "/user/hand/left/input/select/click", &selectLeft);
	XR_CHECK_GOTO(res, "Failed to get select/left path", cleanup);
	res = xrStringToPath(ctx->instance, "/user/hand/right/input/select/click", &selectRight);
	XR_CHECK_GOTO(res, "Failed to get select/right path", cleanup);
	res = xrStringToPath(ctx->instance, "/user/hand/left/input/menu/click", &menuLeft);
	XR_CHECK_GOTO(res, "Failed to get menu/left path", cleanup);
	res = xrStringToPath(ctx->instance, "/user/hand/right/input/menu/click", &menuRight);
	XR_CHECK_GOTO(res, "Failed to get menu/right path", cleanup);
	res = xrStringToPath(ctx->instance, "/user/hand/left/input/thumbstick", &thumbstickLeft);
	XR_CHECK_GOTO(res, "Failed to get thumbstick/left path", cleanup);
	res = xrStringToPath(ctx->instance, "/user/hand/right/input/thumbstick", &thumbstickRight);
	XR_CHECK_GOTO(res, "Failed to get thumbstick/right path", cleanup);
	res = xrStringToPath(ctx->instance, "/user/hand/right/input/aim/pose", &rightHandPose);
	XR_CHECK_GOTO(res, "Failed to get aim/pose path", cleanup);

	XrPath aClick, bClick, xClick, yClick, triggerLeft, triggerRight, squeezeLeft, squeezeRight, gripLeft, gripRight;
	res = xrStringToPath(ctx->instance, "/user/hand/right/input/a/click", &aClick);
	XR_CHECK_GOTO(res, "Failed to get a/click path", cleanup);
	res = xrStringToPath(ctx->instance, "/user/hand/right/input/b/click", &bClick);
	XR_CHECK_GOTO(res, "Failed to get b/click path", cleanup);
	res = xrStringToPath(ctx->instance, "/user/hand/left/input/x/click", &xClick);
	XR_CHECK_GOTO(res, "Failed to get x/click path", cleanup);
	res = xrStringToPath(ctx->instance, "/user/hand/left/input/y/click", &yClick);
	XR_CHECK_GOTO(res, "Failed to get y/click path", cleanup);
	res = xrStringToPath(ctx->instance, "/user/hand/left/input/trigger/value", &triggerLeft);
	XR_CHECK_GOTO(res, "Failed to get trigger/left path", cleanup);
	res = xrStringToPath(ctx->instance, "/user/hand/right/input/trigger/value", &triggerRight);
	XR_CHECK_GOTO(res, "Failed to get trigger/right path", cleanup);
	res = xrStringToPath(ctx->instance, "/user/hand/left/input/squeeze/value", &squeezeLeft);
	XR_CHECK_GOTO(res, "Failed to get squeeze/left path", cleanup);
	res = xrStringToPath(ctx->instance, "/user/hand/right/input/squeeze/value", &squeezeRight);
	XR_CHECK_GOTO(res, "Failed to get squeeze/right path", cleanup);
	res = xrStringToPath(ctx->instance, "/user/hand/left/input/grip/click", &gripLeft);
	XR_CHECK_GOTO(res, "Failed to get grip/left path", cleanup);
	res = xrStringToPath(ctx->instance, "/user/hand/right/input/grip/click", &gripRight);
	XR_CHECK_GOTO(res, "Failed to get grip/right path", cleanup);

	printf("=== OpenXR Interaction Profiles ===\n");

	XrActionSuggestedBinding simpleBindings[] = {
		{ctx->select_action, selectLeft},
		{ctx->select_action, selectRight},
		{ctx->menu_action, menuLeft},
		{ctx->menu_action, menuRight},
	};
	if (suggest_bindings(ctx->instance, "/interaction_profiles/khr/simple_controller", simpleBindings, 4))
		printf("  [X] KHR Simple Controller\n");

	XrActionSuggestedBinding touchBindings[] = {
		{ctx->select_action, triggerLeft}, {ctx->select_action, triggerRight}, {ctx->menu_action, menuLeft}, {ctx->button_a_action, aClick}, {ctx->button_b_action, bClick}, {ctx->button_x_action, xClick}, {ctx->button_y_action, yClick}, {ctx->thumbstick_left_action, thumbstickLeft}, {ctx->thumbstick_right_action, thumbstickRight}, {ctx->right_hand_pose_action, rightHandPose},
	};
	if (suggest_bindings(ctx->instance, "/interaction_profiles/oculus/touch_controller", touchBindings, 10))
		printf("  [X] Oculus Touch Controller\n");

	XrActionSuggestedBinding indexBindings[] = {
		{ctx->select_action, triggerLeft}, {ctx->select_action, triggerRight}, {ctx->button_a_action, aClick}, {ctx->button_b_action, bClick}, {ctx->thumbstick_left_action, thumbstickLeft}, {ctx->thumbstick_right_action, thumbstickRight},
	};
	if (suggest_bindings(ctx->instance, "/interaction_profiles/valve/index_controller", indexBindings, 6))
		printf("  [X] Valve Index Controller\n");

	XrActionSuggestedBinding viveBindings[] = {
		{ctx->select_action, triggerLeft}, {ctx->select_action, triggerRight}, {ctx->menu_action, menuLeft}, {ctx->menu_action, menuRight}, {ctx->button_a_action, gripLeft}, {ctx->button_b_action, gripRight},
	};
	if (suggest_bindings(ctx->instance, "/interaction_profiles/htc/vive_controller", viveBindings, 6))
		printf("  [X] HTC Vive Controller\n");

	XrActionSuggestedBinding mrBindings[] = {
		{ctx->select_action, triggerLeft}, {ctx->select_action, triggerRight}, {ctx->menu_action, menuLeft}, {ctx->thumbstick_left_action, thumbstickLeft}, {ctx->thumbstick_right_action, thumbstickRight},
	};
	if (suggest_bindings(ctx->instance, "/interaction_profiles/microsoft/motion_controller", mrBindings, 5))
		printf("  [X] Microsoft Motion Controller\n");

	XrPath squareClick, triangleClick, circleClick, crossClick;
	res = xrStringToPath(ctx->instance, "/user/hand/left/input/square/click", &squareClick);
	XR_CHECK_GOTO(res, "Failed to get square/click path", cleanup);
	res = xrStringToPath(ctx->instance, "/user/hand/left/input/triangle/click", &triangleClick);
	XR_CHECK_GOTO(res, "Failed to get triangle/click path", cleanup);
	res = xrStringToPath(ctx->instance, "/user/hand/right/input/circle/click", &circleClick);
	XR_CHECK_GOTO(res, "Failed to get circle/click path", cleanup);
	res = xrStringToPath(ctx->instance, "/user/hand/right/input/cross/click", &crossClick);
	XR_CHECK_GOTO(res, "Failed to get cross/click path", cleanup);

	XrActionSuggestedBinding psvr2Bindings[] = {
		{ctx->select_action, triggerLeft}, {ctx->select_action, triggerRight}, {ctx->menu_action, menuLeft}, {ctx->button_x_action, squareClick}, {ctx->button_y_action, triangleClick}, {ctx->button_a_action, crossClick}, {ctx->button_b_action, circleClick}, {ctx->thumbstick_left_action, thumbstickLeft}, {ctx->thumbstick_right_action, thumbstickRight},
	};
	if (suggest_bindings(ctx->instance, "/interaction_profiles/sony/psvr2_controller", psvr2Bindings, 9))
		printf("  [X] PS VR2 Controller\n");

	printf("===================================\n");

	XrSessionActionSetsAttachInfo attachInfo = {
		.type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO,
		.countActionSets = 1,
		.actionSets = &ctx->action_set,
	};
	res = xrAttachSessionActionSets(ctx->session, &attachInfo);
	XR_CHECK_GOTO(res, "Failed to attach action sets", cleanup);

	return true;

cleanup:
	if (ctx->right_hand_space != XR_NULL_HANDLE) {
		xrDestroySpace(ctx->right_hand_space);
		ctx->right_hand_space = XR_NULL_HANDLE;
	}
	if (ctx->action_set != XR_NULL_HANDLE) {
		xrDestroyActionSet(ctx->action_set);
		ctx->action_set = XR_NULL_HANDLE;
	}
	return false;
}

bool xr_context_is_action_pressed(XrContext *ctx, XrAction action, uint32_t hand_index)
{
	if (!ctx->session_running || hand_index >= 2)
		return false;
	XrActionStateGetInfo getInfo = {
		.type = XR_TYPE_ACTION_STATE_GET_INFO,
		.action = action,
		.subactionPath = ctx->hand_paths[hand_index],
	};
	XrActionStateBoolean state = {.type = XR_TYPE_ACTION_STATE_BOOLEAN};
	xrGetActionStateBoolean(ctx->session, &getInfo, &state);
	return state.isActive && state.changedSinceLastSync && state.currentState;
}

static void xr_check_button(XrContext *ctx, XrAction action, const char *name)
{
	for (int i = 0; i < 2; i++) {
		if (xr_context_is_action_pressed(ctx, action, i)) {
			printf("VR Button Pressed: %s (%s hand)\n", name, i == 0 ? "Left" : "Right");
		}
	}
}

void xr_context_sync_input(XrContext *ctx)
{
	if (!ctx->session_running)
		return;

	XrActiveActionSet activeActionSet = {
		.actionSet = ctx->action_set,
		.subactionPath = XR_NULL_PATH,
	};

	XrActionsSyncInfo syncInfo = {
		.type = XR_TYPE_ACTIONS_SYNC_INFO,
		.countActiveActionSets = 1,
		.activeActionSets = &activeActionSet,
	};

	xrSyncActions(ctx->session, &syncInfo);

	if (!ctx->printed_capabilities) {
		for (int i = 0; i < 2; i++) {
			XrInteractionProfileState state = {.type = XR_TYPE_INTERACTION_PROFILE_STATE};
			if (XR_SUCCEEDED(xrGetCurrentInteractionProfile(ctx->session, ctx->hand_paths[i], &state)) && state.interactionProfile != XR_NULL_PATH) {
				xr_context_print_capabilities(ctx);
				ctx->printed_capabilities = true;
				break;
			}
		}
	}

	xr_check_button(ctx, ctx->select_action, "Select");
	xr_check_button(ctx, ctx->menu_action, "Menu");
	xr_check_button(ctx, ctx->button_a_action, "Button A");
	xr_check_button(ctx, ctx->button_b_action, "Button B");
	xr_check_button(ctx, ctx->button_x_action, "Button X");
	xr_check_button(ctx, ctx->button_y_action, "Button Y");
}

float xr_context_get_thumbstick(XrContext *ctx, uint32_t hand_index, uint32_t axis)
{
	if (!ctx->session_running || hand_index >= 2 || axis >= 2)
		return 0.0f;

	XrAction action = (hand_index == 0) ? ctx->thumbstick_left_action : ctx->thumbstick_right_action;
	if (action == XR_NULL_HANDLE)
		return 0.0f;

	XrActionStateGetInfo getInfo = {
		.type = XR_TYPE_ACTION_STATE_GET_INFO,
		.action = action,
		.subactionPath = ctx->hand_paths[hand_index],
	};

	XrActionStateVector2f state = {
		.type = XR_TYPE_ACTION_STATE_VECTOR2F,
	};

	XrResult res = xrGetActionStateVector2f(ctx->session, &getInfo, &state);
	if (XR_FAILED(res) || !state.isActive)
		return 0.0f;

	const float deadzone = 0.15f;
	float value = (axis == 0) ? state.currentState.x : state.currentState.y;
	if (fabsf(value) < deadzone)
		return 0.0f;

	return value;
}

bool xr_context_get_hand_pose(XrContext *ctx, uint32_t hand_index, XrTime time, XrPosef *out_pose)
{
	if (!ctx->session_running || hand_index != 1 || time == 0)
		return false;

	XrSpaceLocation location = {.type = XR_TYPE_SPACE_LOCATION};
	XrResult res = xrLocateSpace(ctx->right_hand_space, ctx->stage_space, time, &location);

	if (XR_SUCCEEDED(res) && (location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) && (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) {
		*out_pose = location.pose;
		return true;
	}
	return false;
}

void xr_context_print_capabilities(XrContext *ctx)
{
	printf("=== VR Controller Capabilities ===\n");

	for (int hand = 0; hand < 2; hand++) {
		printf("Hand %d (%s):\n", hand, hand == 0 ? "Left" : "Right");

		XrInteractionProfileState state = {.type = XR_TYPE_INTERACTION_PROFILE_STATE};
		XrResult res = xrGetCurrentInteractionProfile(ctx->session, ctx->hand_paths[hand], &state);
		if (XR_SUCCEEDED(res) && state.interactionProfile != XR_NULL_PATH) {
			uint32_t strLen = 0;
			xrPathToString(ctx->instance, state.interactionProfile, 0, &strLen, NULL);
			char profileStr[strLen + 1];
			xrPathToString(ctx->instance, state.interactionProfile, strLen + 1, &strLen, profileStr);
			printf("  Profile: %s\n", profileStr);

			printf("  Supports: ");
			bool first = true;
			if (strstr(profileStr, "touch_controller") || strstr(profileStr, "index_controller") || strstr(profileStr, "psvr2_controller") || strstr(profileStr, "motion_controller") || strstr(profileStr, "vive_cosmos_controller")) {
				printf("Thumbsticks");
				first = false;
			}
			if (strstr(profileStr, "vive_controller") || strstr(profileStr, "psvr2_controller") || strstr(profileStr, "index_controller") || strstr(profileStr, "touch_controller") || strstr(profileStr, "simple_controller") || strstr(profileStr, "vive_cosmos_controller")) {
				printf("%sSelect/Trigger", first ? "" : ", ");
				first = false;
			}
			if (strstr(profileStr, "vive_controller") || strstr(profileStr, "psvr2_controller") || strstr(profileStr, "index_controller") || strstr(profileStr, "touch_controller") || strstr(profileStr, "simple_controller") || strstr(profileStr, "vive_cosmos_controller")) {
				printf("%sMenu", first ? "" : ", ");
				first = false;
			}
			if (strstr(profileStr, "psvr2_controller") || strstr(profileStr, "index_controller") || strstr(profileStr, "touch_controller")) {
				printf("%sA/B/X/Y (or Shape) buttons", first ? "" : ", ");
			}
			printf("\n");
		} else {
			printf("  No profile bound or controller disconnected.\n");
		}
	}
	printf("==================================\n");
}
