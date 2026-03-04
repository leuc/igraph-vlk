#ifndef UI_MENU_H
#define UI_MENU_H

#include "interaction/camera.h"
#include "interaction/state.h" // For MenuNode, MenuNodeType, IgraphCommand, etc.
#include "vulkan/renderer.h"

// ============================================================================
// Menu Definition Data Structures (Data-Driven Initialization)
// ============================================================================

/**
 * @brief Static menu item definition for data-driven initialization
 * This struct allows defining the entire menu hierarchy in a single array,
 * avoiding hardcoded pointer assignments in init_menu_tree.
 */
typedef struct
{
	const char *label;		// Menu item label (e.g., "Layout", "Analyze")
	MenuNodeType type;		// NODE_BRANCH (submenu) or NODE_LEAF (command)
	const int *child_ids;	// Array of child node IDs (NULL/empty for leaves). Terminated by -1.
	const char *command_id; // ID ("shortest_path") to lookup command for NODE_LEAF (NULL for branches)
	int radius_level;		// Which spherical level (0=root children, 1=grandchildren, etc.)
} MenuDefinition;

// Function declarations
void init_menu_tree(MenuNode *root);
void destroy_menu_tree(MenuNode *node);
void update_menu_animation(MenuNode *node, float delta_time);
void generate_vulkan_menu_buffers(MenuNode *node, Renderer *r, vec3 cam_pos, vec3 cam_front, vec3 cam_up);
MenuNode *find_menu_node(MenuNode *root, char const *label);

// Data-driven initialization helper
void init_menu_from_definitions(MenuNode *root, const MenuDefinition *definitions, int num_definitions);

#endif // UI_MENU_H
