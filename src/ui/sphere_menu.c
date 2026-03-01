#include "ui/sphere_menu.h"
#include "graph/wrappers.h"
#include "vulkan/renderer.h"
#include "vulkan/renderer_geometry.h"
#include "vulkan/text.h"
#include "vulkan/utils.h"
#include "interaction/camera.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

extern FontAtlas globalAtlas;

// Helper function to create a menu node
static MenuNode *create_menu_node(const char *label, MenuNodeType type) {
	MenuNode *node = (MenuNode *)malloc(sizeof(MenuNode));
	node->label = strdup(label);
	node->type = type;
	node->icon_texture_id = -1;
	node->target_phi = 0.0f;
	node->target_theta = 0.0f;
	node->current_radius = 0.0f;
	node->target_radius = 0.0f; // Start collapsed
	node->num_children = 0;
	node->children = NULL;
	node->command = NULL;
	return node;
}

// Helper function to create a command
static IgraphCommand *create_command(const char *id_name,
									 const char *display_name,
									 IgraphWrapperFunc execute,
									 int num_params) {
	IgraphCommand *cmd = (IgraphCommand *)malloc(sizeof(IgraphCommand));
	cmd->id_name = strdup(id_name);
	cmd->display_name = strdup(display_name);
	cmd->icon_path = NULL;
	cmd->execute = execute;
	cmd->num_params = num_params;
	cmd->params =
		(CommandParameter *)malloc(sizeof(CommandParameter) * num_params);
	cmd->produces_visual_output = false;
	return cmd;
}

// Forward declaration
static void assign_menu_icons(MenuNode *node);

// Initialize the menu tree
void init_menu_tree(MenuNode *root) {
	// Create main menu
	MenuNode *main_menu = create_menu_node("Main", NODE_BRANCH);

	// Create Analysis submenu
	MenuNode *analysis_menu = create_menu_node("Analysis", NODE_BRANCH);

	// Create Paths submenu
	MenuNode *paths_menu = create_menu_node("Paths", NODE_BRANCH);

	// Create Visualize submenu
	MenuNode *visualize_menu = create_menu_node("Visualize", NODE_BRANCH);

	// Create Shortest Path command
	IgraphCommand *cmd_shortest_path = create_command(
		"shortest_path", "Shortest Path", wrapper_shortest_path, 2);
	cmd_shortest_path->params[0] = (CommandParameter){
		.name = "Source",
		.type = PARAM_TYPE_NODE_SELECTION,
		.value.selection_id = -1,
		.min_val = 0,
		.max_val = 0};
	cmd_shortest_path->params[1] = (CommandParameter){
		.name = "Target",
		.type = PARAM_TYPE_NODE_SELECTION,
		.value.selection_id = -1,
		.min_val = 0,
		.max_val = 0};

	// Create PageRank command
	IgraphCommand *cmd_pagerank =
		create_command("pagerank", "PageRank", wrapper_pagerank, 0);

	// Create Betweenness command
	IgraphCommand *cmd_betweenness =
		create_command("betweenness", "Betweenness", wrapper_betweenness, 0);

	// Create leaf nodes
	MenuNode *shortest_path_node = create_menu_node("Shortest Path", NODE_LEAF);
	shortest_path_node->command = cmd_shortest_path;

	MenuNode *pagerank_node = create_menu_node("PageRank", NODE_LEAF);
	pagerank_node->command = cmd_pagerank;

	MenuNode *betweenness_node = create_menu_node("Betweenness", NODE_LEAF);
	betweenness_node->command = cmd_betweenness;

	// Build tree structure
	paths_menu->num_children = 1;
	paths_menu->children = (MenuNode **)malloc(sizeof(MenuNode *));
	paths_menu->children[0] = shortest_path_node;

	analysis_menu->num_children = 3;
	analysis_menu->children = (MenuNode **)malloc(sizeof(MenuNode *) * 3);
	analysis_menu->children[0] = paths_menu;
	analysis_menu->children[1] = pagerank_node;
	analysis_menu->children[2] = betweenness_node;

	visualize_menu->num_children = 0;
	visualize_menu->children = NULL;

	main_menu->num_children = 2;
	main_menu->children = (MenuNode **)malloc(sizeof(MenuNode *) * 2);
	main_menu->children[0] = analysis_menu;
	main_menu->children[1] = visualize_menu;

	// Copy to root (assuming root is pre-allocated)
	*root = *main_menu;
	free(main_menu);

	assign_menu_icons(root);
}

