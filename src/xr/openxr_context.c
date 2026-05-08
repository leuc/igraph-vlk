#include "xr/openxr_context.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <math.h>

static void xr_fov_to_matrix(const XrFovf fov, float nearZ, float farZ, mat4 out)
{
	float tanLeft = tanf(fov.angleLeft);
	float tanRight = tanf(fov.angleRight);
	float tanUp = tanf(fov.angleUp);
	float tanDown = tanf(fov.angleDown);
	float tanWidth = tanRight - tanLeft;
	float tanHeight = tanUp - tanDown;

	glm_mat4_zero(out);
	out[0][0] = 2.0f / tanWidth;
	out[1][1] = 2.0f / tanHeight;
	out[2][0] = (tanRight + tanLeft) / tanWidth;
	out[2][1] = (tanUp + tanDown) / tanHeight;
	out[2][2] = -(farZ + nearZ) / (farZ - nearZ);
	out[2][3] = -1.0f;
	out[3][2] = -(2.0f * farZ * nearZ) / (farZ - nearZ);
}

static void xr_pose_to_matrix(const XrPosef pose, mat4 out)
{
	versor q = {pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w};
	vec3 p = {pose.position.x, pose.position.y, pose.position.z};

	// View Matrix = Inverse(Rotation * Translation) = Inverse(Translation) * Inverse(Rotation)
	// T = Translation(p), R = Rotation(q)
	// Inv(TR) = R^T * Trans(-p)
	mat4 rot;
	glm_quat_mat4(q, rot);
	glm_mat4_transpose(rot); // R^T

	mat4 trans;
	glm_mat4_identity(trans);
	vec3 neg_p = {-p[0], -p[1], -p[2]};
	glm_translate(trans, neg_p); // Trans(-p)

	glm_mat4_mul(rot, trans, out); // Out = R^T * T^-1
}

bool xr_context_init(XrContext *ctx, const char *app_name)
{
	memset(ctx, 0, sizeof(XrContext));
	ctx->view_config_type = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;

	const char *extensions[] = {XR_KHR_VULKAN_ENABLE_EXTENSION_NAME};

	XrInstanceCreateInfo createInfo = {
		.type = XR_TYPE_INSTANCE_CREATE_INFO,
		.next = NULL,
		.createFlags = 0,
		.applicationInfo =
			{
				.applicationName = "igraph-vlk",
				.applicationVersion = 1,
				.engineName = "No Engine",
				.engineVersion = 1,
				.apiVersion = XR_CURRENT_API_VERSION,
			},
		.enabledApiLayerCount = 0,
		.enabledApiLayerNames = NULL,
		.enabledExtensionCount = 1,
		.enabledExtensionNames = extensions,
	};
	strncpy(createInfo.applicationInfo.applicationName, app_name, XR_MAX_APPLICATION_NAME_SIZE - 1);

	XrResult res = xrCreateInstance(&createInfo, &ctx->instance);
	if (XR_FAILED(res)) {
		return false;
	}

	XrSystemGetInfo systemInfo = {
		.type = XR_TYPE_SYSTEM_GET_INFO,
		.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY,
	};
	res = xrGetSystem(ctx->instance, &systemInfo, &ctx->system_id);
	if (XR_FAILED(res)) {
		fprintf(stderr, "OpenXR Error: Failed to get system (Result: %d)\n", res);
		return false;
	}

	res = xrEnumerateViewConfigurationViews(ctx->instance, ctx->system_id, ctx->view_config_type, 0, &ctx->view_count, NULL);
	if (XR_FAILED(res)) {
		fprintf(stderr, "OpenXR Error: Failed to count view configs (Result: %d)\n", res);
		return false;
	}

	ctx->view_configs = malloc(sizeof(XrViewConfigurationView) * ctx->view_count);
	for (uint32_t i = 0; i < ctx->view_count; i++) {
		ctx->view_configs[i].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
		ctx->view_configs[i].next = NULL;
	}
	res = xrEnumerateViewConfigurationViews(ctx->instance, ctx->system_id, ctx->view_config_type, ctx->view_count, &ctx->view_count, ctx->view_configs);
	if (XR_FAILED(res)) {
		fprintf(stderr, "OpenXR Error: Failed to enumerate view configs (Result: %d)\n", res);
		return false;
	}

	// Re-verify view count
	if (ctx->view_count == 0)
		return false;

	ctx->views = malloc(sizeof(XrView) * ctx->view_count);
	for (uint32_t i = 0; i < ctx->view_count; i++) {
		ctx->views[i].type = XR_TYPE_VIEW;
		ctx->views[i].next = NULL;
	}

	return true;
}

