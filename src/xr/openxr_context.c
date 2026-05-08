#include "xr/openxr_context.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool xr_context_init(XrContext *ctx, const char *app_name)
{
	memset(ctx, 0, sizeof(XrContext));
	ctx->view_config_type = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;

	const char *wanted_extensions[] = {
		XR_KHR_VULKAN_ENABLE_EXTENSION_NAME,
		"XR_EXT_psvr2_controller",
		"XR_KHR_binding_modification",
		"XR_EXT_hp_mixed_reality_controller",
		"XR_HTC_vive_cosmos_controller_interaction",
	};
	const char *enabled_extensions[16];
	uint32_t enabled_count = 0;

	uint32_t extCount = 0;
	xrEnumerateInstanceExtensionProperties(NULL, 0, &extCount, NULL);
	XrExtensionProperties *extProps = malloc(sizeof(XrExtensionProperties) * extCount);
	for (uint32_t i = 0; i < extCount; i++)
		extProps[i].type = XR_TYPE_EXTENSION_PROPERTIES;
	xrEnumerateInstanceExtensionProperties(NULL, extCount, &extCount, extProps);

	printf("=== OpenXR Extensions ===\n");
	for (uint32_t i = 0; i < extCount; i++) {
		bool wanted = false;
		for (size_t j = 0; j < sizeof(wanted_extensions) / sizeof(wanted_extensions[0]); j++) {
			if (strcmp(extProps[i].extensionName, wanted_extensions[j]) == 0) {
				wanted = true;
				if (enabled_count < 16) {
					enabled_extensions[enabled_count++] = wanted_extensions[j];
				}
				break;
			}
		}
		printf("  [%s] %s (v%u)\n", wanted ? "X" : " ", extProps[i].extensionName, extProps[i].extensionVersion);
	}
	printf("=========================\n");
	free(extProps);

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
		.enabledExtensionCount = enabled_count,
		.enabledExtensionNames = enabled_extensions,
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
	XR_CHECK(res, "Failed to get system");

	res = xrEnumerateViewConfigurationViews(ctx->instance, ctx->system_id, ctx->view_config_type, 0, &ctx->view_count, NULL);
	XR_CHECK(res, "Failed to count view configs");

	ctx->view_configs = malloc(sizeof(XrViewConfigurationView) * ctx->view_count);
	for (uint32_t i = 0; i < ctx->view_count; i++) {
		ctx->view_configs[i].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
		ctx->view_configs[i].next = NULL;
	}
	res = xrEnumerateViewConfigurationViews(ctx->instance, ctx->system_id, ctx->view_config_type, ctx->view_count, &ctx->view_count, ctx->view_configs);
	XR_CHECK(res, "Failed to enumerate view configs");

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
	xr_context_destroy_session(ctx);

	if (ctx->action_set != XR_NULL_HANDLE) {
		xrDestroyActionSet(ctx->action_set);
		ctx->action_set = XR_NULL_HANDLE;
	}
	if (ctx->instance != XR_NULL_HANDLE) {
		xrDestroyInstance(ctx->instance);
		ctx->instance = XR_NULL_HANDLE;
	}
	if (ctx->view_configs) {
		free(ctx->view_configs);
		ctx->view_configs = NULL;
	}
	if (ctx->views) {
		free(ctx->views);
		ctx->views = NULL;
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
