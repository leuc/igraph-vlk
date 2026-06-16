/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "xr/openxr_context.h"
#include <stdio.h>

bool xr_context_get_vulkan_instance_extensions(XrContext *ctx, char *out_exts, uint32_t *out_size)
{
	PFN_xrGetVulkanInstanceExtensionsKHR xrGetVulkanInstanceExtensionsKHR;
	XrResult res = xrGetInstanceProcAddr(ctx->instance, "xrGetVulkanInstanceExtensionsKHR", (PFN_xrVoidFunction *)&xrGetVulkanInstanceExtensionsKHR);
	if (XR_FAILED(res) || !xrGetVulkanInstanceExtensionsKHR) {
		fprintf(stderr, "OpenXR Error: Failed to get xrGetVulkanInstanceExtensionsKHR (Result: %d)\n", res);
		return false;
	}
	res = xrGetVulkanInstanceExtensionsKHR(ctx->instance, ctx->system_id, *out_size, out_size, out_exts);
	return XR_SUCCEEDED(res);
}

bool xr_context_get_vulkan_device_extensions(XrContext *ctx, char *out_exts, uint32_t *out_size)
{
	PFN_xrGetVulkanDeviceExtensionsKHR xrGetVulkanDeviceExtensionsKHR;
	XrResult res = xrGetInstanceProcAddr(ctx->instance, "xrGetVulkanDeviceExtensionsKHR", (PFN_xrVoidFunction *)&xrGetVulkanDeviceExtensionsKHR);
	if (XR_FAILED(res) || !xrGetVulkanDeviceExtensionsKHR) {
		fprintf(stderr, "OpenXR Error: Failed to get xrGetVulkanDeviceExtensionsKHR (Result: %d)\n", res);
		return false;
	}
	res = xrGetVulkanDeviceExtensionsKHR(ctx->instance, ctx->system_id, *out_size, out_size, out_exts);
	return XR_SUCCEEDED(res);
}

VkPhysicalDevice xr_context_get_vulkan_graphics_device(XrContext *ctx, VkInstance instance)
{
	PFN_xrGetVulkanGraphicsDeviceKHR xrGetVulkanGraphicsDeviceKHR;
	XrResult res = xrGetInstanceProcAddr(ctx->instance, "xrGetVulkanGraphicsDeviceKHR", (PFN_xrVoidFunction *)&xrGetVulkanGraphicsDeviceKHR);
	if (XR_FAILED(res) || !xrGetVulkanGraphicsDeviceKHR) {
		fprintf(stderr, "OpenXR Error: Failed to get xrGetVulkanGraphicsDeviceKHR (Result: %d)\n", res);
		return VK_NULL_HANDLE;
	}
	VkPhysicalDevice pd;
	xrGetVulkanGraphicsDeviceKHR(ctx->instance, ctx->system_id, instance, &pd);
	return pd;
}
