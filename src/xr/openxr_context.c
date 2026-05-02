#include "xr/openxr_context.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <math.h>

#define XR_CHECK(res, msg) if (XR_FAILED(res)) { fprintf(stderr, "OpenXR Error: %s (Result: %d)\n", msg, res); return false; }

static void xr_fov_to_matrix(const XrFovf fov, float nearZ, float farZ, mat4 out) {
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

static void xr_pose_to_matrix(const XrPosef pose, mat4 out) {
    versor q = {pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w};
    vec3 p = {pose.position.x, pose.position.y, pose.position.z};
    glm_quat_mat4(q, out);
    glm_translate(out, p);
    glm_mat4_inv(out, out); // View matrix is inverse of camera pose
}

bool xr_context_init(XrContext* ctx, const char* app_name) {
    memset(ctx, 0, sizeof(XrContext));
    ctx->view_config_type = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;

    const char* extensions[] = {XR_KHR_VULKAN_ENABLE_EXTENSION_NAME};
    
    XrInstanceCreateInfo createInfo = {
        .type = XR_TYPE_INSTANCE_CREATE_INFO,
        .next = NULL,
        .createFlags = 0,
        .applicationInfo = {
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

    ctx->views = malloc(sizeof(XrView) * ctx->view_count);
    for (uint32_t i = 0; i < ctx->view_count; i++) {
        ctx->views[i].type = XR_TYPE_VIEW;
        ctx->views[i].next = NULL;
    }

    return true;
}

void xr_context_cleanup(XrContext* ctx) {
    if (ctx->instance != XR_NULL_HANDLE) {
        xrDestroyInstance(ctx->instance);
    }
    if (ctx->view_configs) free(ctx->view_configs);
    if (ctx->views) free(ctx->views);
    if (ctx->swapchains) {
        for (uint32_t i = 0; i < ctx->view_count; i++) {
            if (ctx->swapchains[i].handle != XR_NULL_HANDLE) {
                xrDestroySwapchain(ctx->swapchains[i].handle);
            }
            if (ctx->swapchains[i].images) free(ctx->swapchains[i].images);
            if (ctx->swapchains[i].image_views) free(ctx->swapchains[i].image_views);
        }
        free(ctx->swapchains);
    }
}

void xr_context_poll_events(XrContext* ctx) {
    XrEventDataBuffer event = { .type = XR_TYPE_EVENT_DATA_BUFFER };
    while (xrPollEvent(ctx->instance, &event) == XR_SUCCESS) {
        switch (event.type) {
            case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
                XrEventDataSessionStateChanged* changed = (XrEventDataSessionStateChanged*)&event;
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
            default: break;
        }
        event.type = XR_TYPE_EVENT_DATA_BUFFER;
    }
}

bool xr_context_create_session(XrContext* ctx, VkInstance instance, VkPhysicalDevice physical_device, VkDevice device, uint32_t queue_family_index, uint32_t queue_index) {
    PFN_xrGetVulkanGraphicsRequirementsKHR xrGetVulkanGraphicsRequirementsKHR;
    xrGetInstanceProcAddr(ctx->instance, "xrGetVulkanGraphicsRequirementsKHR", (PFN_xrVoidFunction*)&xrGetVulkanGraphicsRequirementsKHR);
    XrGraphicsRequirementsVulkanKHR graphicsRequirements = { .type = XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR };
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

    // Create swapchains
    ctx->swapchains = malloc(sizeof(*ctx->swapchains) * ctx->view_count);
    for (uint32_t i = 0; i < ctx->view_count; i++) {
        XrSwapchainCreateInfo swapchainCreateInfo = {
            .type = XR_TYPE_SWAPCHAIN_CREATE_INFO,
            .usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT,
            .format = VK_FORMAT_B8G8R8A8_SRGB, // Should query supported formats
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

        XrSwapchainImageVulkanKHR* images = malloc(sizeof(XrSwapchainImageVulkanKHR) * ctx->swapchains[i].image_count);
        for (uint32_t j = 0; j < ctx->swapchains[i].image_count; j++) {
            images[j].type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR;
            images[j].next = NULL;
        }
        res = xrEnumerateSwapchainImages(ctx->swapchains[i].handle, ctx->swapchains[i].image_count, &ctx->swapchains[i].image_count, (XrSwapchainImageBaseHeader*)images);
        XR_CHECK(res, "Failed to enumerate swapchain images");

        ctx->swapchains[i].images = malloc(sizeof(VkImage) * ctx->swapchains[i].image_count);
        for (uint32_t j = 0; j < ctx->swapchains[i].image_count; j++) {
            ctx->swapchains[i].images[j] = images[j].image;
        }
        free(images);
    }

    return true;
}

bool xr_context_get_vulkan_instance_extensions(XrContext* ctx, char* out_exts, uint32_t* out_size) {
    PFN_xrGetVulkanInstanceExtensionsKHR xrGetVulkanInstanceExtensionsKHR;
    xrGetInstanceProcAddr(ctx->instance, "xrGetVulkanInstanceExtensionsKHR", (PFN_xrVoidFunction*)&xrGetVulkanInstanceExtensionsKHR);
    XrResult res = xrGetVulkanInstanceExtensionsKHR(ctx->instance, ctx->system_id, *out_size, out_size, out_exts);
    return XR_SUCCEEDED(res);
}

bool xr_context_get_vulkan_device_extensions(XrContext* ctx, char* out_exts, uint32_t* out_size) {
    PFN_xrGetVulkanDeviceExtensionsKHR xrGetVulkanDeviceExtensionsKHR;
    xrGetInstanceProcAddr(ctx->instance, "xrGetVulkanDeviceExtensionsKHR", (PFN_xrVoidFunction*)&xrGetVulkanDeviceExtensionsKHR);
    XrResult res = xrGetVulkanDeviceExtensionsKHR(ctx->instance, ctx->system_id, *out_size, out_size, out_exts);
    return XR_SUCCEEDED(res);
}

VkPhysicalDevice xr_context_get_vulkan_graphics_device(XrContext* ctx, VkInstance instance) {
    PFN_xrGetVulkanGraphicsDeviceKHR xrGetVulkanGraphicsDeviceKHR;
    xrGetInstanceProcAddr(ctx->instance, "xrGetVulkanGraphicsDeviceKHR", (PFN_xrVoidFunction*)&xrGetVulkanGraphicsDeviceKHR);
    VkPhysicalDevice pd;
    xrGetVulkanGraphicsDeviceKHR(ctx->instance, ctx->system_id, instance, &pd);
    return pd;
}

bool xr_context_begin_frame(XrContext* ctx, XrFrameState* frame_state) {
    XrFrameWaitInfo waitInfo = { .type = XR_TYPE_FRAME_WAIT_INFO };
    XrResult res = xrWaitFrame(ctx->session, &waitInfo, frame_state);
    XR_CHECK(res, "Failed to wait frame");

    XrFrameBeginInfo beginInfo = { .type = XR_TYPE_FRAME_BEGIN_INFO };
    res = xrBeginFrame(ctx->session, &beginInfo);
    XR_CHECK(res, "Failed to begin frame");

    XrViewState viewState = { .type = XR_TYPE_VIEW_STATE };
    XrViewLocateInfo locateInfo = {
        .type = XR_TYPE_VIEW_LOCATE_INFO,
        .viewConfigurationType = ctx->view_config_type,
        .displayTime = frame_state->predictedDisplayTime,
        .space = ctx->stage_space,
    };
    res = xrLocateViews(ctx->session, &locateInfo, &viewState, ctx->view_count, &ctx->view_count, ctx->views);
    XR_CHECK(res, "Failed to locate views");

    return true;
}

void xr_context_get_view_matrix(XrContext* ctx, uint32_t view_index, mat4 out) {
    xr_pose_to_matrix(ctx->views[view_index].pose, out);
}

void xr_context_get_projection_matrix(XrContext* ctx, uint32_t view_index, float nearZ, float farZ, mat4 out) {
    xr_fov_to_matrix(ctx->views[view_index].fov, nearZ, farZ, out);
}

bool xr_context_end_frame(XrContext* ctx, XrFrameState* frame_state, XrCompositionLayerBaseHeader** layers, uint32_t layer_count) {
    XrFrameEndInfo endInfo = {
        .type = XR_TYPE_FRAME_END_INFO,
        .displayTime = frame_state->predictedDisplayTime,
        .environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE,
        .layerCount = layer_count,
        .layers = (const XrCompositionLayerBaseHeader * const*)layers,
    };
    XrResult res = xrEndFrame(ctx->session, &endInfo);
    XR_CHECK(res, "Failed to end frame");
    return true;
}
