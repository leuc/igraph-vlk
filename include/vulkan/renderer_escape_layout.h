#ifndef RENDERER_ESCAPE_LAYOUT_H
#define RENDERER_ESCAPE_LAYOUT_H

#include "vulkan/vulkan_types.h"

void renderer_escape_build_blas(Renderer *r);
void renderer_escape_build_tlas(Renderer *r, GraphData *data, uint32_t node_count);
void renderer_escape_create_rt_pipeline(Renderer *r, VkBuffer physics_buffer);
void renderer_escape_update_rt_physics_buffer(Renderer *r, VkBuffer physics_buffer);
void renderer_escape_update_tlas_cpu(Renderer *r, uint32_t node_count);
void renderer_escape_record_rt_pass(VkCommandBuffer cmd, Renderer *r, uint32_t node_count, uint32_t frame_index);
void renderer_escape_cleanup_rt(Renderer *r);

#endif