static void assign_menu_icons(MenuNode *node) {
	if (node == NULL)
		return;
	static int next_icon_id = 0;
	node->icon_texture_id = next_icon_id++;
	for (int i = 0; i < node->num_children; i++) {
		assign_menu_icons(node->children[i]);
	}
}

// Destroy the menu tree recursively
void destroy_menu_tree(MenuNode *node) {
	if (node == NULL)
		return;
	if (node->type == NODE_BRANCH) {
		for (int i = 0; i < node->num_children; i++) {
			destroy_menu_tree(node->children[i]);
		}
		if (node->children)
			free(node->children);
	}

	if (node->label)
		free((void *)node->label);
}

// Update menu animation with smooth easing
// Uses recursive vertical tree layout
static void update_menu_layout_recursive(MenuNode *node, float delta_time, int depth, float *current_y) {
	if (node == NULL)
		return;

	float speed = 8.0f;
	float diff = node->target_radius - node->current_radius;
	if (fabsf(diff) < 0.001f) {
		node->current_radius = node->target_radius;
	} else {
		node->current_radius += diff * speed * delta_time;
	}

	// If a parent is closing, children must start closing immediately
	if (node->target_radius < 0.001f) {
		for (int i = 0; i < node->num_children; i++) {
			node->children[i]->target_radius = 0.0f;
		}
	}

	if (node->num_children > 0) {
		for (int i = 0; i < node->num_children; i++) {
			MenuNode *child = node->children[i];

			// Use depth for X-axis indentation
			child->target_phi = (float)depth * 0.15f;
			
			// Use the globally tracked current_y for Y-axis vertical position
			child->target_theta = *current_y;
			
			// Ensure the screen updates its layout vertically as items expand/collapse
			// Using current_radius makes the stacking dynamic and animated
			*current_y -= 0.12f * child->current_radius;

			child->target_radius =
				(node->current_radius > 0.5f && node->target_radius > 0.5f) ? 1.0f : 0.0f;

			// Pass the pointer and increment depth for the next level
			// Continue recursion even if closing to ensure smooth collapse and offset reset
			if (child->target_radius > 0.0f || child->current_radius > 0.001f) {
				update_menu_layout_recursive(child, delta_time, depth + 1, current_y);
			} else {
				// Animation done and closed, reset position offsets
				child->target_phi = 0.0f;
				child->target_theta = 0.0f;
				child->current_radius = 0.0f;
			}
		}
	}
}

void update_menu_animation(MenuNode *node, float delta_time) {
    if (node == NULL) return;
    
    // Start tracking layout from the top
    float start_y = 0.0f; 
    
    // The root node itself doesn't need indentation, its children start at depth 1
    update_menu_layout_recursive(node, delta_time, 1, &start_y);
}

