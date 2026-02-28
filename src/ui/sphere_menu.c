#include "ui/sphere_menu.h"
#include "graph/wrappers.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Helper function to create a menu node
static MenuNode* create_menu_node(const char* label, MenuNodeType type) {
    MenuNode* node = (MenuNode*)malloc(sizeof(MenuNode));
    node->label = strdup(label);
    node->type = type;
    node->icon_texture_id = -1;
    node->target_phi = 0.0f;
    node->target_theta = 0.0f;
    node->current_radius = 0.0f;
    node->target_radius = 1.0f;
    node->num_children = 0;
    node->children = NULL;
    node->command = NULL;
    return node;
}

// Helper function to create a command
static IgraphCommand* create_command(const char* id_name, const char* display_name, IgraphWrapperFunc execute, int num_params) {
    IgraphCommand* cmd = (IgraphCommand*)malloc(sizeof(IgraphCommand));
    cmd->id_name = strdup(id_name);
    cmd->display_name = strdup(display_name);
    cmd->icon_path = NULL;
    cmd->execute = execute;
    cmd->num_params = num_params;
    cmd->params = (CommandParameter*)malloc(sizeof(CommandParameter) * num_params);
    return cmd;
}

// Initialize the menu tree with sample data
void init_menu_tree(MenuNode* root) {
    // Create main menu
    MenuNode* main_menu = create_menu_node("Main", NODE_BRANCH);
    
    // Create Analysis submenu
    MenuNode* analysis_menu = create_menu_node("Analysis", NODE_BRANCH);
    
    // Create Paths submenu
    MenuNode* paths_menu = create_menu_node("Paths", NODE_BRANCH);
    
    // Create Visualize submenu
    MenuNode* visualize_menu = create_menu_node("Visualize", NODE_BRANCH);
    
    // Create Shortest Path command
    IgraphCommand* cmd_shortest_path = create_command("shortest_path", "Shortest Path", wrapper_shortest_path, 2);
    cmd_shortest_path->params[0] = (CommandParameter){.name="Source", .type=PARAM_TYPE_NODE_SELECTION, .value.selection_id=-1, .min_val=0, .max_val=0};
    cmd_shortest_path->params[1] = (CommandParameter){.name="Target", .type=PARAM_TYPE_NODE_SELECTION, .value.selection_id=-1, .min_val=0, .max_val=0};
    
    // Create PageRank command
    IgraphCommand* cmd_pagerank = create_command("pagerank", "PageRank", wrapper_pagerank, 0);

    // Create Betweenness command
    IgraphCommand* cmd_betweenness = create_command("betweenness", "Betweenness", wrapper_betweenness, 0);

    // Create leaf nodes
    MenuNode* shortest_path_node = create_menu_node("Shortest Path", NODE_LEAF);
    shortest_path_node->command = cmd_shortest_path;
    
    MenuNode* pagerank_node = create_menu_node("PageRank", NODE_LEAF);
    pagerank_node->command = cmd_pagerank;

    MenuNode* betweenness_node = create_menu_node("Betweenness", NODE_LEAF);
    betweenness_node->command = cmd_betweenness;

    // Build tree structure
    paths_menu->num_children = 1;
    paths_menu->children = (MenuNode**)malloc(sizeof(MenuNode*));
    paths_menu->children[0] = shortest_path_node;
    
    analysis_menu->num_children = 3;
    analysis_menu->children = (MenuNode**)malloc(sizeof(MenuNode*) * 3);
    analysis_menu->children[0] = paths_menu;
    analysis_menu->children[1] = pagerank_node;
    analysis_menu->children[2] = betweenness_node;
    
    visualize_menu->num_children = 0; // Placeholder for future commands
    visualize_menu->children = NULL;
    
    main_menu->num_children = 2;
    main_menu->children = (MenuNode**)malloc(sizeof(MenuNode*) * 2);
    main_menu->children[0] = analysis_menu;
    main_menu->children[1] = visualize_menu;
    
    // Copy to root (assuming root is pre-allocated)
    *root = *main_menu;
    free(main_menu);
}

// Destroy the menu tree recursively
void destroy_menu_tree(MenuNode* node) {
    if (node == NULL) return;
    
    if (node->type == NODE_BRANCH) {
        for (int i = 0; i < node->num_children; i++) {
            destroy_menu_tree(node->children[i]);
        }
        free(node->children);
    } else if (node->type == NODE_LEAF && node->command != NULL) {
        free((void*)node->command->id_name);
        free((void*)node->command->display_name);
        free((void*)node->command->params);
        free(node->command);
    }
    free((void*)node->label);
    free(node);
}

// Update menu animation and calculate spherical positions
void update_menu_animation(MenuNode* node, float delta_time) {
    if (node == NULL) return;
    
    // Smooth radius transition
    float speed = 8.0f;
    node->current_radius += (node->target_radius - node->current_radius) * speed * delta_time;
    
    // Distribute children on a sphere/arc
    if (node->num_children > 0) {
        float golden_ratio = (1.0f + sqrtf(5.0f)) / 2.0f;
        float angle_increment = 2.0f * M_PI * golden_ratio;

        for (int i = 0; i < node->num_children; i++) {
            // Fibonacci sphere distribution for child offsets
            float t = (float)i / node->num_children;
            float phi = acosf(1.0f - 2.0f * t);
            float theta = angle_increment * i;

            // Offset from parent's orientation
            node->children[i]->target_phi = node->target_phi + (phi - M_PI/2.0f) * 0.5f;
            node->children[i]->target_theta = node->target_theta + theta * 0.2f;
            
            // Set radius (only active level should be expanded)
            // For now, simple logic: if parent is at radius > 0.5, children start expanding
            node->children[i]->target_radius = (node->current_radius > 0.5f) ? 1.0f : 0.0f;

            update_menu_animation(node->children[i], delta_time);
        }
    }
}

// Generate Vulkan menu buffers (stub implementation)
void generate_vulkan_menu_buffers(MenuNode* node) {
    if (node == NULL) return;
    
    printf("[Menu] Generating Vulkan buffers for: %s (radius: %.2f)\n", node->label, node->current_radius);
    
    // Recursively generate buffers for children
    for (int i = 0; i < node->num_children; i++) {
        generate_vulkan_menu_buffers(node->children[i]);
    }
}

// Find a menu node by label (recursive search)
MenuNode* find_menu_node(MenuNode* root, const char* label) {
    if (root == NULL || label == NULL) return NULL;
    
    if (strcmp(root->label, label) == 0) {
        return root;
    }
    
    for (int i = 0; i < root->num_children; i++) {
        MenuNode* result = find_menu_node(root->children[i], label);
        if (result != NULL) {
            return result;
        }
    }
    
    return NULL;
}
