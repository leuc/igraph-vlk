#ifndef VULKAN_MENU_H
#define VULKAN_MENU_H

#include "interaction/state.h"
#include "vulkan/renderer.h"

void generate_vulkan_menu_buffers(MenuNode *node, Renderer *r, vec3 cam_pos, vec3 cam_front, vec3 cam_up);

#endif // VULKAN_MENU_H
