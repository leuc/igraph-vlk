/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "interaction/display_hdr.h"

#include <stdio.h>
#include <stdlib.h>

#ifdef HAVE_WAYLAND_COLOR_MANAGEMENT
#include <string.h>
#include <unistd.h>

#define GLFW_EXPOSE_NATIVE_WAYLAND
#include <GLFW/glfw3native.h>
#include <wayland-client.h>

#include "color-management-v1-client-protocol.h"
#endif

#define BT2020_COORDINATE_TOLERANCE 2500

static bool coordinate_near(int32_t actual, int32_t expected)
{
	int64_t delta = (int64_t)actual - expected;
	return delta >= -BT2020_COORDINATE_TOLERANCE && delta <= BT2020_COORDINATE_TOLERANCE;
}

bool display_hdr_is_bt2020_primaries(int32_t r_x, int32_t r_y, int32_t g_x, int32_t g_y, int32_t b_x, int32_t b_y, int32_t w_x, int32_t w_y)
{
	return coordinate_near(r_x, 708000) && coordinate_near(r_y, 292000) && coordinate_near(g_x, 170000) && coordinate_near(g_y, 797000) && coordinate_near(b_x, 131000) && coordinate_near(b_y, 46000) && coordinate_near(w_x, 312700) && coordinate_near(w_y, 329000);
}

bool display_hdr_description_is_hdr10(bool has_st2084_pq, bool has_bt2020_primaries)
{
	return has_st2084_pq && has_bt2020_primaries;
}

#ifdef HAVE_WAYLAND_COLOR_MANAGEMENT

struct DisplayHdrTracker
{
	WindowState *window;
	struct wl_display *display;
	struct wl_registry *registry;
	struct wp_color_manager_v1 *manager;
	struct wp_color_management_surface_feedback_v1 *feedback;
	struct wp_image_description_v1 *description;
	struct wp_image_description_info_v1 *info;
	bool pending_st2084_pq;
	bool pending_bt2020_primaries;
	DisplayColorInfo pending;
};

static ColorPrimaries color_primaries_from_wayland(int32_t r_x, int32_t r_y, int32_t g_x, int32_t g_y, int32_t b_x, int32_t b_y, int32_t w_x, int32_t w_y)
{
	return (ColorPrimaries){.r_x = (float)r_x / 1000000.0f, .r_y = (float)r_y / 1000000.0f, .g_x = (float)g_x / 1000000.0f, .g_y = (float)g_y / 1000000.0f, .b_x = (float)b_x / 1000000.0f, .b_y = (float)b_y / 1000000.0f, .w_x = (float)w_x / 1000000.0f, .w_y = (float)w_y / 1000000.0f};
}

static ColorPrimaries bt2020_primaries(void)
{
	return color_primaries_from_wayland(708000, 292000, 170000, 797000, 131000, 46000, 312700, 329000);
}

static void tracker_commit(DisplayHdrTracker *tracker, const DisplayColorInfo *info)
{
	tracker->window->displayColor = *info;
	tracker->window->displayColorDirty = true;
}

static void tracker_set_unknown(DisplayHdrTracker *tracker)
{
	DisplayColorInfo info = tracker->window->displayColor;
	info.known = false;
	info.hdr10 = false;
	tracker_commit(tracker, &info);
}

static void manager_supported_intent(void *data, struct wp_color_manager_v1 *manager, uint32_t intent)
{
	(void)data;
	(void)manager;
	(void)intent;
}

static void manager_supported_feature(void *data, struct wp_color_manager_v1 *manager, uint32_t feature)
{
	(void)data;
	(void)manager;
	(void)feature;
}

static void manager_supported_tf_named(void *data, struct wp_color_manager_v1 *manager, uint32_t tf)
{
	(void)data;
	(void)manager;
	(void)tf;
}

static void manager_supported_primaries_named(void *data, struct wp_color_manager_v1 *manager, uint32_t primaries)
{
	(void)data;
	(void)manager;
	(void)primaries;
}

static void manager_done(void *data, struct wp_color_manager_v1 *manager)
{
	(void)data;
	(void)manager;
}

static const struct wp_color_manager_v1_listener manager_listener = {
	.supported_intent = manager_supported_intent,
	.supported_feature = manager_supported_feature,
	.supported_tf_named = manager_supported_tf_named,
	.supported_primaries_named = manager_supported_primaries_named,
	.done = manager_done,
};

static void registry_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
	DisplayHdrTracker *tracker = data;
	if (tracker->manager || strcmp(interface, wp_color_manager_v1_interface.name) != 0) {
		return;
	}
	tracker->manager = wl_registry_bind(registry, name, &wp_color_manager_v1_interface, version < 1 ? version : 1);
	if (tracker->manager) {
		wp_color_manager_v1_add_listener(tracker->manager, &manager_listener, tracker);
	}
}

static void registry_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
	(void)data;
	(void)registry;
	(void)name;
}

static const struct wl_registry_listener registry_listener = {
	.global = registry_global,
	.global_remove = registry_global_remove,
};

