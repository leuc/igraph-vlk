#include "xr/openxr_context.h"
#include <stdio.h>

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
