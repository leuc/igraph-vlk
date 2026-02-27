#ifndef SPHERE_MENU_H
#define SPHERE_MENU_H

#include "../interaction/state.h"  // For IgraphCommand

// --- Menu Node Types ---
typedef enum {
    NODE_BRANCH, // Submenu
    NODE_LEAF    // Actionable Command
} MenuNodeType;

// Forward declaration for self-referential struct
typedef struct MenuNode MenuNode;

// --- 3D Spherical Menu Tree Structure ---
typedef struct MenuNode {
    MenuNodeType type;
    const char* label;
    int icon_texture_id;  // Vulkan texture handle
    
    // 3D Visual State for unfolding animation
    float target_phi;      // Spherical coordinate (azimuth)
    float target_theta;    // Spherical coordinate (polar)
    float current_radius; 
    
    // For Branches (submenus)
    int num_children;
    MenuNode** children;
    
    // For Leaves (commands)
    IgraphCommand* command;
} MenuNode;

#endif // SPHERE_MENU_H