void xr_context_cleanup(XrContext *ctx)
{
	if (ctx->action_set != XR_NULL_HANDLE) {
		xrDestroyActionSet(ctx->action_set);
	}
	if (ctx->instance != XR_NULL_HANDLE) {
		xrDestroyInstance(ctx->instance);
	}
	if (ctx->view_configs)
		free(ctx->view_configs);
	if (ctx->views)
		free(ctx->views);
	if (ctx->swapchains) {
		for (uint32_t i = 0; i < ctx->view_count; i++) {
			if (ctx->swapchains[i].handle != XR_NULL_HANDLE) {
				xrDestroySwapchain(ctx->swapchains[i].handle);
			}
			if (ctx->swapchains[i].images)
				free(ctx->swapchains[i].images);
			if (ctx->swapchains[i].image_views)
				free(ctx->swapchains[i].image_views);
		}
		free(ctx->swapchains);
	}
}

void xr_context_poll_events(XrContext *ctx)
{
	XrEventDataBuffer event = {.type = XR_TYPE_EVENT_DATA_BUFFER};
	while (xrPollEvent(ctx->instance, &event) == XR_SUCCESS) {
		switch (event.type) {
		case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
			XrEventDataSessionStateChanged *changed = (XrEventDataSessionStateChanged *)&event;
			ctx->state = changed->state;
			if (ctx->state == XR_SESSION_STATE_READY) {
				XrSessionBeginInfo beginInfo = {
					.type = XR_TYPE_SESSION_BEGIN_INFO,
					.primaryViewConfigurationType = ctx->view_config_type,
				};
				xrBeginSession(ctx->session, &beginInfo);
				ctx->session_running = true;
			} else if (ctx->state == XR_SESSION_STATE_STOPPING) {
				xrEndSession(ctx->session);
				ctx->session_running = false;
			}
			break;
		}
		default:
			break;
		}
		event.type = XR_TYPE_EVENT_DATA_BUFFER;
	}
}

