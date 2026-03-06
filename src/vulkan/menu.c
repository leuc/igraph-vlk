#include "vulkan/menu.h"
#include "vulkan/renderer_geometry.h"
#include "vulkan/text.h"
#include "vulkan/utils.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

extern FontAtlas globalAtlas;

static float calculate_text_width(const char *text)
{
	if (!text)
		return 0.0f;

	int len = strlen(text);
	float x_cursor = 0.0f;

	for (int i = 0; i < len; i++) {
		unsigned char c = text[i];
		CharInfo *ci = (c < 128) ? &globalAtlas.chars[c] : &globalAtlas.chars[32];
		x_cursor += ci->xadvance;
	}

	float world_text_scale = 0.003f;
	return x_cursor * world_text_scale;
}

static void render_text_at_position(MenuNode *node, const char *text, vec3 base_pos, int *label_count, LabelInstance **label_instances, int *label_capacity)
{
	if (!text)
		return;

	int len = strlen(text);
	if (*label_count + len >= *label_capacity) {
		*label_capacity *= 2;
		*label_instances = (LabelInstance *)realloc(*label_instances, sizeof(LabelInstance) * *label_capacity);
	}

	float world_text_scale = 0.003f;
	float x_cursor = 0.0f;

	vec3 front;
	glm_vec3_cross(node->up_vec, node->right_vec, front);

	for (int i = 0; i < len; i++) {
		unsigned char c = text[i];
		CharInfo *ci = (c < 128) ? &globalAtlas.chars[c] : &globalAtlas.chars[32];

		vec3 label_pos;
		glm_vec3_copy(base_pos, label_pos);
		vec3 forward_off, down_off;
		glm_vec3_scale(front, -0.002f, forward_off);
		glm_vec3_scale(node->up_vec, -0.015f, down_off);
		glm_vec3_add(label_pos, forward_off, label_pos);
		glm_vec3_add(label_pos, down_off, label_pos);

		glm_vec3_copy(label_pos, (*label_instances)[*label_count].nodePos);
		(*label_instances)[*label_count].charRect[0] = x_cursor + ci->x0;
		(*label_instances)[*label_count].charRect[1] = ci->y0;
		(*label_instances)[*label_count].charRect[2] = x_cursor + ci->x1;
		(*label_instances)[*label_count].charRect[3] = ci->y1;
		(*label_instances)[*label_count].charUV[0] = ci->u0;
		(*label_instances)[*label_count].charUV[1] = ci->v0;
		(*label_instances)[*label_count].charUV[2] = ci->u1;
		(*label_instances)[*label_count].charUV[3] = ci->v1;

		float dynamic_scale = world_text_scale;
		glm_vec3_scale(node->right_vec, dynamic_scale, (*label_instances)[*label_count].right);
		glm_vec3_scale(node->up_vec, dynamic_scale, (*label_instances)[*label_count].up);

		x_cursor += ci->xadvance;
		(*label_count)++;
	}
}