// Generate Vulkan menu buffers (instanced rendering)
void generate_vulkan_menu_buffers(MenuNode *node,
								  Renderer *r,
								  vec3 cam_pos,
								  vec3 cam_front,
								  vec3 cam_up) {
	if (node == NULL)
		return;

	// Fully vanish after closing animation
	// Reset counts to 0 once we're collapsed so nothing is drawn
	if (node->target_radius == 0.0f && node->current_radius < 0.005f) {
		r->menuNodeCount = 0;
		r->menuTextCharCount = 0;
		return;
	}

	int capacity = 128;
	MenuInstance *instances =
		(MenuInstance *)malloc(sizeof(MenuInstance) * capacity);
	int instance_count = 0;

	// Label rendering
	int label_capacity = 1024;
	LabelInstance *label_instances =
		(LabelInstance *)malloc(sizeof(LabelInstance) * label_capacity);
	int label_count = 0;

	// Stack-based traversal
	MenuNode **stack = (MenuNode **)malloc(sizeof(MenuNode *) * 256);
	int stack_top = 0;
	stack[stack_top++] = node;

	// Basis vectors for billboarding (using cam_front as camera front)
	vec3 right, up;
    glm_vec3_cross(cam_front, cam_up, right);
	glm_vec3_normalize(right);
	glm_vec3_cross(right, cam_front, up);
	glm_vec3_normalize(up);

	while (stack_top > 0) {
		MenuNode *current = stack[--stack_top];
		if (current == NULL)
			continue;

		// Render every visible node except the root container if it's at (0,0)
		if (current->current_radius > 0.01f) {
			if (instance_count >= capacity) {
				capacity *= 2;
				instances = (MenuInstance *)realloc(
					instances, sizeof(MenuInstance) * capacity);
			}

			// Simple billboard position: cam_pos + distance*front + x*right + y*up
            // Offset adjusted to be a bit more centered (-0.8m) and further away (2.5m)
			float x_off = current->target_phi - 0.8f;
			float y_off = current->target_theta;

			vec3 world_pos;
			glm_vec3_copy(cam_pos, world_pos);

			vec3 f_part, r_part, u_part;
			glm_vec3_scale(cam_front, 2.5f, f_part); // 2.5 meters away
			glm_vec3_scale(right, x_off, r_part);
			glm_vec3_scale(up, y_off, u_part);

			glm_vec3_add(world_pos, f_part, world_pos);
			glm_vec3_add(world_pos, r_part, world_pos);
			glm_vec3_add(world_pos, u_part, world_pos);

			// 2D tree layout - no spherical scaling needed
			// world_pos already contains the correct flat position

			glm_vec3_copy(world_pos, instances[instance_count].worldPos);
			instances[instance_count].texCoord[0] = 0.0f;
			instances[instance_count].texCoord[1] = 0.0f;
			instances[instance_count].texId = (float)current->icon_texture_id;

			// Dynamic box sizing based on text - Tightened to match font
			float world_text_scale = 0.003f; // Font bake size (32px) to world units
			float padding_x = 0.08f;   // Tight padding
			float fixed_height = 0.09f; // Consistent height

			// Calculate total width for dynamic sizing (even if no label)
			float total_w = 0.0f;
			int label_len = 0;
			if (current->label) {
				label_len = strlen(current->label);
				for (int i = 0; i < label_len; i++) {
					unsigned char c = current->label[i];
					CharInfo *ci = (c < 128) ? &globalAtlas.chars[c]
											 : &globalAtlas.chars[32];
					total_w += ci->xadvance;
				}
			}

			// Use dynamic bounds: width based on text + padding, height fixed
			float box_width = (total_w * world_text_scale) + padding_x;
			instances[instance_count].scale[0] = box_width * current->current_radius; 
			instances[instance_count].scale[1] = fixed_height * current->current_radius;
			instances[instance_count].scale[2] = 1.0f; 

			// Center the background quad around the text anchor
			if (current->label) {
				vec3 offset;
				glm_vec3_scale(right, box_width * 0.5f, offset);
				glm_vec3_add(instances[instance_count].worldPos, offset,
							 instances[instance_count].worldPos);
			}

            // Calculate rotation quaternion to align quad (default XY plane) with camera
            // Camera orientation: front is Z (backwards), right is X, up is Y
            // We want the quad's +Z to face the camera's -front
            mat3 rot_mat;
            vec3 neg_front;
            glm_vec3_negate_to(cam_front, neg_front);
            glm_mat3_identity(rot_mat);
            glm_mat3_copy((mat3){
                {right[0], right[1], right[2]},
                {up[0],    up[1],    up[2]},
                {neg_front[0], neg_front[1], neg_front[2]}
            }, rot_mat);
            glm_mat3_quat(rot_mat, instances[instance_count].rotation);

			// Generate label instances
			if (current->label && current->current_radius > 0.8f) { // Render text when open
				int len = strlen(current->label);
				if (label_count + len >= label_capacity) {
					label_capacity *= 2;
					label_instances = (LabelInstance *)realloc(
						label_instances,
						sizeof(LabelInstance) * label_capacity);
				}

				float x_cursor = 0.0f;
				x_cursor = -total_w * 0.5f;

				for (int i = 0; i < len; i++) {
					unsigned char c = current->label[i];
					CharInfo *ci = (c < 128) ? &globalAtlas.chars[c]
											 : &globalAtlas.chars[32];

					// Offset text forward from the quad slightly to prevent z-fighting
					vec3 label_pos;
					glm_vec3_copy(world_pos, label_pos);
					vec3 forward_off, down_off;
					glm_vec3_scale(cam_front, -0.002f, forward_off); // Move slightly towards camera
					glm_vec3_scale(up, -0.012f, down_off); // Centering adjustment
					glm_vec3_add(label_pos, forward_off, label_pos);
					glm_vec3_add(label_pos, down_off, label_pos);

					glm_vec3_copy(label_pos, label_instances[label_count].nodePos);
					label_instances[label_count].charRect[0] = (x_cursor + ci->x0) * world_text_scale;
					label_instances[label_count].charRect[1] = ci->y0 * world_text_scale;
					label_instances[label_count].charRect[2] = (x_cursor + ci->x1) * world_text_scale;
					label_instances[label_count].charRect[3] = ci->y1 * world_text_scale;
					label_instances[label_count].charUV[0] = ci->u0;
					label_instances[label_count].charUV[1] = ci->v0;
					label_instances[label_count].charUV[2] = ci->u1;
					label_instances[label_count].charUV[3] = ci->v1;
					// Use fixed orientation vectors captured at spawn for correct text billboarding
					glm_vec3_copy(right, label_instances[label_count].right);
					glm_vec3_copy(up, label_instances[label_count].up);

					x_cursor += ci->xadvance;
					label_count++;
				}
			}

			instance_count++;
		}

		for (int i = 0; i < current->num_children; i++) {
			stack[stack_top++] = current->children[i];
		}
	}

	free(stack);

	if (instance_count > 0) {
		if (r->menuQuadVertexBuffer == VK_NULL_HANDLE) {
			QuadVertex qv[] = {{{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f}},
							   {{0.5f, -0.5f, 0.0f}, {1.0f, 0.0f}},
							   {{0.5f, 0.5f, 0.0f}, {1.0f, 1.0f}},
							   {{-0.5f, 0.5f, 0.0f}, {0.0f, 1.0f}}};
			uint32_t qi[] = {0, 1, 2, 2, 3, 0};

			createBuffer(r->device, r->physicalDevice, sizeof(qv),
						 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
						 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
							 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
						 &r->menuQuadVertexBuffer,
						 &r->menuQuadVertexBufferMemory);
			updateBuffer(r->device, r->menuQuadVertexBufferMemory, sizeof(qv),
						 qv);

			createBuffer(r->device, r->physicalDevice, sizeof(qi),
						 VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
						 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
							 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
						 &r->menuQuadIndexBuffer,
						 &r->menuQuadIndexBufferMemory);
			updateBuffer(r->device, r->menuQuadIndexBufferMemory, sizeof(qi),
						 qi);
			r->menuQuadIndexCount = 6;
		}

		VkDeviceSize bufferSize = sizeof(MenuInstance) * instance_count;
		if (r->menuInstanceBuffer != VK_NULL_HANDLE) {
			vkDeviceWaitIdle(r->device); // Ensure GPU is done with it
			vkDestroyBuffer(r->device, r->menuInstanceBuffer, NULL);
			vkFreeMemory(r->device, r->menuInstanceBufferMemory, NULL);
		}
		createBuffer(r->device, r->physicalDevice, bufferSize,
					 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
					 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
						 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
					 &r->menuInstanceBuffer, &r->menuInstanceBufferMemory);
		updateBuffer(r->device, r->menuInstanceBufferMemory, bufferSize,
					 instances);
		r->menuNodeCount = instance_count;

		// Update label buffer
		if (label_count > 0) {
			VkDeviceSize labelBufferSize = sizeof(LabelInstance) * label_count;
			if (r->menuTextInstanceBuffer != VK_NULL_HANDLE) {
				vkDestroyBuffer(r->device, r->menuTextInstanceBuffer, NULL);
				vkFreeMemory(r->device, r->menuTextInstanceBufferMemory, NULL);
			}
			createBuffer(r->device, r->physicalDevice, labelBufferSize,
						 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
						 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
							 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
						 &r->menuTextInstanceBuffer,
						 &r->menuTextInstanceBufferMemory);
			updateBuffer(r->device, r->menuTextInstanceBufferMemory,
						 labelBufferSize, label_instances);
			r->menuTextCharCount = label_count;
		}
	}

	free(instances);
	free(label_instances);
}

MenuNode* find_menu_node(MenuNode* root, const char* label) {
    if (root == NULL || label == NULL) return NULL;
    if (strcmp(root->label, label) == 0) return root;
    for (int i = 0; i < root->num_children; i++) {
        MenuNode* res = find_menu_node(root->children[i], label);
        if (res) return res;
    }
    return NULL;
}
