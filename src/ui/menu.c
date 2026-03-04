#include "ui/menu.h"
#include "graph/command_registry.h"
#include "interaction/camera.h"
#include "vulkan/renderer.h"
#include "vulkan/renderer_geometry.h"
#include "vulkan/text.h"
#include "vulkan/utils.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

extern FontAtlas globalAtlas;

// Helper function to create a menu node
static MenuNode *create_menu_node(const char *label, MenuNodeType type)
{
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
	node->is_expanded = false;
	return node;
}

// Helper function to create a command
static IgraphCommand *create_command(const char *id_name, const char *display_name, IgraphWrapperFunc execute, int num_params)
{
	IgraphCommand *cmd = (IgraphCommand *)malloc(sizeof(IgraphCommand));
	cmd->id_name = strdup(id_name);
	cmd->display_name = strdup(display_name);
	cmd->icon_path = NULL;
	cmd->execute = execute;
	cmd->num_params = num_params;
	cmd->params = (CommandParameter *)malloc(sizeof(CommandParameter) * num_params);
	cmd->produces_visual_output = false;
	cmd->cmd_def = NULL; // Will be set for data-driven commands
	return cmd;
}

// Forward declarations
static void assign_menu_icons(MenuNode *node);

// Initialize the menu tree from the command registry
void init_menu_tree(MenuNode *root)
{
	// Initialize root
	root->label = strdup("Main");
	root->type = NODE_BRANCH;
	root->icon_texture_id = -1;
	root->target_phi = 0.0f;
	root->target_theta = 0.0f;
	root->current_radius = 0.0f;
	root->target_radius = 1.0f;
	root->num_children = 0;
	root->children = NULL;
	root->command = NULL;
	root->is_expanded = true;
	root->hovered = false;

	for (int i = 0; i < g_command_registry_size; i++) {
		const CommandDef *cmd_def = &g_command_registry[i];

		MenuNode *current_parent = root;

		// 1. Process Path (Create/Traverse Folders)
		if (cmd_def->category_path && strlen(cmd_def->category_path) > 0) {
			char path_copy[256];
			strncpy(path_copy, cmd_def->category_path, sizeof(path_copy) - 1);
			path_copy[255] = '\0';

			char *token = strtok(path_copy, "/");
			while (token != NULL) {
				MenuNode *branch = NULL;

				// Search for existing folder
				for (int c = 0; c < current_parent->num_children; c++) {
					if (current_parent->children[c]->type == NODE_BRANCH && strcmp(current_parent->children[c]->label, token) == 0) {
						branch = current_parent->children[c];
						break;
					}
				}

				// Create folder if it doesn't exist
				if (branch == NULL) {
					branch = create_menu_node(token, NODE_BRANCH);
					current_parent->children = (MenuNode **)realloc(current_parent->children, sizeof(MenuNode *) * (current_parent->num_children + 1));
					current_parent->children[current_parent->num_children] = branch;
					current_parent->num_children++;
				}

				// Move down into the folder
				current_parent = branch;
				token = strtok(NULL, "/");
			}
		}

		// 2. Path complete. Attach the Leaf Node.
		MenuNode *leaf = create_menu_node(cmd_def->display_name, NODE_LEAF);
		leaf->command = create_command(cmd_def->command_id, cmd_def->display_name, NULL, 0);
		leaf->command->cmd_def = cmd_def;

		current_parent->children = (MenuNode **)realloc(current_parent->children, sizeof(MenuNode *) * (current_parent->num_children + 1));
		current_parent->children[current_parent->num_children] = leaf;
		current_parent->num_children++;
	}

	// Assign icons globally
	assign_menu_icons(root);
}

// Destroy the menu tree recursively
void destroy_menu_tree(MenuNode *node)
{
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

	// Also free command if it exists
	if (node->command) {
		if (node->command->id_name)
			free((void *)node->command->id_name);
		if (node->command->display_name)
			free((void *)node->command->display_name);
		if (node->command->icon_path)
			free((void *)node->command->icon_path);
		if (node->command->params)
			free(node->command->params);
		free(node->command);
	}
}