void generate_vulkan_menu_buffers(AppContext *ctx, Renderer *r)
{
	MenuNode *node = ctx->root_menu;
	if (node == NULL)
		return;

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

		// 1. Draw as a list item if it's not the root
		if (current != node) {
			if (instance_count >= capacity) {
				capacity *= 2;
				instances = (MenuInstance *)realloc(instances, sizeof(MenuInstance) * capacity);
			}

			glm_vec3_copy(current->quad_center_pos, instances[instance_count].worldPos);
			instances[instance_count].texCoord[0] = 0.0f;
			instances[instance_count].texCoord[1] = 0.0f;
			instances[instance_count].texId = (float)current->icon_texture_id;
			instances[instance_count].scale[0] = current->box_width;
			instances[instance_count].scale[1] = current->box_height;
			instances[instance_count].scale[2] = 1.0f;
			instances[instance_count].hovered = current->hovered ? 1.0f : 0.0f;
			memcpy(instances[instance_count].rotation, current->rotation, sizeof(versor));

			const char *display_text = current->label;
			char input_display[300] = {0};

			if (current->type == NODE_INPUT_TEXT) {
				if (current->input_buffer[0]) {
					strncpy(input_display, current->input_buffer, sizeof(input_display) - 1);
					if (current->is_focused) {
						size_t len = strlen(input_display);
						if (len < sizeof(input_display) - 2) {
							input_display[len] = '_';
							input_display[len + 1] = '\0';
						}
					}
				} else if (current->is_focused) {
					input_display[0] = '_';
					input_display[1] = '\0';
				}
				display_text = input_display;
			}

			if (display_text && display_text[0]) {
				render_text_at_position(current, display_text, current->text_anchor_pos, &label_count, &label_instances, &label_capacity);
			}

			// Draw ">" arrow for branches to indicate submenus
			if (current->type == NODE_BRANCH) {
				vec3 arrow_pos;
				glm_vec3_copy(current->text_anchor_pos, arrow_pos);
				vec3 right_shift;
				glm_vec3_scale(current->right_vec, current->box_width - 0.15f, right_shift);
				glm_vec3_add(arrow_pos, right_shift, arrow_pos);
				render_text_at_position(current, ">", arrow_pos, &label_count, &label_instances, &label_capacity);
			}

			instance_count++;
		}

		// 2. Draw Submenu Card Background and Title ONLY if expanded
		if (current->type == NODE_BRANCH && current->num_children > 0) {
			if (current->is_expanded) {
				if (instance_count >= capacity) {
					capacity *= 2;
					instances = (MenuInstance *)realloc(instances, sizeof(MenuInstance) * capacity);
				}

				glm_vec3_copy(current->card_bg_pos, instances[instance_count].worldPos);
				instances[instance_count].texCoord[0] = 0.0f;
				instances[instance_count].texCoord[1] = 0.0f;
				instances[instance_count].texId = -1.0f;

				instances[instance_count].scale[0] = current->card_width;
				instances[instance_count].scale[1] = current->card_height;
				instances[instance_count].scale[2] = 1.0f;
				instances[instance_count].hovered = 0.0f;

				memcpy(instances[instance_count].rotation, current->rotation, sizeof(versor));
				instance_count++;

				// --- NEW: Draw Title Bar Background ---
				if (instance_count >= capacity) {
					capacity *= 2;
					instances = (MenuInstance *)realloc(instances, sizeof(MenuInstance) * capacity);
				}
				vec3 title_bg_pos;
				glm_vec3_copy(current->card_bg_pos, title_bg_pos);
				vec3 title_up_shift;
				glm_vec3_scale(current->up_vec, (current->card_height * 0.5f) - 0.05f, title_up_shift);
				glm_vec3_add(title_bg_pos, title_up_shift, title_bg_pos);

				glm_vec3_copy(title_bg_pos, instances[instance_count].worldPos);
				instances[instance_count].texCoord[0] = 0.0f;
				instances[instance_count].texCoord[1] = 0.0f;
				instances[instance_count].texId = -2.0f; // -2.0f = Title Bar Color
				instances[instance_count].scale[0] = current->card_width;
				instances[instance_count].scale[1] = 0.10f; // TITLE_BAR_HEIGHT
				instances[instance_count].scale[2] = 1.0f;
				instances[instance_count].hovered = 0.0f;
				memcpy(instances[instance_count].rotation, current->rotation, sizeof(versor));
				instance_count++;

				if (current->label) {
					vec3 title_pos;
					glm_vec3_copy(current->card_bg_pos, title_pos);
					vec3 up_shift, right_shift;

					glm_vec3_scale(current->up_vec, current->card_height * 0.5f - 0.05f, up_shift);
					glm_vec3_scale(current->right_vec, -current->card_width * 0.5f + 0.05f, right_shift);

					glm_vec3_add(title_pos, up_shift, title_pos);
					glm_vec3_add(title_pos, right_shift, title_pos);

					render_text_at_position(current, current->label, title_pos, &label_count, &label_instances, &label_capacity);
				}

				// Traverse into submenus
				for (int i = 0; i < current->num_children; i++) {
					stack[stack_top++] = current->children[i];
				}
			}
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

		// --- NEW: Draw Detached Info Card ---
		if (ctx->info_card.is_visible && ctx->active_menu_level) {
			float card_w = 0.8f;
			float card_h = 0.10f + (ctx->info_card.num_pairs * 0.09f); // TITLE_BAR_HEIGHT + (items * MENU_ITEM_HEIGHT)

			if (instance_count + 2 >= capacity) {
				capacity *= 2;
				instances = (MenuInstance *)realloc(instances, sizeof(MenuInstance) * capacity);
			}

			// Anchor the info card to the right of the active menu branch AND Top-Align it
			vec3 card_pos;
			glm_vec3_copy(ctx->active_menu_level->card_bg_pos, card_pos);

			vec3 right_shift, up_shift;
			glm_vec3_scale(node->right_vec, (ctx->active_menu_level->card_width * 0.5f) + (card_w * 0.5f) + 0.05f, right_shift);

			// Top Alignment Shift: (ActiveCardHeight/2) - (InfoCardHeight/2)
			float align_y = (ctx->active_menu_level->card_height * 0.5f) - (card_h * 0.5f);
			glm_vec3_scale(node->up_vec, align_y, up_shift);

			glm_vec3_add(card_pos, right_shift, card_pos);
			glm_vec3_add(card_pos, up_shift, card_pos);

			// 1. Draw Info Card Background Quad
			glm_vec3_copy(card_pos, instances[instance_count].worldPos);
			instances[instance_count].texCoord[0] = 0.0f;
			instances[instance_count].texCoord[1] = 0.0f;
			instances[instance_count].texId = -3.0f; // -3.0 = Info Card Color
			instances[instance_count].scale[0] = card_w;
			instances[instance_count].scale[1] = card_h;
			instances[instance_count].scale[2] = 1.0f;
			instances[instance_count].hovered = 0.0f;
			memcpy(instances[instance_count].rotation, node->rotation, sizeof(versor));
			instance_count++;

			// 1.5 Draw Info Card Title Bar Background Quad
			vec3 info_title_bg;
			glm_vec3_copy(card_pos, info_title_bg);
			vec3 info_title_up;
			glm_vec3_scale(node->up_vec, (card_h * 0.5f) - 0.05f, info_title_up);
			glm_vec3_add(info_title_bg, info_title_up, info_title_bg);

			glm_vec3_copy(info_title_bg, instances[instance_count].worldPos);
			instances[instance_count].texCoord[0] = 0.0f;
			instances[instance_count].texCoord[1] = 0.0f;
			instances[instance_count].texId = -2.0f; // -2.0 = Title Bar Color
			instances[instance_count].scale[0] = card_w;
			instances[instance_count].scale[1] = 0.10f; // TITLE_BAR_HEIGHT
			instances[instance_count].scale[2] = 1.0f;
			instances[instance_count].hovered = 0.0f;
			memcpy(instances[instance_count].rotation, node->rotation, sizeof(versor));
			instance_count++;

			// Calculate Top-Left Anchor for Text
			vec3 title_pos;
			glm_vec3_copy(card_pos, title_pos);
			vec3 title_up_shift, left_shift;
			glm_vec3_scale(node->up_vec, card_h * 0.5f - 0.05f, title_up_shift);
			glm_vec3_scale(node->right_vec, -card_w * 0.5f + 0.05f, left_shift);
			glm_vec3_add(title_pos, title_up_shift, title_pos);
			glm_vec3_add(title_pos, left_shift, title_pos);

			// 2. Draw Title Text
			render_text_at_position(node, ctx->info_card.title, title_pos, &label_count, &label_instances, &label_capacity);

			// 3. Draw Generic Key-Value Columns
			for (int i = 0; i < ctx->info_card.num_pairs; i++) {
				vec3 row_pos;
				glm_vec3_copy(title_pos, row_pos);
				vec3 row_down;
				glm_vec3_scale(node->up_vec, -(0.10f + i * 0.09f), row_down);
				glm_vec3_add(row_pos, row_down, row_pos);

				// Key (Left Column)
				render_text_at_position(node, ctx->info_card.pairs[i].key, row_pos, &label_count, &label_instances, &label_capacity);

				// Value (Right Column) - positioned relative to key width
				float key_width = calculate_text_width(ctx->info_card.pairs[i].key);
				float value_offset = key_width + 0.02f; // key width + padding
				vec3 val_pos;
				glm_vec3_copy(row_pos, val_pos);
				vec3 val_right;
				glm_vec3_scale(node->right_vec, value_offset, val_right);
				glm_vec3_add(val_pos, val_right, val_pos);
				render_text_at_position(node, ctx->info_card.pairs[i].value, val_pos, &label_count, &label_instances, &label_capacity);
			}
		}

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