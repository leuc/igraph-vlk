#include "state.h"
#include "../ui/sphere_menu.h"
#include <stdlib.h>
#include <igraph.h>

// Global or passed app context updater - stub for main loop integration
void update_app_state(AppContext* ctx) {
    // Central switch-statement for state transitions
    switch (ctx->current_state) {
        case STATE_GRAPH_VIEW:
            // Freely navigating the graph
            // Transition: User summons menu (e.g., key press or gesture) -> STATE_MENU_OPEN
            // Stub: ctx->current_state = STATE_MENU_OPEN; // Triggered by input handler
            break;

        case STATE_MENU_OPEN:
            // Sphere menu active, graph dimmed
            // Transition: User selects leaf -> check params, go to STATE_AWAITING_SELECTION or STATE_EXECUTING
            // or back to STATE_GRAPH_VIEW
            // Stub: ctx->current_state = STATE_AWAITING_SELECTION;
            break;

        case STATE_AWAITING_SELECTION:
            // User picks nodes/edges via raycast
            // Increment ctx->selection_step on pick
            // When enough selections: ctx->current_state = STATE_AWAITING_INPUT or STATE_EXECUTING
            ctx->selection_step++;  // Example increment (actual from picking.c)
            if (ctx->selection_step >= ctx->pending_command->num_params) {  // Pseudo-check
                ctx->current_state = STATE_EXECUTING;
            }
            break;

        case STATE_AWAITING_INPUT:
            // Handle sliders/text inputs
            // Transition to STATE_EXECUTING when complete
            // Stub
            ctx->current_state = STATE_EXECUTING;
            break;

        case STATE_EXECUTING:
            // Run igraph command, update visuals via callback
            // Transition back to STATE_GRAPH_VIEW on completion
            // Stub
            ctx->current_state = STATE_GRAPH_VIEW;
            break;

        default:
            ctx->current_state = STATE_GRAPH_VIEW;
            break;
    }
}

// Initialization function
void app_context_init(AppContext* ctx, igraph_t* graph, MenuNode* root_menu) {
    ctx->current_state = STATE_GRAPH_VIEW;
    ctx->root_menu = root_menu;
    ctx->active_menu_level = root_menu;
    ctx->pending_command = NULL;
    ctx->selection_step = 0;
    ctx->current_graph = graph;
    igraph_empty(&ctx->target_graph, 0, 0);  // Initialize empty target graph
}

// Cleanup function
void app_context_destroy(AppContext* ctx) {
    igraph_destroy(&ctx->target_graph);
}
