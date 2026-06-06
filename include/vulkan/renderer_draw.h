#ifndef RENDERER_DRAW_H
#define RENDERER_DRAW_H

#include "vulkan/vulkan_types.h"

void renderer_draw_frame(Renderer *r);
void renderer_render_scene(Renderer *r, VkCommandBuffer cmd, VkRenderPass rp, VkFramebuffer fb, VkExtent2D extent, mat4 view, mat4 proj, uint32_t view_index, bool has_ray, vec3 ray_origin, vec3 ray_dir);

#endif
