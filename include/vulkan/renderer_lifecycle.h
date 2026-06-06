#ifndef RENDERER_LIFECYCLE_H
#define RENDERER_LIFECYCLE_H

#include "vulkan/vulkan_types.h"

extern FontAtlas globalAtlas;

bool renderer_init(Renderer *r, GLFWwindow *window, GraphData *graph, void *xr);
void renderer_cleanup(Renderer *r);

#endif
