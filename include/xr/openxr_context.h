#pragma once

#define XR_USE_GRAPHICS_API_VULKAN
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <stdbool.h>
#include <stdint.h>
#include <cglm/cglm.h>

typedef struct {
    XrInstance instance;
    XrSystemId system_id;
    XrSession session;
    XrSpace stage_space;
    XrViewConfigurationType view_config_type;
    uint32_t view_count;
    XrViewConfigurationView* view_configs;
    XrView* views;
    
    struct {
        XrSwapchain handle;
        uint32_t width;
        uint32_t height;
        uint32_t image_count;
        VkImage* images;
        VkImageView* image_views;
    }* swapchains;

    bool session_running;
    XrSessionState state;
} XrContext;

bool xr_context_init(XrContext* ctx, const char* app_name);
void xr_context_cleanup(XrContext* ctx);
void xr_context_poll_events(XrContext* ctx);

bool xr_context_get_vulkan_instance_extensions(XrContext* ctx, char* out_exts, uint32_t* out_size);
bool xr_context_get_vulkan_device_extensions(XrContext* ctx, char* out_exts, uint32_t* out_size);
VkPhysicalDevice xr_context_get_vulkan_graphics_device(XrContext* ctx, VkInstance instance);

bool xr_context_create_session(XrContext* ctx, VkInstance instance, VkPhysicalDevice physical_device, VkDevice device, uint32_t queue_family_index, uint32_t queue_index);
void xr_context_destroy_session(XrContext* ctx);

bool xr_context_begin_frame(XrContext* ctx, XrFrameState* frame_state);
void xr_context_get_view_matrix(XrContext* ctx, uint32_t view_index, mat4 out);
void xr_context_get_projection_matrix(XrContext* ctx, uint32_t view_index, float nearZ, float farZ, mat4 out);
bool xr_context_end_frame(XrContext* ctx, XrFrameState* frame_state, XrCompositionLayerBaseHeader** layers, uint32_t layer_count);