bool xr_context_create_session(XrContext *ctx, VkInstance instance, VkPhysicalDevice physical_device, VkDevice device, uint32_t queue_family_index, uint32_t queue_index)
{
	PFN_xrGetVulkanGraphicsRequirementsKHR xrGetVulkanGraphicsRequirementsKHR;
	xrGetInstanceProcAddr(ctx->instance, "xrGetVulkanGraphicsRequirementsKHR", (PFN_xrVoidFunction *)&xrGetVulkanGraphicsRequirementsKHR);
	XrGraphicsRequirementsVulkanKHR graphicsRequirements = {.type = XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR};
	xrGetVulkanGraphicsRequirementsKHR(ctx->instance, ctx->system_id, &graphicsRequirements);

	XrGraphicsBindingVulkanKHR graphicsBinding = {
		.type = XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR,
		.instance = instance,
		.physicalDevice = physical_device,
		.device = device,
		.queueFamilyIndex = queue_family_index,
		.queueIndex = queue_index,
	};

	XrSessionCreateInfo sessionCreateInfo = {
		.type = XR_TYPE_SESSION_CREATE_INFO,
		.next = &graphicsBinding,
		.systemId = ctx->system_id,
	};

	XrResult res = xrCreateSession(ctx->instance, &sessionCreateInfo, &ctx->session);
	if (XR_FAILED(res)) {
		fprintf(stderr, "OpenXR Error: Failed to create session (Result: %d)\n", res);
		return false;
	}

	XrReferenceSpaceCreateInfo spaceCreateInfo = {
		.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO,
		.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE,
		.poseInReferenceSpace = {{0, 0, 0, 1}, {0, 0, 0}},
	};
	res = xrCreateReferenceSpace(ctx->session, &spaceCreateInfo, &ctx->stage_space);
	if (XR_FAILED(res)) {
		fprintf(stderr, "OpenXR Error: Failed to create reference space (Result: %d)\n", res);
		return false;
	}

	uint32_t formatCount = 0;
	res = xrEnumerateSwapchainFormats(ctx->session, 0, &formatCount, NULL);
	if (XR_FAILED(res)) {
		fprintf(stderr, "OpenXR Error: Failed to count swapchain formats (Result: %d)\n", res);
		return false;
	}

	VkFormat *supportedFormats = malloc(sizeof(VkFormat) * formatCount);
	res = xrEnumerateSwapchainFormats(ctx->session, formatCount, &formatCount, (int64_t *)supportedFormats);
	if (XR_FAILED(res)) {
		fprintf(stderr, "OpenXR Error: Failed to enumerate swapchain formats (Result: %d)\n", res);
		free(supportedFormats);
		return false;
	}

	// Preferred formats in order
	VkFormat preferredFormats[] = {VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM};

	ctx->swapchain_format = VK_FORMAT_UNDEFINED;
	for (size_t p = 0; p < sizeof(preferredFormats) / sizeof(preferredFormats[0]); p++) {
		for (uint32_t s = 0; s < formatCount; s++) {
			if (supportedFormats[s] == preferredFormats[p]) {
				ctx->swapchain_format = preferredFormats[p];
				break;
			}
		}
		if (ctx->swapchain_format != VK_FORMAT_UNDEFINED)
			break;
	}

	if (ctx->swapchain_format == VK_FORMAT_UNDEFINED) {
		fprintf(stderr, "Warning: No preferred swapchain format found, falling back to VK_FORMAT_B8G8R8A8_SRGB\n");
		ctx->swapchain_format = VK_FORMAT_B8G8R8A8_SRGB;
	}
	free(supportedFormats);

	// Create swapchains
	ctx->swapchains = malloc(sizeof(*ctx->swapchains) * ctx->view_count);
	for (uint32_t i = 0; i < ctx->view_count; i++) {
		printf("[OpenXR] View %u: %ux%u (recommended)\n", i, ctx->view_configs[i].recommendedImageRectWidth, ctx->view_configs[i].recommendedImageRectHeight);

		XrSwapchainCreateInfo swapchainCreateInfo = {
			.type = XR_TYPE_SWAPCHAIN_CREATE_INFO,
			.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT,
			.format = ctx->swapchain_format,
			.sampleCount = ctx->view_configs[i].recommendedSwapchainSampleCount,
			.width = ctx->view_configs[i].recommendedImageRectWidth,
			.height = ctx->view_configs[i].recommendedImageRectHeight,
			.faceCount = 1,
			.arraySize = 1,
			.mipCount = 1,
		};
		ctx->swapchains[i].width = swapchainCreateInfo.width;
		ctx->swapchains[i].height = swapchainCreateInfo.height;

		res = xrCreateSwapchain(ctx->session, &swapchainCreateInfo, &ctx->swapchains[i].handle);
		if (XR_FAILED(res)) {
			fprintf(stderr, "OpenXR Error: Failed to create swapchain (Result: %d)\n", res);
			return false;
		}

		res = xrEnumerateSwapchainImages(ctx->swapchains[i].handle, 0, &ctx->swapchains[i].image_count, NULL);
		if (XR_FAILED(res)) {
			fprintf(stderr, "OpenXR Error: Failed to count swapchain images (Result: %d)\n", res);
			return false;
		}

		XrSwapchainImageVulkanKHR *images = malloc(sizeof(XrSwapchainImageVulkanKHR) * ctx->swapchains[i].image_count);
		for (uint32_t j = 0; j < ctx->swapchains[i].image_count; j++) {
			images[j].type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR;
			images[j].next = NULL;
		}
		res = xrEnumerateSwapchainImages(ctx->swapchains[i].handle, ctx->swapchains[i].image_count, &ctx->swapchains[i].image_count, (XrSwapchainImageBaseHeader *)images);
		if (XR_FAILED(res)) {
			fprintf(stderr, "OpenXR Error: Failed to enumerate swapchain images (Result: %d)\n", res);
			free(images);
			return false;
		}

		ctx->swapchains[i].images = malloc(sizeof(VkImage) * ctx->swapchains[i].image_count);
		for (uint32_t j = 0; j < ctx->swapchains[i].image_count; j++) {
			ctx->swapchains[i].images[j] = images[j].image;
		}
		free(images);
	}

	return true;
}

