#pragma once

#include <vulkan/vulkan.h>
#define XR_USE_GRAPHICS_API_VULKAN
#include <cglm/cglm.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <stdbool.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

#define XR_CHECK(res, msg) \
	if (XR_FAILED(res)) { \
		fprintf(stderr, "OpenXR Error: %s (Result: %d)\n", msg, res); \
		return false; \
	}

#define XR_CHECK_VOID(res, msg) \
	if (XR_FAILED(res)) { \
		fprintf(stderr, "OpenXR Error: %s (Result: %d)\n", msg, res); \
		return; \
	}

typedef struct
{
	XrInstance instance;
	XrSystemId system_id;
	XrSession session;
	XrSpace stage_space;
	XrViewConfigurationType view_config_type;
	uint32_t view_count;
	VkFormat swapchain_format; // Chosen swapchain format
	XrViewConfigurationView *view_configs;
	XrView *views;

	struct
	{
		XrSwapchain handle;
		uint32_t width;
		uint32_t height;
		uint32_t image_count;
		VkImage *images;
		VkImageView *image_views;
	} *swapchains;

	bool session_running;
	XrSessionState state;

	// Input actions
	XrActionSet action_set;
	XrAction select_action;
	XrAction menu_action;
	XrAction button_a_action;
	XrAction button_b_action;
	XrAction button_x_action;
	XrAction button_y_action;
	XrAction thumbstick_left_action;
	XrAction thumbstick_right_action;
	XrPath hand_paths[2];
	bool printed_capabilities;
} XrContext;

bool xr_context_init(XrContext *ctx, const char *app_name);
void xr_context_cleanup(XrContext *ctx);
void xr_context_poll_events(XrContext *ctx);
bool xr_context_init_input(XrContext *ctx);
void xr_context_sync_input(XrContext *ctx);
bool xr_context_is_action_pressed(XrContext *ctx, XrAction action, uint32_t hand_index);
float xr_context_get_thumbstick(XrContext *ctx, uint32_t hand_index, uint32_t axis);

bool xr_context_get_vulkan_instance_extensions(XrContext *ctx, char *out_exts, uint32_t *out_size);
bool xr_context_get_vulkan_device_extensions(XrContext *ctx, char *out_exts, uint32_t *out_size);
VkPhysicalDevice xr_context_get_vulkan_graphics_device(XrContext *ctx, VkInstance instance);

bool xr_context_create_session(XrContext *ctx, VkInstance instance, VkPhysicalDevice physical_device, VkDevice device, uint32_t queue_family_index, uint32_t queue_index);
void xr_context_destroy_session(XrContext *ctx);

bool xr_context_wait_frame(XrContext *ctx, XrFrameState *frame_state);
bool xr_context_begin_frame(XrContext *ctx);
bool xr_context_locate_views(XrContext *ctx, XrTime predictedDisplayTime);
void xr_context_get_view_matrix(XrContext *ctx, uint32_t view_index, vec3 camera_pos, float camera_yaw, mat4 out);
void xr_context_get_projection_matrix(XrContext *ctx, uint32_t view_index, float nearZ, float farZ, mat4 out);
bool xr_context_end_frame(XrContext *ctx, XrFrameState *frame_state, XrCompositionLayerBaseHeader **layers, uint32_t layer_count);
void xr_context_print_capabilities(XrContext *ctx);
