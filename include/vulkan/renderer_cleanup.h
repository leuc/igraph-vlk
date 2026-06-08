#ifndef RENDERER_CLEANUP_H
#define RENDERER_CLEANUP_H

#include "vulkan/vulkan_types.h"

void cleanup_uniform_buffers(Renderer *r);
void cleanup_compute_context(Renderer *r);
void cleanup_geometry_buffers(Renderer *r);
void cleanup_menu_label_atlases(Renderer *r);
void cleanup_xr_resources(Renderer *r);
void cleanup_splc_pipelines_core(Renderer *r);
void renderer_cleanup(Renderer *r);

#endif