static void info_done(void *data, struct wp_image_description_info_v1 *info)
{
	DisplayHdrTracker *tracker = data;
	if (info != tracker->info) {
		return;
	}
	tracker->info = NULL;
	tracker->pending.known = true;
	tracker->pending.hdr10 = display_hdr_description_is_hdr10(tracker->pending_st2084_pq, tracker->pending_bt2020_primaries);
	tracker_commit(tracker, &tracker->pending);
	if (tracker->description) {
		wp_image_description_v1_destroy(tracker->description);
		tracker->description = NULL;
	}
}

static void info_icc_file(void *data, struct wp_image_description_info_v1 *info, int32_t fd, uint32_t size)
{
	(void)data;
	(void)info;
	(void)size;
	close(fd);
}

static void info_primaries(void *data, struct wp_image_description_info_v1 *info, int32_t r_x, int32_t r_y, int32_t g_x, int32_t g_y, int32_t b_x, int32_t b_y, int32_t w_x, int32_t w_y)
{
	(void)info;
	DisplayHdrTracker *tracker = data;
	tracker->pending_bt2020_primaries |= display_hdr_is_bt2020_primaries(r_x, r_y, g_x, g_y, b_x, b_y, w_x, w_y);
	tracker->pending.primaries = color_primaries_from_wayland(r_x, r_y, g_x, g_y, b_x, b_y, w_x, w_y);
	tracker->pending.has_primaries = true;
}

static void info_primaries_named(void *data, struct wp_image_description_info_v1 *info, uint32_t primaries)
{
	(void)info;
	DisplayHdrTracker *tracker = data;
	tracker->pending_bt2020_primaries |= primaries == WP_COLOR_MANAGER_V1_PRIMARIES_BT2020;
	if (primaries == WP_COLOR_MANAGER_V1_PRIMARIES_BT2020) {
		tracker->pending.primaries = bt2020_primaries();
		tracker->pending.has_primaries = true;
	}
}

static void info_tf_power(void *data, struct wp_image_description_info_v1 *info, uint32_t exponent)
{
	(void)data;
	(void)info;
	(void)exponent;
}

static void info_tf_named(void *data, struct wp_image_description_info_v1 *info, uint32_t tf)
{
	(void)info;
	DisplayHdrTracker *tracker = data;
	tracker->pending_st2084_pq = tf == WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_ST2084_PQ;
}

static void info_luminances(void *data, struct wp_image_description_info_v1 *info, uint32_t min_lum, uint32_t max_lum, uint32_t reference_lum)
{
	(void)info;
	DisplayHdrTracker *tracker = data;
	tracker->pending.min_luminance = (float)min_lum / 10000.0f;
	tracker->pending.max_luminance = (float)max_lum;
	tracker->pending.reference_luminance = (float)reference_lum;
	tracker->pending.has_luminance = true;
}

static void info_target_primaries(void *data, struct wp_image_description_info_v1 *info, int32_t r_x, int32_t r_y, int32_t g_x, int32_t g_y, int32_t b_x, int32_t b_y, int32_t w_x, int32_t w_y)
{
	(void)info;
	DisplayHdrTracker *tracker = data;
	tracker->pending.target_primaries = color_primaries_from_wayland(r_x, r_y, g_x, g_y, b_x, b_y, w_x, w_y);
	tracker->pending.has_target_primaries = true;
}

static void info_target_luminance(void *data, struct wp_image_description_info_v1 *info, uint32_t min_lum, uint32_t max_lum)
{
	(void)info;
	DisplayHdrTracker *tracker = data;
	tracker->pending.target_min_luminance = (float)min_lum / 10000.0f;
	tracker->pending.target_max_luminance = (float)max_lum;
	tracker->pending.has_target_luminance = true;
}

static void info_target_max_cll(void *data, struct wp_image_description_info_v1 *info, uint32_t max_cll)
{
	(void)info;
	DisplayHdrTracker *tracker = data;
	tracker->pending.target_max_cll = (float)max_cll;
}

static void info_target_max_fall(void *data, struct wp_image_description_info_v1 *info, uint32_t max_fall)
{
	(void)info;
	DisplayHdrTracker *tracker = data;
	tracker->pending.target_max_fall = (float)max_fall;
}

static const struct wp_image_description_info_v1_listener info_listener = {
	.done = info_done,
	.icc_file = info_icc_file,
	.primaries = info_primaries,
	.primaries_named = info_primaries_named,
	.tf_power = info_tf_power,
	.tf_named = info_tf_named,
	.luminances = info_luminances,
	.target_primaries = info_target_primaries,
	.target_luminance = info_target_luminance,
	.target_max_cll = info_target_max_cll,
	.target_max_fall = info_target_max_fall,
};

static void description_failed(void *data, struct wp_image_description_v1 *description, uint32_t cause, const char *message)
{
	DisplayHdrTracker *tracker = data;
	if (description != tracker->description) {
		return;
	}
	fprintf(stderr, "[Wayland] Failed to read current display color description (%u): %s\n", cause, message ? message : "unknown error");
	tracker_set_unknown(tracker);
	wp_image_description_v1_destroy(description);
	tracker->description = NULL;
}

