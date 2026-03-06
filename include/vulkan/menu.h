#ifndef VULKAN_MENU_H
#define VULKAN_MENU_H

#include "interaction/spatial.h"
#include "interaction/state.h"
#include "vulkan/renderer.h"

void generate_vulkan_menu_buffers(MenuNode *node, Renderer *r, const SpatialBasis *basis);

#endif // VULKAN_MENU_H
