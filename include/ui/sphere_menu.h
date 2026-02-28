#ifndef UI_SPHERE_MENU_H
#define UI_SPHERE_MENU_H

#include "interaction/state.h"  // For MenuNode, MenuNodeType, IgraphCommand, etc.

// Menu management functions
void init_menu_tree(MenuNode* root);
void destroy_menu_tree(MenuNode* node);
void update_menu_animation(MenuNode* node, float delta_time);
void generate_vulkan_menu_buffers(MenuNode* node);
MenuNode* find_menu_node(MenuNode* root, char const* label);

#endif // UI_SPHERE_MENU_H
