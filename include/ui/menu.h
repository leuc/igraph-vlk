/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef UI_MENU_H
#define UI_MENU_H

#include "graph/graph_types.h" // For GraphData
#include "interaction/camera.h"
#include "interaction/spatial.h"
#include "interaction/state.h" // For MenuNode, MenuNodeType, IgraphCommand, etc.

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
	MenuNodeType type;		// NODE_BRANCH, NODE_LEAF_COMMAND, NODE_INFO_DISPLAY
	const int *child_ids;	// Array of child node IDs (NULL/empty for leaves). Terminated by -1.
	const char *command_id; // ID ("shortest_path") to lookup command for NODE_LEAF_COMMAND (NULL for branches)
	const char *info_value; // For NODE_INFO_DISPLAY: the read-only value to show
	int radius_level;		// Which spherical level (0=root children, 1=grandchildren, etc.)
} MenuDefinition;

// Function declarations
void init_menu_tree(MenuNode *root);
void menu_tree_destroy(MenuNode *node);
void update_menu_transforms(MenuNode *node, const SpatialBasis *basis);
MenuNode *raycast_menu_vr(struct AppState *state, vec3 ray_ori, vec3 ray_dir);

// ============================================================================
// Dynamic Attribute Filter Menu
// ============================================================================

/**
 * Populate the "Node > Filter" submenu with entries from the graph's
 * filterable attributes (low-cardinality string/boolean attributes).
 * @param root Root menu node
 * @param data GraphData with filterable_attrs populated
 */
void menu_populate_attribute_filters(MenuNode *root, GraphData *data);

/**
 * Clear all dynamically added filter entries from the "Node > Filter" submenu.
 * @param root Root menu node
 */
void menu_clear_attribute_filters(MenuNode *root, GraphData *data);

#endif // UI_MENU_H
