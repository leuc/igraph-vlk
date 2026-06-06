#ifndef RENDERER_CAMERA_H
#define RENDERER_CAMERA_H

#include "vulkan/vulkan_types.h"

void renderer_update_view(Renderer *r, vec3 pos, vec3 front, vec3 up);

#endif
