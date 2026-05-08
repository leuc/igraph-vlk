#include "xr/openxr_context.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

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

bool xr_context_init_input(XrContext *ctx)
{
	XrActionSetCreateInfo actionSetInfo = {
		.type = XR_TYPE_ACTION_SET_CREATE_INFO,
		.next = NULL,
		.actionSetName = "main",
		.localizedActionSetName = "Main Actions",
		.priority = 0,
	};
	if (XR_FAILED(xrCreateActionSet(ctx->instance, &actionSetInfo, &ctx->action_set))) {
		fprintf(stderr, "OpenXR Error: Failed to create action set\n");
		return false;
	}

	xrStringToPath(ctx->instance, "/user/hand/left", &ctx->hand_paths[0]);
	xrStringToPath(ctx->instance, "/user/hand/right", &ctx->hand_paths[1]);

	if (XR_FAILED(create_action(ctx->action_set, ctx->hand_paths, &ctx->select_action, "select", "Select", XR_ACTION_TYPE_BOOLEAN_INPUT))) {
		fprintf(stderr, "OpenXR Error: Failed to create select action\n");
		return false;
	}
	if (XR_FAILED(create_action(ctx->action_set, ctx->hand_paths, &ctx->menu_action, "menu", "Menu", XR_ACTION_TYPE_BOOLEAN_INPUT))) {
		fprintf(stderr, "OpenXR Error: Failed to create menu action\n");
		return false;
	}
	if (XR_FAILED(create_action(ctx->action_set, ctx->hand_paths, &ctx->button_a_action, "button_a", "Button A", XR_ACTION_TYPE_BOOLEAN_INPUT))) {
		fprintf(stderr, "OpenXR Error: Failed to create button A action\n");
		return false;
	}
	if (XR_FAILED(create_action(ctx->action_set, ctx->hand_paths, &ctx->button_b_action, "button_b", "Button B", XR_ACTION_TYPE_BOOLEAN_INPUT))) {
		fprintf(stderr, "OpenXR Error: Failed to create button B action\n");
		return false;
	}
	if (XR_FAILED(create_action(ctx->action_set, ctx->hand_paths, &ctx->button_x_action, "button_x", "Button X", XR_ACTION_TYPE_BOOLEAN_INPUT))) {
		fprintf(stderr, "OpenXR Error: Failed to create button X action\n");
		return false;
	}
	if (XR_FAILED(create_action(ctx->action_set, ctx->hand_paths, &ctx->button_y_action, "button_y", "Button Y", XR_ACTION_TYPE_BOOLEAN_INPUT))) {
		fprintf(stderr, "OpenXR Error: Failed to create button Y action\n");
		return false;
	}

	XrActionCreateInfo thumbstickLeftInfo = {
		.type = XR_TYPE_ACTION_CREATE_INFO,
		.actionType = XR_ACTION_TYPE_VECTOR2F_INPUT,
		.countSubactionPaths = 1,
		.subactionPaths = &ctx->hand_paths[0],
	};
	strncpy(thumbstickLeftInfo.actionName, "thumbstick_left", XR_MAX_ACTION_NAME_SIZE - 1);
	strncpy(thumbstickLeftInfo.localizedActionName, "Left Thumbstick", XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
	if (XR_FAILED(xrCreateAction(ctx->action_set, &thumbstickLeftInfo, &ctx->thumbstick_left_action))) {
		fprintf(stderr, "OpenXR Error: Failed to create left thumbstick action\n");
		return false;
	}

	// Right hand thumbstick
	XrActionCreateInfo thumbstickRightInfo = {
		.type = XR_TYPE_ACTION_CREATE_INFO,
		.actionType = XR_ACTION_TYPE_VECTOR2F_INPUT,
		.countSubactionPaths = 1,
		.subactionPaths = &ctx->hand_paths[1],
	};
	strncpy(thumbstickRightInfo.actionName, "thumbstick_right", XR_MAX_ACTION_NAME_SIZE - 1);
	strncpy(thumbstickRightInfo.localizedActionName, "Right Thumbstick", XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
	if (XR_FAILED(xrCreateAction(ctx->action_set, &thumbstickRightInfo, &ctx->thumbstick_right_action))) {
		fprintf(stderr, "OpenXR Error: Failed to create right thumb stick action\n");
		return false;
	}

	// Suggest bindings for KHR Simple Controller
	XrPath simpleProfile;
	xrStringToPath(ctx->instance, "/interaction_profiles/khr/simple_controller", &simpleProfile);

	XrPath selectLeft, selectRight, menuLeft, menuRight;
	xrStringToPath(ctx->instance, "/user/hand/left/input/select/click", &selectLeft);
	xrStringToPath(ctx->instance, "/user/hand/right/input/select/click", &selectRight);
	xrStringToPath(ctx->instance, "/user/hand/left/input/menu/click", &menuLeft);
	xrStringToPath(ctx->instance, "/user/hand/right/input/menu/click", &menuRight);

	XrActionSuggestedBinding simpleBindings[] = {{ctx->select_action, selectLeft}, {ctx->select_action, selectRight}, {ctx->menu_action, menuLeft}, {ctx->menu_action, menuRight}};

	XrInteractionProfileSuggestedBinding simpleProfileBindings = {.type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING, .interactionProfile = simpleProfile, .suggestedBindings = simpleBindings, .countSuggestedBindings = 4};
	xrSuggestInteractionProfileBindings(ctx->instance, &simpleProfileBindings);

	// Suggest bindings for Oculus Touch (broadly compatible)
	XrPath touchProfile;
	xrStringToPath(ctx->instance, "/interaction_profiles/oculus/touch_controller", &touchProfile);

	XrPath aClick, bClick, xClick, yClick;
	xrStringToPath(ctx->instance, "/user/hand/right/input/a/click", &aClick);
	xrStringToPath(ctx->instance, "/user/hand/right/input/b/click", &bClick);
	xrStringToPath(ctx->instance, "/user/hand/left/input/x/click", &xClick);
	xrStringToPath(ctx->instance, "/user/hand/left/input/y/click", &yClick);
	XrPath triggerLeft, triggerRight;
	xrStringToPath(ctx->instance, "/user/hand/left/input/trigger/value", &triggerLeft);
	xrStringToPath(ctx->instance, "/user/hand/right/input/trigger/value", &triggerRight);
	XrPath thumbstickLeft, thumbstickRight;
	xrStringToPath(ctx->instance, "/user/hand/left/input/thumbstick", &thumbstickLeft);
	xrStringToPath(ctx->instance, "/user/hand/right/input/thumbstick", &thumbstickRight);

	XrActionSuggestedBinding touchBindings[] = {{ctx->select_action, triggerLeft}, {ctx->select_action, triggerRight}, {ctx->menu_action, menuLeft}, {ctx->button_a_action, aClick}, {ctx->button_b_action, bClick}, {ctx->button_x_action, xClick}, {ctx->button_y_action, yClick}, {ctx->thumbstick_left_action, thumbstickLeft}, {ctx->thumbstick_right_action, thumbstickRight}};

	XrInteractionProfileSuggestedBinding touchProfileBindings = {.type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING, .interactionProfile = touchProfile, .suggestedBindings = touchBindings, .countSuggestedBindings = 9};
	xrSuggestInteractionProfileBindings(ctx->instance, &touchProfileBindings);

	XrSessionActionSetsAttachInfo attachInfo = {
		.type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO,
		.countActionSets = 1,
		.actionSets = &ctx->action_set,
	};
	if (XR_FAILED(xrAttachSessionActionSets(ctx->session, &attachInfo))) {
		fprintf(stderr, "OpenXR Error: Failed to attach action sets\n");
		return false;
	}

	return true;
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

static void check_button(XrContext *ctx, XrAction action, const char *name)
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

	check_button(ctx, ctx->select_action, "Select");
	check_button(ctx, ctx->menu_action, "Menu");
	check_button(ctx, ctx->button_a_action, "Button A");
	check_button(ctx, ctx->button_b_action, "Button B");
	check_button(ctx, ctx->button_x_action, "Button X");
	check_button(ctx, ctx->button_y_action, "Button Y");
}

float xr_context_get_thumbstick(XrContext *ctx, uint32_t hand_index, uint32_t axis)
{
	if (!ctx->session_running || hand_index >= 2 || axis >= 2)
		return 0.0f;

	// Get thumbstick vector2f state
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

	// Apply deadzone to ignore small drift
	const float deadzone = 0.15f;
	float value = (axis == 0) ? state.currentState.x : state.currentState.y;
	if (fabsf(value) < deadzone)
		return 0.0f;

	return value;
}

void xr_context_print_capabilities(XrContext *ctx)
{
	if (!ctx->session_running) {
		printf("No active session for controller detection.\n");
		return;
	}

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

			// Assumed capabilities from profile
			if (strstr(profileStr, "touch_controller") || strstr(profileStr, "simple_controller")) {
				printf("  Supports: Thumbsticks, Select/Trigger, Menu, A/B/X/Y buttons\n");
			}
		} else {
			printf("  No profile bound.\n");
		}
	}
	printf("==================================\n");
}
