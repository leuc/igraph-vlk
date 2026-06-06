#ifndef RENDERER_H
#define RENDERER_H

#include "vulkan/vulkan_commands.h"
#include "vulkan/vulkan_device.h"
#include "vulkan/vulkan_render_pass.h"
#include "vulkan/vulkan_swapchain.h"
#include "vulkan/vulkan_types.h"

#include "vulkan/renderer_camera.h"
#include "vulkan/renderer_draw.h"
#include "vulkan/renderer_labels.h"
#include "vulkan/renderer_lifecycle.h"

#ifdef USE_OPENXR
#include "vulkan/renderer_xr.h"
#include "xr/openxr_context.h"
#endif

void renderer_update_graph(Renderer *r, GraphData *graph);
void renderer_render_ray(Renderer *r, VkCommandBuffer cmd, vec3 origin, vec3 dir, mat4 view, mat4 proj);

#endif
