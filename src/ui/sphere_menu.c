#include "ui/sphere_menu.h"
#include "graph/wrappers.h"
#include "vulkan/renderer.h"
#include "vulkan/renderer_geometry.h"
#include "vulkan/utils.h"
#include "interaction/camera.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Maximum children per page before using outer radius
#define MAX_CHILDREN_PER_PAGE 15
#define MENU_PAGINATION_THRESHOLD 12

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

// Forward declaration
static void assign_menu_icons(MenuNode* node);

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

    assign_menu_icons(root);
}

// Helper to assign texture IDs recursively
static void assign_menu_icons(MenuNode* node) {
    if (node == NULL) return;
    
    static int next_icon_id = 0;
    node->icon_texture_id = next_icon_id++;
    
    for (int i = 0; i < node->num_children; i++) {
        assign_menu_icons(node->children[i]);
    }
}

// Destroy the menu tree recursively
void destroy_menu_tree(MenuNode* node) {
    if (node == NULL) return;
    
    if (node->type == NODE_BRANCH) {
        for (int i = 0; i < node->num_children; i++) {
            destroy_menu_tree(node->children[i]);
        }
        if (node->children) free(node->children);
    } else if (node->type == NODE_LEAF && node->command != NULL) {
        if (node->command->id_name) free((void*)node->command->id_name);
        if (node->command->display_name) free((void*)node->command->display_name);
        if (node->command->params) free((void*)node->command->params);
        free(node->command);
    }
    if (node->label) free((void*)node->label);
    // Note: Don't free(node) if it's the root allocated on stack or special way
}

// Update menu animation with smooth easing
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
            node->children[i]->target_radius = (node->current_radius > 0.5f) ? 1.0f : 0.0f;

            update_menu_animation(node->children[i], delta_time);
        }
    }
}

// Generate Vulkan menu buffers (instanced rendering)
void generate_vulkan_menu_buffers(MenuNode* node, Renderer* r, Camera* cam) {
    if (node == NULL) return;
    
    int capacity = 128;
    MenuInstance* instances = (MenuInstance*)malloc(sizeof(MenuInstance) * capacity);
    int instance_count = 0;
    
    // Stack-based traversal
    MenuNode** stack = (MenuNode**)malloc(sizeof(MenuNode*) * 256);
    int stack_top = 0;
    stack[stack_top++] = node;
    
    while (stack_top > 0) {
        MenuNode* current = stack[--stack_top];
        if (current == NULL) continue;
        
        if (current->current_radius > 0.01f) {
            if (instance_count >= capacity) {
                capacity *= 2;
                instances = (MenuInstance*)realloc(instances, sizeof(MenuInstance) * capacity);
            }
            
            // Calculate world position relative to camera
            float dist = 2.0f * current->current_radius;
            float phi = current->target_phi;
            float theta = current->target_theta;
            
            vec3 offset;
            offset[0] = dist * sinf(phi) * cosf(theta);
            offset[1] = dist * cosf(phi);
            offset[2] = dist * sinf(phi) * sinf(theta);
            
            // For now, place it in front of camera
            vec3 world_pos;
            glm_vec3_add(cam->pos, cam->front, world_pos);
            glm_vec3_add(world_pos, offset, world_pos);
            
            glm_vec3_copy(world_pos, instances[instance_count].worldPos);
            instances[instance_count].texCoord[0] = 0.5f;
            instances[instance_count].texCoord[1] = 0.5f;
            instances[instance_count].texId = (float)current->icon_texture_id;
            
            float s = 0.15f;
            instances[instance_count].scale[0] = s;
            instances[instance_count].scale[1] = s;
            instances[instance_count].scale[2] = s;
            
            glm_vec4_copy((vec4){1, 0, 0, 0}, instances[instance_count].rotation);
            
            instance_count++;
        }
        
        for (int i = 0; i < current->num_children; i++) {
            stack[stack_top++] = current->children[i];
        }
    }
    
    free(stack);
    
    if (instance_count > 0) {
        if (r->menuQuadVertexBuffer == VK_NULL_HANDLE) {
            QuadVertex qv[] = {
                {{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f}},
                {{ 0.5f, -0.5f, 0.0f}, {1.0f, 0.0f}},
                {{ 0.5f,  0.5f, 0.0f}, {1.0f, 1.0f}},
                {{-0.5f,  0.5f, 0.0f}, {0.0f, 1.0f}}
            };
            uint32_t qi[] = {0, 1, 2, 2, 3, 0};
            
            // Use createBuffer with data
            // Note: existing createBuffer in utils.c might need to be checked
            // Looking at renderer.c, createBuffer is used as:
            // createBuffer(device, physicalDevice, size, VK_BUFFER_USAGE_..., VK_MEMORY_PROPERTY_..., &buffer, &memory)
            // It doesn't seem to take data directly. It's followed by updateBuffer.
            
            createBuffer(r->device, r->physicalDevice, sizeof(qv),
                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         &r->menuQuadVertexBuffer, &r->menuQuadVertexBufferMemory);
            updateBuffer(r->device, r->menuQuadVertexBufferMemory, sizeof(qv), qv);
            
            createBuffer(r->device, r->physicalDevice, sizeof(qi),
                         VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         &r->menuQuadIndexBuffer, &r->menuQuadIndexBufferMemory);
            updateBuffer(r->device, r->menuQuadIndexBufferMemory, sizeof(qi), qi);
            r->menuQuadIndexCount = 6;
        }
        
        VkDeviceSize bufferSize = sizeof(MenuInstance) * instance_count;
        if (r->menuInstanceBuffer == VK_NULL_HANDLE) {
            createBuffer(r->device, r->physicalDevice, bufferSize,
                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         &r->menuInstanceBuffer, &r->menuInstanceBufferMemory);
        }
        // In a real app we might need to recreate if size changed, but for now just update
        updateBuffer(r->device, r->menuInstanceBufferMemory, bufferSize, instances);
        r->menuNodeCount = instance_count;
    }
    
    free(instances);
}

// Find a menu node by label
MenuNode* find_menu_node(MenuNode* root, const char* label) {
    if (root == NULL || label == NULL) return NULL;
    if (strcmp(root->label, label) == 0) return root;
    for (int i = 0; i < root->num_children; i++) {
        MenuNode* res = find_menu_node(root->children[i], label);
        if (res) return res;
    }
    return NULL;
}
