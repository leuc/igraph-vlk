#ifndef RENDERER_LIFECYCLE_H
#define RENDERER_LIFECYCLE_H

#include "vulkan/renderer_cleanup.h"
#include "vulkan/vulkan_types.h"

extern FontAtlas globalAtlas;

bool renderer_init(Renderer *r, GLFWwindow *window, GraphData *graph, void *xr);
void renderer_recreate_swapchain(Renderer *r);

#endif