bool xr_context_get_vulkan_instance_extensions(XrContext *ctx, char *out_exts, uint32_t *out_size)
{
	PFN_xrGetVulkanInstanceExtensionsKHR xrGetVulkanInstanceExtensionsKHR;
	xrGetInstanceProcAddr(ctx->instance, "xrGetVulkanInstanceExtensionsKHR", (PFN_xrVoidFunction *)&xrGetVulkanInstanceExtensionsKHR);
	XrResult res = xrGetVulkanInstanceExtensionsKHR(ctx->instance, ctx->system_id, *out_size, out_size, out_exts);
	return XR_SUCCEEDED(res);
}

bool xr_context_get_vulkan_device_extensions(XrContext *ctx, char *out_exts, uint32_t *out_size)
{
	PFN_xrGetVulkanDeviceExtensionsKHR xrGetVulkanDeviceExtensionsKHR;
	xrGetInstanceProcAddr(ctx->instance, "xrGetVulkanDeviceExtensionsKHR", (PFN_xrVoidFunction *)&xrGetVulkanDeviceExtensionsKHR);
	XrResult res = xrGetVulkanDeviceExtensionsKHR(ctx->instance, ctx->system_id, *out_size, out_size, out_exts);
	return XR_SUCCEEDED(res);
}

VkPhysicalDevice xr_context_get_vulkan_graphics_device(XrContext *ctx, VkInstance instance)
{
	PFN_xrGetVulkanGraphicsDeviceKHR xrGetVulkanGraphicsDeviceKHR;
	xrGetInstanceProcAddr(ctx->instance, "xrGetVulkanGraphicsDeviceKHR", (PFN_xrVoidFunction *)&xrGetVulkanGraphicsDeviceKHR);
	VkPhysicalDevice pd;
	xrGetVulkanGraphicsDeviceKHR(ctx->instance, ctx->system_id, instance, &pd);
	return pd;
}

bool xr_context_begin_frame(XrContext *ctx, XrFrameState *frame_state)
{
	XrFrameWaitInfo waitInfo = {.type = XR_TYPE_FRAME_WAIT_INFO};
	XrResult res = xrWaitFrame(ctx->session, &waitInfo, frame_state);
	if (XR_FAILED(res)) {
		fprintf(stderr, "OpenXR Error: Failed to wait frame (Result: %d)\n", res);
		return false;
	}

	XrFrameBeginInfo beginInfo = {.type = XR_TYPE_FRAME_BEGIN_INFO};
	res = xrBeginFrame(ctx->session, &beginInfo);
	if (XR_FAILED(res)) {
		fprintf(stderr, "OpenXR Error: Failed to begin frame (Result: %d)\n", res);
		return false;
	}

	XrViewState viewState = {.type = XR_TYPE_VIEW_STATE};
	XrViewLocateInfo locateInfo = {
		.type = XR_TYPE_VIEW_LOCATE_INFO,
		.viewConfigurationType = ctx->view_config_type,
		.displayTime = frame_state->predictedDisplayTime,
		.space = ctx->stage_space,
	};
	res = xrLocateViews(ctx->session, &locateInfo, &viewState, ctx->view_count, &ctx->view_count, ctx->views);
	if (XR_FAILED(res)) {
		fprintf(stderr, "OpenXR Error: Failed to locate views (Result: %d)\n", res);
		return false;
	}

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
	if (XR_FAILED(res)) {
		fprintf(stderr, "OpenXR Error: Failed to end frame (Result: %d)\n", res);
		return false;
	}
	return true;
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