// Update menu animation with smooth easing
// Uses recursive vertical tree layout
static void update_menu_layout_recursive(MenuNode *node, float delta_time, int depth, float *current_y)
{
	if (node == NULL)
		return;

	float speed = 8.0f;
	float diff = node->target_radius - node->current_radius;
	if (fabsf(diff) < 0.001f) {
		node->current_radius = node->target_radius;
	} else {
		node->current_radius += diff * speed * delta_time;
	}

	// Position THIS node
	node->target_phi = (float)(depth - 1) * 0.12f;
	node->target_theta = *current_y;

	// Only consume vertical space if this node is expanding/open
	// This ensures smooth stacking animation
	*current_y -= 0.12f * node->current_radius;

	// Recursively update children
	if (node->num_children > 0) {
		for (int i = 0; i < node->num_children; i++) {
			MenuNode *child = node->children[i];

			// Children only expand if parent is mostly open AND expanded
			child->target_radius = (node->current_radius > 0.5f && node->is_expanded) ? 1.0f : 0.0f;

			// Always recurse if parent is visible to ensure children close/open correctly
			if (node->current_radius > 0.001f) {
				update_menu_layout_recursive(child, delta_time, depth + 1, current_y);
			} else {
				// Parent is fully closed, snap child state
				child->current_radius = 0.0f;
				child->target_radius = 0.0f;
				child->target_phi = 0.0f;
				child->target_theta = 0.0f;
			}
		}
	}
}

void update_menu_animation(MenuNode *node, float delta_time)
{
	if (node == NULL)
		return;

	// Start tracking layout from the top
	float start_y = 0.0f;

	// The root node itself doesn't need indentation, its children start at depth 1
	update_menu_layout_recursive(node, delta_time, 1, &start_y);
}