static void description_ready(void *data, struct wp_image_description_v1 *description, uint32_t identity)
{
	DisplayHdrTracker *tracker = data;
	if (description != tracker->description) {
		return;
	}
	tracker->pending.revision = identity;
	tracker->info = wp_image_description_v1_get_information(description);
	if (tracker->info) {
		wp_image_description_info_v1_add_listener(tracker->info, &info_listener, tracker);
		wl_display_flush(tracker->display);
	}
}

static const struct wp_image_description_v1_listener description_listener = {
	.failed = description_failed,
	.ready = description_ready,
};

static void tracker_query_preferred(DisplayHdrTracker *tracker)
{
	if (tracker->info) {
		wp_image_description_info_v1_destroy(tracker->info);
		tracker->info = NULL;
	}
	if (tracker->description) {
		wp_image_description_v1_destroy(tracker->description);
		tracker->description = NULL;
	}
	tracker->pending_st2084_pq = false;
	tracker->pending_bt2020_primaries = false;
	tracker->pending = (DisplayColorInfo){0};

	tracker->description = wp_color_management_surface_feedback_v1_get_preferred(tracker->feedback);
	if (tracker->description) {
		wp_image_description_v1_add_listener(tracker->description, &description_listener, tracker);
		wl_display_flush(tracker->display);
	}
}

static void feedback_preferred_changed(void *data, struct wp_color_management_surface_feedback_v1 *feedback, uint32_t identity)
{
	(void)feedback;
	(void)identity;
	tracker_query_preferred(data);
}

static const struct wp_color_management_surface_feedback_v1_listener feedback_listener = {
	.preferred_changed = feedback_preferred_changed,
};

static void tracker_destroy(DisplayHdrTracker *tracker)
{
	if (!tracker) {
		return;
	}
	if (tracker->info) {
		wp_image_description_info_v1_destroy(tracker->info);
	}
	if (tracker->description) {
		wp_image_description_v1_destroy(tracker->description);
	}
	if (tracker->feedback) {
		wp_color_management_surface_feedback_v1_destroy(tracker->feedback);
	}
	if (tracker->manager) {
		wp_color_manager_v1_destroy(tracker->manager);
	}
	if (tracker->registry) {
		wl_registry_destroy(tracker->registry);
	}
	free(tracker);
}

#endif

bool window_display_hdr_init(WindowState *window)
{
	window->displayColor = (DisplayColorInfo){0};
	window->displayColorDirty = true;
	window->displayHdrTracker = NULL;

#ifdef HAVE_WAYLAND_COLOR_MANAGEMENT
#if GLFW_VERSION_MAJOR > 3 || (GLFW_VERSION_MAJOR == 3 && GLFW_VERSION_MINOR >= 4)
	if (glfwGetPlatform() != GLFW_PLATFORM_WAYLAND) {
		return false;
	}
#endif

	DisplayHdrTracker *tracker = calloc(1, sizeof(*tracker));
	if (!tracker) {
		return false;
	}
	tracker->window = window;
	tracker->display = glfwGetWaylandDisplay();
	struct wl_surface *surface = glfwGetWaylandWindow(window->handle);
	if (!tracker->display || !surface) {
		tracker_destroy(tracker);
		return false;
	}
	window->displayHdrTracker = tracker;

	tracker->registry = wl_display_get_registry(tracker->display);
	if (!tracker->registry || wl_registry_add_listener(tracker->registry, &registry_listener, tracker) != 0 || wl_display_roundtrip(tracker->display) < 0) {
		window_display_hdr_cleanup(window);
		return false;
	}
	if (!tracker->manager) {
		printf("[Wayland] Per-display HDR10 tracking unavailable: wp_color_manager_v1 is not advertised\n");
		window_display_hdr_cleanup(window);
		return false;
	}

	tracker->feedback = wp_color_manager_v1_get_surface_feedback(tracker->manager, surface);
	if (!tracker->feedback || wp_color_management_surface_feedback_v1_add_listener(tracker->feedback, &feedback_listener, tracker) != 0) {
		window_display_hdr_cleanup(window);
		return false;
	}
	tracker_query_preferred(tracker);
	if (wl_display_roundtrip(tracker->display) < 0 || wl_display_roundtrip(tracker->display) < 0) {
		window_display_hdr_cleanup(window);
		return false;
	}
	printf("[Wayland] Tracking HDR10 for the window's current display set with wp_color_manager_v1\n");
	return true;
#else
	return false;
#endif
}

void window_display_hdr_cleanup(WindowState *window)
{
#ifdef HAVE_WAYLAND_COLOR_MANAGEMENT
	tracker_destroy(window->displayHdrTracker);
#endif
	window->displayHdrTracker = NULL;
}

bool window_consume_display_color_info(WindowState *window, DisplayColorInfo *info)
{
	if (!window->displayColorDirty) {
		return false;
	}
	window->displayColorDirty = false;
	if (info) {
		*info = window->displayColor;
	}
	return true;
}
