#ifndef RENDERER_H
#define RENDERER_H

#include "vulkan/vulkan_commands.h"
#include "vulkan/vulkan_device.h"
#include "vulkan/vulkan_render_pass.h"
#include "vulkan/vulkan_swapchain.h"
#include "vulkan/vulkan_types.h"

#ifdef USE_OPENXR
#include "xr/openxr_context.h"
#endif

int renderer_init(Renderer *r, GLFWwindow *window, GraphData *graph, void *xr);
void renderer_cleanup(Renderer *r);
void renderer_draw_frame(Renderer *r);
void renderer_render_scene(Renderer *r, VkCommandBuffer cmd, VkRenderPass rp, VkFramebuffer fb, VkExtent2D extent, mat4 view, mat4 proj, uint32_t view_index, bool has_ray, vec3 ray_origin, vec3 ray_dir);
void renderer_update_view(Renderer *r, vec3 pos, vec3 front, vec3 up);
void renderer_update_graph(Renderer *r, GraphData *graph);
void renderer_render_ray(Renderer *r, VkCommandBuffer cmd, vec3 origin, vec3 dir, mat4 view, mat4 proj);

#ifdef USE_OPENXR
void renderer_setup_xr(Renderer *r, XrContext *xr);
#endif

#endif
