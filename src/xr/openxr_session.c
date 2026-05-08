#include "xr/openxr_context.h"
#include <stdio.h>
#include <stdlib.h>

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
	XR_CHECK(res, "Failed to create session");

	XrReferenceSpaceCreateInfo spaceCreateInfo = {
		.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO,
		.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE,
		.poseInReferenceSpace = {{0, 0, 0, 1}, {0, 0, 0}},
	};
	res = xrCreateReferenceSpace(ctx->session, &spaceCreateInfo, &ctx->stage_space);
	XR_CHECK(res, "Failed to create reference space");

	uint32_t formatCount = 0;
	res = xrEnumerateSwapchainFormats(ctx->session, 0, &formatCount, NULL);
	XR_CHECK(res, "Failed to count swapchain formats");

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
		XR_CHECK(res, "Failed to create swapchain");

		res = xrEnumerateSwapchainImages(ctx->swapchains[i].handle, 0, &ctx->swapchains[i].image_count, NULL);
		XR_CHECK(res, "Failed to count swapchain images");

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

void xr_context_destroy_session(XrContext *ctx)
{
	if (ctx->stage_space != XR_NULL_HANDLE) {
		xrDestroySpace(ctx->stage_space);
		ctx->stage_space = XR_NULL_HANDLE;
	}

	if (ctx->swapchains) {
		for (uint32_t i = 0; i < ctx->view_count; i++) {
			if (ctx->swapchains[i].handle != XR_NULL_HANDLE) {
				xrDestroySwapchain(ctx->swapchains[i].handle);
				ctx->swapchains[i].handle = XR_NULL_HANDLE;
			}
			if (ctx->swapchains[i].images) {
				free(ctx->swapchains[i].images);
				ctx->swapchains[i].images = NULL;
			}
			if (ctx->swapchains[i].image_views) {
				free(ctx->swapchains[i].image_views);
				ctx->swapchains[i].image_views = NULL;
			}
		}
		free(ctx->swapchains);
		ctx->swapchains = NULL;
	}

	if (ctx->session != XR_NULL_HANDLE) {
		xrDestroySession(ctx->session);
		ctx->session = XR_NULL_HANDLE;
	}

	ctx->session_running = false;
}
