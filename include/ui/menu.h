/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef UI_MENU_H
#define UI_MENU_H

#include "graph/graph_types.h"
#include "interaction/state.h"

void init_menu_tree(MenuNode *root);
void menu_tree_destroy(MenuNode *node);
bool menu_update_layout(MenuState *menu);
MenuNode *menu_find_node_by_command_id(MenuNode *node, const char *command_id);

void menu_populate_attribute_filters(MenuState *menu, GraphData *data);
void menu_clear_attribute_filters(MenuState *menu, GraphData *data);
void menu_populate_attribute_edge_filters(MenuState *menu, GraphData *data);
void menu_clear_attribute_edge_filters(MenuState *menu, GraphData *data);
void menu_populate_netzschleuder_static(MenuState *menu);
void menu_populate_famous_graphs(MenuState *menu);

#endif // UI_MENU_H
