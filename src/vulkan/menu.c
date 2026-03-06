#include "vulkan/menu.h"
#include "vulkan/renderer_geometry.h"
#include "vulkan/text.h"
#include "vulkan/utils.h"
#include <stdlib.h>
#include <string.h>

extern FontAtlas globalAtlas;

void generate_vulkan_menu_buffers(MenuNode *node, Renderer *r, const SpatialBasis *basis)
{
	if (node == NULL)
		return;

	if (node->target_radius == 0.0f && node->current_radius < 0.005f) {
		r->menuNodeCount = 0;
		r->menuTextCharCount = 0;
		return;
	}

	int capacity = 128;
	MenuInstance *instances = (MenuInstance *)malloc(sizeof(MenuInstance) * capacity);
	int instance_count = 0;

	int label_capacity = 1024;
	LabelInstance *label_instances = (LabelInstance *)malloc(sizeof(LabelInstance) * label_capacity);
	int label_count = 0;

	MenuNode **stack = (MenuNode **)malloc(sizeof(MenuNode *) * 256);
	int stack_top = 0;
	stack[stack_top++] = node;

	while (stack_top > 0) {
		MenuNode *current = stack[--stack_top];
		if (current == NULL)
			continue;

		if (current->current_radius > 0.01f) {
			if (instance_count >= capacity) {
				capacity *= 2;
				instances = (MenuInstance *)realloc(instances, sizeof(MenuInstance) * capacity);
			}

			glm_vec3_copy(current->quad_center_pos, instances[instance_count].worldPos);
			instances[instance_count].texCoord[0] = 0.0f;
			instances[instance_count].texCoord[1] = 0.0f;
			instances[instance_count].texId = (float)current->icon_texture_id;

			instances[instance_count].scale[0] = current->box_width * current->current_radius;
			instances[instance_count].scale[1] = current->box_height * current->current_radius;
			instances[instance_count].scale[2] = 1.0f;
			instances[instance_count].hovered = current->hovered ? 1.0f : 0.0f;

			mat3 rot_mat;
			vec3 front_copy, neg_front;
			memcpy(front_copy, basis->front, sizeof(vec3));
			glm_vec3_negate_to(front_copy, neg_front);
			glm_mat3_identity(rot_mat);
			glm_mat3_copy((mat3){{current->right_vec[0], current->right_vec[1], current->right_vec[2]}, {current->up_vec[0], current->up_vec[1], current->up_vec[2]}, {neg_front[0], neg_front[1], neg_front[2]}}, rot_mat);
			glm_mat3_quat(rot_mat, instances[instance_count].rotation);

			if (current->label) {
				int len = strlen(current->label);
				if (label_count + len >= label_capacity) {
					label_capacity *= 2;
					label_instances = (LabelInstance *)realloc(label_instances, sizeof(LabelInstance) * label_capacity);
				}

				float world_text_scale = 0.003f;
				float x_cursor = (0.08f * 0.4f) / world_text_scale;

				for (int i = 0; i < len; i++) {
					unsigned char c = current->label[i];
					CharInfo *ci = (c < 128) ? &globalAtlas.chars[c] : &globalAtlas.chars[32];

					vec3 label_pos;
					glm_vec3_copy(current->text_anchor_pos, label_pos);
					vec3 forward_off, down_off;
					glm_vec3_scale(front_copy, -0.002f, forward_off);
					glm_vec3_scale(current->up_vec, -0.015f, down_off);
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

					float dynamic_scale = world_text_scale * current->current_radius;
					glm_vec3_scale(current->right_vec, dynamic_scale, label_instances[label_count].right);
					glm_vec3_scale(current->up_vec, dynamic_scale, label_instances[label_count].up);

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
			vkDeviceWaitIdle(r->device);
			vkDestroyBuffer(r->device, r->menuInstanceBuffer, NULL);
			vkFreeMemory(r->device, r->menuInstanceBufferMemory, NULL);
		}
		createBuffer(r->device, r->physicalDevice, bufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->menuInstanceBuffer, &r->menuInstanceBufferMemory);
		updateBuffer(r->device, r->menuInstanceBufferMemory, bufferSize, instances);
		r->menuNodeCount = instance_count;

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
