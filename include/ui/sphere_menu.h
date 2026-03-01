#ifndef UI_SPHERE_MENU_H
#define UI_SPHERE_MENU_H

#include "interaction/state.h"  // For MenuNode, MenuNodeType, IgraphCommand, etc.
#include "vulkan/renderer.h"
#include "interaction/camera.h"

// Menu management functions
void init_menu_tree(MenuNode* root);
void destroy_menu_tree(MenuNode* node);
void update_menu_animation(MenuNode* node, float delta_time);
void generate_vulkan_menu_buffers(MenuNode* node, Renderer* r, Camera* cam);
MenuNode* find_menu_node(MenuNode* root, char const* label);

#endif // UI_SPHERE_MENU_H
