#include "app_state.h"
#include "interaction/state.h"
#include "interaction/picking.h"
#include <GLFW/glfw3.h>
#include <stdlib.h>
#include <stdio.h>

void app_context_init(AppContext* ctx, igraph_t* graph, MenuNode* root_menu) {
    ctx->current_state = STATE_GRAPH_VIEW;
    ctx->root_menu = root_menu;
    ctx->active_menu_level = root_menu;
    ctx->pending_command = NULL;
    ctx->selection_step = 0;
    ctx->target_graph = graph;
}

void app_context_destroy(AppContext* ctx) {
    // Cleanup if anything was allocated
}

void update_app_state(AppState* state) {
    AppContext* app = &state->app_ctx;
    
    // In menu open state: perform crosshair raycasting to track hover
    if (app->current_state == STATE_MENU_OPEN) {
        // Raycast from camera through screen center (crosshair)
        MenuNode* hovered = raycast_menu_crosshair(state);
        app->crosshair_hovered_node = hovered;
        
        // Check for activation trigger (left mouse button press)
        if (hovered && glfwGetMouseButton(state->window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
            printf("[DEBUG] Triggered menu option: %s\n", hovered->label);
            handle_menu_selection(app, hovered);
        }
    }
    
    switch (app->current_state) {
        case STATE_GRAPH_VIEW:
            // Normal navigation, handle "Menu Open" trigger (e.g., Space key)
            break;
            
        case STATE_MENU_OPEN:
            // Menu is visible, handle interactions with it
            break;
            
        case STATE_AWAITING_SELECTION:
            // Highlight nodes/edges for picking
            if (app->pending_command) {
                // Check if all parameters of type PARAM_TYPE_NODE_SELECTION or EDGE_SELECTION are filled
                bool all_filled = true;
                for (int i = 0; i < app->pending_command->num_params; i++) {
                    if ((app->pending_command->params[i].type == PARAM_TYPE_NODE_SELECTION || 
                         app->pending_command->params[i].type == PARAM_TYPE_EDGE_SELECTION) &&
                        app->pending_command->params[i].value.selection_id == -1) {
                        all_filled = false;
                        break;
                    }
                }
                
                if (all_filled) {
                    app->current_state = STATE_EXECUTING;
                }
            }
            break;
            
        case STATE_AWAITING_INPUT:
            // Handle floating forms
            break;
            
        case STATE_EXECUTING:
            if (app->pending_command && app->pending_command->execute) {
                printf("[State] Executing command: %s\n", app->pending_command->display_name);
                
                ExecutionContext exec_ctx;
                exec_ctx.current_graph = app->target_graph; // Use the graph in AppContext
                exec_ctx.params = app->pending_command->params;
                exec_ctx.num_params = app->pending_command->num_params;
                exec_ctx.update_visuals_callback = NULL; // To be set
                
                app->pending_command->execute(&exec_ctx);
                
                // Check if this command produced visual results
                if (app->pending_command->produces_visual_output) {
                    app->has_visual_results = true;
                    app->current_state = STATE_DISPLAY_RESULTS;
                } else {
                    // Reset after execution
                    app->pending_command = NULL;
                    app->current_state = STATE_GRAPH_VIEW;
                }
            }
            break;
            
        case STATE_DISPLAY_RESULTS:
            // Results are displayed. Wait for user to close/dismiss them.
            // For now, any keypress or click will return to graph view
            // TODO: Implement proper dismissal (e.g., ESC key, or "Close" button in UI)
            break;
    }
}

void handle_menu_selection(AppContext* app, MenuNode* selected_node) {
    if (selected_node->type == NODE_BRANCH) {
        app->active_menu_level = selected_node;
    } else if (selected_node->type == NODE_LEAF) {
        app->pending_command = selected_node->command;
        app->selection_step = 0;
        
        // Reset selection parameters
        for (int i = 0; i < app->pending_command->num_params; i++) {
            if (app->pending_command->params[i].type == PARAM_TYPE_NODE_SELECTION ||
                app->pending_command->params[i].type == PARAM_TYPE_EDGE_SELECTION) {
                app->pending_command->params[i].value.selection_id = -1;
            }
        }
        
        check_pending_command_requirements(app);
    }
}

void check_pending_command_requirements(AppContext* app) {
    if (!app->pending_command) return;
    
    bool needs_selection = false;
    bool has_numeric_input = false;
    int numeric_param_idx = -1;
    
    for (int i = 0; i < app->pending_command->num_params; i++) {
        ParameterType type = app->pending_command->params[i].type;
        if (type == PARAM_TYPE_NODE_SELECTION || type == PARAM_TYPE_EDGE_SELECTION) {
            needs_selection = true;
        } else if (type == PARAM_TYPE_FLOAT || type == PARAM_TYPE_INT) {
            has_numeric_input = true;
            numeric_param_idx = i;
        }
    }
    
    if (needs_selection) {
        app->current_state = STATE_AWAITING_SELECTION;
    } else if (has_numeric_input) {
        // TODO: Implement numeric input widget properly
        app->current_state = STATE_EXECUTING;
    } else {
        // No selection or numeric input needed, can execute directly
        app->current_state = STATE_EXECUTING;
    }
}