// Generate Vulkan menu buffers (instanced rendering)
void generate_vulkan_menu_buffers(MenuNode *node, Renderer *r, vec3 cam_pos, vec3 cam_front, vec3 cam_up)
{
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
	MenuInstance *instances = (MenuInstance *)malloc(sizeof(MenuInstance) * capacity);
	int instance_count = 0;

	// Label rendering
	int label_capacity = 1024;
	LabelInstance *label_instances = (LabelInstance *)malloc(sizeof(LabelInstance) * label_capacity);
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
				instances = (MenuInstance *)realloc(instances, sizeof(MenuInstance) * capacity);
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
			float padding_x = 0.08f;		 // Tight padding
			float fixed_height = 0.09f;		 // Consistent height

			// Calculate total width for dynamic sizing (even if no label)
			float total_w = 0.0f;
			int label_len = 0;
			if (current->label) {
				label_len = strlen(current->label);
				for (int i = 0; i < label_len; i++) {
					unsigned char c = current->label[i];
					CharInfo *ci = (c < 128) ? &globalAtlas.chars[c] : &globalAtlas.chars[32];
					total_w += ci->xadvance;
				}
			}

			// Use dynamic bounds: width based on text + padding, height fixed
			float box_width = (total_w * world_text_scale) + padding_x;
			instances[instance_count].scale[0] = box_width * current->current_radius;
			instances[instance_count].scale[1] = fixed_height * current->current_radius;
			instances[instance_count].scale[2] = 1.0f;
			instances[instance_count].hovered = current->hovered ? 1.0f : 0.0f;

			// Center the background quad around the text anchor
			if (current->label) {
				vec3 offset;
				glm_vec3_scale(right, box_width * 0.5f, offset);
				glm_vec3_add(instances[instance_count].worldPos, offset, instances[instance_count].worldPos);
			}

			// Calculate rotation quaternion to align quad (default XY plane) with camera
			// Camera orientation: front is Z (backwards), right is X, up is Y
			// We want the quad's +Z to face the camera's -front
			mat3 rot_mat;
			vec3 neg_front;
			glm_vec3_negate_to(cam_front, neg_front);
			glm_mat3_identity(rot_mat);
			glm_mat3_copy((mat3){{right[0], right[1], right[2]}, {up[0], up[1], up[2]}, {neg_front[0], neg_front[1], neg_front[2]}}, rot_mat);
			glm_mat3_quat(rot_mat, instances[instance_count].rotation);

			// Generate label instances
			if (current->label && current->current_radius > 0.01f) { // Render text as long as node is visible
				int len = strlen(current->label);
				if (label_count + len >= label_capacity) {
					label_capacity *= 2;
					label_instances = (LabelInstance *)realloc(label_instances, sizeof(LabelInstance) * label_capacity);
				}

				float x_cursor = 0.0f;
				// x_cursor = -total_w * 0.5f; // old centering
				x_cursor = (padding_x * 0.4f) / world_text_scale; // Left-aligned with a small margin

				for (int i = 0; i < len; i++) {
					unsigned char c = current->label[i];
					CharInfo *ci = (c < 128) ? &globalAtlas.chars[c] : &globalAtlas.chars[32];

					// Offset text forward from the quad slightly to prevent z-fighting
					vec3 label_pos;
					glm_vec3_copy(world_pos, label_pos);
					vec3 forward_off, down_off;
					glm_vec3_scale(cam_front, -0.002f, forward_off); // Move slightly towards camera
					glm_vec3_scale(up, -0.015f, down_off);			 // Centering adjustment
					glm_vec3_add(label_pos, forward_off, label_pos);
					glm_vec3_add(label_pos, down_off, label_pos);

					glm_vec3_copy(label_pos, label_instances[label_count].nodePos);
					label_instances[label_count].charRect[0] = x_cursor + ci->x0;
					label_instances[label_count].charRect[1] = ci->y0;
					label_instances[label_count].charRect[2] = x_cursor + ci->x1;
					label_instances[label_count].charRect[3] = ci->y1;
					label_instances[label_count].charUV[0] = ci->u0;
					label_instances[label_count].charUV[1] = ci->v0;
					label_instances[label_count].charUV[2] = ci->u1;
					label_instances[label_count].charUV[3] = ci->v1;
					// Scale orientation vectors by current_radius so text shrinks/vanishes with the quad
					float dynamic_scale = world_text_scale * current->current_radius;
					glm_vec3_scale(right, dynamic_scale, label_instances[label_count].right);
					glm_vec3_scale(up, dynamic_scale, label_instances[label_count].up);

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
			QuadVertex qv[] = {{{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f}}, {{0.5f, -0.5f, 0.0f}, {1.0f, 0.0f}}, {{0.5f, 0.5f, 0.0f}, {1.0f, 1.0f}}, {{-0.5f, 0.5f, 0.0f}, {0.0f, 1.0f}}};
			uint32_t qi[] = {0, 1, 2, 2, 3, 0};

			createBuffer(r->device, r->physicalDevice, sizeof(qv), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->menuQuadVertexBuffer, &r->menuQuadVertexBufferMemory);
			updateBuffer(r->device, r->menuQuadVertexBufferMemory, sizeof(qv), qv);

			createBuffer(r->device, r->physicalDevice, sizeof(qi), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->menuQuadIndexBuffer, &r->menuQuadIndexBufferMemory);
			updateBuffer(r->device, r->menuQuadIndexBufferMemory, sizeof(qi), qi);
			r->menuQuadIndexCount = 6;
		}

		VkDeviceSize bufferSize = sizeof(MenuInstance) * instance_count;
		if (r->menuInstanceBuffer != VK_NULL_HANDLE) {
			vkDeviceWaitIdle(r->device); // Ensure GPU is done with it
			vkDestroyBuffer(r->device, r->menuInstanceBuffer, NULL);
			vkFreeMemory(r->device, r->menuInstanceBufferMemory, NULL);
		}
		createBuffer(r->device, r->physicalDevice, bufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->menuInstanceBuffer, &r->menuInstanceBufferMemory);
		updateBuffer(r->device, r->menuInstanceBufferMemory, bufferSize, instances);
		r->menuNodeCount = instance_count;

		// Update label buffer
		if (label_count > 0) {
			VkDeviceSize labelBufferSize = sizeof(LabelInstance) * label_count;
			if (r->menuTextInstanceBuffer != VK_NULL_HANDLE) {
				vkDestroyBuffer(r->device, r->menuTextInstanceBuffer, NULL);
				vkFreeMemory(r->device, r->menuTextInstanceBufferMemory, NULL);
			}
			createBuffer(r->device, r->physicalDevice, labelBufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->menuTextInstanceBuffer, &r->menuTextInstanceBufferMemory);
			updateBuffer(r->device, r->menuTextInstanceBufferMemory, labelBufferSize, label_instances);
			r->menuTextCharCount = label_count;
		}
	}

	free(instances);
	free(label_instances);
}

MenuNode *find_menu_node(MenuNode *root, const char *label)
{
	if (root == NULL || label == NULL)
		return NULL;
	if (strcmp(root->label, label) == 0)
		return root;
	for (int i = 0; i < root->num_children; i++) {
		MenuNode *res = find_menu_node(root->children[i], label);
		if (res)
			return res;
	}
	return NULL;
}

// Static helper to assign icons to nodes recursively
static void assign_menu_icons(MenuNode *node)
{
	if (node == NULL)
		return;
	static int next_icon_id = 0;
	node->icon_texture_id = next_icon_id++;
	for (int i = 0; i < node->num_children; i++) {
		assign_menu_icons(node->children[i]);
	}
}
