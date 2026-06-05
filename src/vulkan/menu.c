#include "vulkan/menu.h"
#include "vulkan/renderer.h"
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

void generate_vulkan_menu_buffers(AppContext *ctx, Renderer *r)
{
	MenuNode *node = ctx->root_menu;
	if (node == NULL)
		return;

	int capacity = 128;
	MenuInstance *instances = (MenuInstance *)malloc(sizeof(MenuInstance) * capacity);
	int instance_count = 0;

	int tq_capacity = 256;
	TextQuadInstance *tq_instances = (TextQuadInstance *)malloc(sizeof(TextQuadInstance) * tq_capacity);
	int tq_count = 0;

	float world_text_scale = 0.003f;

	// Clear text atlas and rebuild
	text_atlas_clear(&r->menuTextAtlas);

	// Reset cached text regions so they get re-rendered into the fresh atlas
	{
		MenuNode **reset_stack = (MenuNode **)malloc(sizeof(MenuNode *) * 256);
		int reset_top = 0;
		reset_stack[reset_top++] = node;
		while (reset_top > 0) {
			MenuNode *n = reset_stack[--reset_top];
			if (n == NULL)
				continue;
			n->cachedTextRegion.u0 = n->cachedTextRegion.v0 = n->cachedTextRegion.u1 = n->cachedTextRegion.v1 = 0;
			n->cachedTextRegion.width_px = 0;
			n->cachedTextRegion.height_px = 0;
			if (n->type == NODE_BRANCH) {
				for (int i = 0; i < n->num_children; i++)
					reset_stack[reset_top++] = n->children[i];
			}
		}
		free(reset_stack);
	}

	MenuNode **stack = (MenuNode **)malloc(sizeof(MenuNode *) * 256);
	int stack_top = 0;
	stack[stack_top++] = node;

	while (stack_top > 0) {
		MenuNode *current = stack[--stack_top];
		if (current == NULL)
			continue;

		// 1. Draw as a list item if it's not the root
		if (current != node) {
			// Ensure capacity
			if (tq_count >= tq_capacity) {
				tq_capacity *= 2;
				tq_instances = (TextQuadInstance *)realloc(tq_instances, sizeof(TextQuadInstance) * tq_capacity);
			}

			// Background + merged text quad for this list item
			TextQuadInstance *tq = &tq_instances[tq_count];
			glm_vec3_copy(current->quad_center_pos, tq->worldPos);

			// Background color: transparent by default, hover highlight
			if (current->hovered) {
				tq->bgColor[0] = 0.3f;
				tq->bgColor[1] = 0.4f;
				tq->bgColor[2] = 0.5f;
				tq->bgColor[3] = 0.8f;
			} else {
				tq->bgColor[0] = 0.0f;
				tq->bgColor[1] = 0.0f;
				tq->bgColor[2] = 0.0f;
				tq->bgColor[3] = 0.0f;
			}

			tq->scale[0] = current->box_width;
			tq->scale[1] = current->box_height;
			tq->scale[2] = 1.0f;
			memcpy(tq->rotation, current->rotation, sizeof(versor));

			// Render label text into atlas
			if (current->label && current->label[0]) {
				if (current->cachedTextRegion.width_px < 0.5f) {
					text_atlas_render(&r->menuTextAtlas, &globalAtlas, current->label, &current->cachedTextRegion);
				}
				tq->textUV[0] = current->cachedTextRegion.u0;
				tq->textUV[1] = current->cachedTextRegion.v0;
				tq->textUV[2] = current->cachedTextRegion.u1;
				tq->textUV[3] = current->cachedTextRegion.v1;

				// Text region in quad-local [0..1]: left-padded, vertically centered
				float text_w_norm = current->cachedTextRegion.width_px * world_text_scale / current->box_width;
				float text_h_norm = current->cachedTextRegion.height_px * world_text_scale / current->box_height;
				float pad_x = 0.05f / current->box_width;
				float pad_y = (1.0f - text_h_norm) * 0.5f;
				tq->textRegion[0] = pad_x;
				tq->textRegion[1] = pad_y;
				tq->textRegion[2] = pad_x + text_w_norm;
				tq->textRegion[3] = pad_y + text_h_norm;
			} else {
				tq->textUV[0] = 0;
				tq->textUV[1] = 0;
				tq->textUV[2] = 0;
				tq->textUV[3] = 0;
				tq->textRegion[0] = 0;
				tq->textRegion[1] = 0;
				tq->textRegion[2] = 0;
				tq->textRegion[3] = 0;
			}

			tq_count++;

			// ">" arrow for branches (text-only quad at right edge)
			if (current->type == NODE_BRANCH) {
				if (tq_count >= tq_capacity) {
					tq_capacity *= 2;
					tq_instances = (TextQuadInstance *)realloc(tq_instances, sizeof(TextQuadInstance) * tq_capacity);
				}

				TextRegion arrowRegion;
				text_atlas_render(&r->menuTextAtlas, &globalAtlas, ">", &arrowRegion);

				// Position arrow at right edge of the item
				vec3 arrow_pos;
				glm_vec3_copy(current->quad_center_pos, arrow_pos);
				vec3 right_shift;
				glm_vec3_scale(current->right_vec, current->box_width * 0.5f - 0.07f, right_shift);
				glm_vec3_add(arrow_pos, right_shift, arrow_pos);

				TextQuadInstance *arrow = &tq_instances[tq_count];
				glm_vec3_copy(arrow_pos, arrow->worldPos);
				arrow->bgColor[0] = 0.0f;
				arrow->bgColor[1] = 0.0f;
				arrow->bgColor[2] = 0.0f;
				arrow->bgColor[3] = 0.0f;
				float arrow_w = arrowRegion.width_px * world_text_scale;
				float arrow_h = arrowRegion.height_px * world_text_scale;
				arrow->scale[0] = arrow_w;
				arrow->scale[1] = arrow_h;
				arrow->scale[2] = 1.0f;
				memcpy(arrow->rotation, current->rotation, sizeof(versor));
				arrow->textUV[0] = arrowRegion.u0;
				arrow->textUV[1] = arrowRegion.v0;
				arrow->textUV[2] = arrowRegion.u1;
				arrow->textUV[3] = arrowRegion.v1;
				arrow->textRegion[0] = 0.0f;
				arrow->textRegion[1] = 0.0f;
				arrow->textRegion[2] = 1.0f;
				arrow->textRegion[3] = 1.0f;
				tq_count++;
			}

			// Add a background-only MenuInstance for this item (for depth testing)
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
			instance_count++;
		}

		// 2. Draw Submenu Card Background and Title ONLY if expanded
		if (current->type == NODE_BRANCH && current->num_children > 0) {
			if (current->is_expanded) {
				// Card background (MenuInstance, drawn by menuPipeline)
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

				// Title bar background (MenuInstance, drawn by menuPipeline)
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
				instances[instance_count].texId = -2.0f;
				instances[instance_count].scale[0] = current->card_width;
				instances[instance_count].scale[1] = 0.10f;
				instances[instance_count].scale[2] = 1.0f;
				instances[instance_count].hovered = 0.0f;
				memcpy(instances[instance_count].rotation, current->rotation, sizeof(versor));
				instance_count++;

				// Title text (TextQuadInstance, drawn by textQuadPipeline)
				if (current->label) {
					if (tq_count >= tq_capacity) {
						tq_capacity *= 2;
						tq_instances = (TextQuadInstance *)realloc(tq_instances, sizeof(TextQuadInstance) * tq_capacity);
					}

					TextRegion titleRegion;
					text_atlas_render(&r->menuTextAtlas, &globalAtlas, current->label, &titleRegion);

					// Position title at the same spot as before
					vec3 title_pos;
					glm_vec3_copy(current->card_bg_pos, title_pos);
					vec3 up_shift, right_shift;
					glm_vec3_scale(current->up_vec, current->card_height * 0.5f - 0.05f, up_shift);
					glm_vec3_scale(current->right_vec, -current->card_width * 0.5f + 0.05f, right_shift);
					glm_vec3_add(title_pos, up_shift, title_pos);
					glm_vec3_add(title_pos, right_shift, title_pos);

					// The title text is positioned at text_anchor, not at quad center.
					// We need to figure out the offset from the title bar's quad center.
					// Title bar quad center is at title_bg_pos. Text anchor is at title_pos.
					// In quad-local coords: offset = (text_pos - quad_center) / scale

					TextQuadInstance *tq = &tq_instances[tq_count];
					// We use the title bar quad center, but the text region accounts for the offset
					glm_vec3_copy(title_bg_pos, tq->worldPos);
					tq->bgColor[0] = 0.18f;
					tq->bgColor[1] = 0.22f;
					tq->bgColor[2] = 0.28f;
					tq->bgColor[3] = 1.0f;
					tq->scale[0] = current->card_width;
					tq->scale[1] = 0.10f;
					tq->scale[2] = 1.0f;
					memcpy(tq->rotation, current->rotation, sizeof(versor));

					tq->textUV[0] = titleRegion.u0;
					tq->textUV[1] = titleRegion.v0;
					tq->textUV[2] = titleRegion.u1;
					tq->textUV[3] = titleRegion.v1;

					// Text region: use left padding, center vertically
					float text_w_norm = titleRegion.width_px * world_text_scale / current->card_width;
					float text_h_norm = titleRegion.height_px * world_text_scale / 0.10f;
					float left = 0.05f / current->card_width;
					float top = (1.0f - text_h_norm) * 0.5f;
					tq->textRegion[0] = left;
					tq->textRegion[1] = top;
					tq->textRegion[2] = left + text_w_norm;
					tq->textRegion[3] = top + text_h_norm;

					tq_count++;
				}

				// Traverse into submenus
				for (int i = 0; i < current->num_children; i++) {
					stack[stack_top++] = current->children[i];
				}
			}
		}
	}

	free(stack);

	// --- Info Card ---
	if (ctx->info_card.is_visible && ctx->active_menu_level) {
		float card_w = 0.8f;
		float card_h = 0.10f + (ctx->info_card.num_pairs * 0.09f);

		// Info card background (MenuInstance)
		if (instance_count + 2 >= capacity) {
			capacity *= 2;
			instances = (MenuInstance *)realloc(instances, sizeof(MenuInstance) * capacity);
		}

		vec3 card_pos;
		glm_vec3_copy(ctx->active_menu_level->card_bg_pos, card_pos);
		vec3 right_shift, up_shift;
		glm_vec3_scale(node->right_vec, (ctx->active_menu_level->card_width * 0.5f) + (card_w * 0.5f) + 0.05f, right_shift);
		float align_y = (ctx->active_menu_level->card_height * 0.5f) - (card_h * 0.5f);
		glm_vec3_scale(node->up_vec, align_y, up_shift);
		glm_vec3_add(card_pos, right_shift, card_pos);
		glm_vec3_add(card_pos, up_shift, card_pos);

		// Info card background
		glm_vec3_copy(card_pos, instances[instance_count].worldPos);
		instances[instance_count].texCoord[0] = 0.0f;
		instances[instance_count].texCoord[1] = 0.0f;
		instances[instance_count].texId = -3.0f;
		instances[instance_count].scale[0] = card_w;
		instances[instance_count].scale[1] = card_h;
		instances[instance_count].scale[2] = 1.0f;
		instances[instance_count].hovered = 0.0f;
		memcpy(instances[instance_count].rotation, node->rotation, sizeof(versor));
		instance_count++;

		// Info card title bar background
		vec3 info_title_bg;
		glm_vec3_copy(card_pos, info_title_bg);
		vec3 info_title_up;
		glm_vec3_scale(node->up_vec, (card_h * 0.5f) - 0.05f, info_title_up);
		glm_vec3_add(info_title_bg, info_title_up, info_title_bg);

		glm_vec3_copy(info_title_bg, instances[instance_count].worldPos);
		instances[instance_count].texCoord[0] = 0.0f;
		instances[instance_count].texCoord[1] = 0.0f;
		instances[instance_count].texId = -2.0f;
		instances[instance_count].scale[0] = card_w;
		instances[instance_count].scale[1] = 0.10f;
		instances[instance_count].scale[2] = 1.0f;
		instances[instance_count].hovered = 0.0f;
		memcpy(instances[instance_count].rotation, node->rotation, sizeof(versor));
		instance_count++;

		// Info card title text (TextQuadInstance on title bar)
		{
			TextRegion titleRegion;
			text_atlas_render(&r->menuTextAtlas, &globalAtlas, ctx->info_card.title, &titleRegion);

			vec3 title_pos;
			glm_vec3_copy(card_pos, title_pos);
			vec3 title_up_shift, left_shift;
			glm_vec3_scale(node->up_vec, card_h * 0.5f - 0.05f, title_up_shift);
			glm_vec3_scale(node->right_vec, -card_w * 0.5f + 0.05f, left_shift);
			glm_vec3_add(title_pos, title_up_shift, title_pos);
			glm_vec3_add(title_pos, left_shift, title_pos);

			if (tq_count >= tq_capacity) {
				tq_capacity *= 2;
				tq_instances = (TextQuadInstance *)realloc(tq_instances, sizeof(TextQuadInstance) * tq_capacity);
			}

			TextQuadInstance *tq = &tq_instances[tq_count];
			glm_vec3_copy(info_title_bg, tq->worldPos);
			tq->bgColor[0] = 0.18f;
			tq->bgColor[1] = 0.22f;
			tq->bgColor[2] = 0.28f;
			tq->bgColor[3] = 1.0f;
			tq->scale[0] = card_w;
			tq->scale[1] = 0.10f;
			tq->scale[2] = 1.0f;
			memcpy(tq->rotation, node->rotation, sizeof(versor));
			tq->textUV[0] = titleRegion.u0;
			tq->textUV[1] = titleRegion.v0;
			tq->textUV[2] = titleRegion.u1;
			tq->textUV[3] = titleRegion.v1;

			float text_w_norm = titleRegion.width_px * world_text_scale / card_w;
			float text_h_norm = titleRegion.height_px * world_text_scale / 0.10f;
			float left = 0.05f / card_w;
			float top = (1.0f - text_h_norm) * 0.5f;
			tq->textRegion[0] = left;
			tq->textRegion[1] = top;
			tq->textRegion[2] = left + text_w_norm;
			tq->textRegion[3] = top + text_h_norm;
			tq_count++;
		}

		// Info card key-value rows
		vec3 row_base;
		glm_vec3_copy(card_pos, row_base);
		vec3 row_base_up, row_base_left;
		glm_vec3_scale(node->up_vec, card_h * 0.5f - 0.05f, row_base_up);
		glm_vec3_scale(node->right_vec, -card_w * 0.5f + 0.05f, row_base_left);
		glm_vec3_add(row_base, row_base_up, row_base);
		glm_vec3_add(row_base, row_base_left, row_base);

		for (int i = 0; i < ctx->info_card.num_pairs; i++) {
			vec3 row_pos;
			glm_vec3_copy(row_base, row_pos);
			vec3 row_down;
			glm_vec3_scale(node->up_vec, -(0.10f + i * 0.09f), row_down);
			glm_vec3_add(row_pos, row_down, row_pos);

			// Pre-render "key  value" as one string for this row
			float key_w = calculate_text_width(ctx->info_card.pairs[i].key);
			float space_w = calculate_text_width("   ");
			char combined[128];
			snprintf(combined, sizeof(combined), "%s   %s", ctx->info_card.pairs[i].key, ctx->info_card.pairs[i].value);

			TextRegion rowRegion;
			text_atlas_render(&r->menuTextAtlas, &globalAtlas, combined, &rowRegion);

			if (tq_count >= tq_capacity) {
				tq_capacity *= 2;
				tq_instances = (TextQuadInstance *)realloc(tq_instances, sizeof(TextQuadInstance) * tq_capacity);
			}

			TextQuadInstance *tq = &tq_instances[tq_count];
			// Position at the row center (halfway between left edge and card right)
			vec3 row_center;
			glm_vec3_copy(card_pos, row_center);
			vec3 rc_up, rc_right;
			glm_vec3_scale(node->up_vec, card_h * 0.5f - 0.05f - (0.10f + i * 0.09f) - 0.045f, rc_up);
			glm_vec3_scale(node->right_vec, 0, rc_right);
			glm_vec3_add(row_center, rc_up, row_center);
			glm_vec3_add(row_center, rc_right, row_center);

			glm_vec3_copy(row_center, tq->worldPos);
			tq->bgColor[0] = 0.0f;
			tq->bgColor[1] = 0.0f;
			tq->bgColor[2] = 0.0f;
			tq->bgColor[3] = 0.0f;
			tq->scale[0] = card_w;
			tq->scale[1] = 0.09f;
			tq->scale[2] = 1.0f;
			memcpy(tq->rotation, node->rotation, sizeof(versor));
			tq->textUV[0] = rowRegion.u0;
			tq->textUV[1] = rowRegion.v0;
			tq->textUV[2] = rowRegion.u1;
			tq->textUV[3] = rowRegion.v1;

			// Text region: centered in the row
			float text_w_norm = rowRegion.width_px * world_text_scale / card_w;
			float text_h_norm = rowRegion.height_px * world_text_scale / 0.09f;
			float left = 0.05f / card_w;
			float top = (1.0f - text_h_norm) * 0.5f;
			tq->textRegion[0] = left;
			tq->textRegion[1] = top;
			tq->textRegion[2] = left + text_w_norm;
			tq->textRegion[3] = top + text_h_norm;
			tq_count++;
		}
	}

	// --- Ensure text atlas is uploaded to GPU ---
	text_atlas_ensure_uploaded(&r->menuTextAtlas, r->core.device, r->core.physicalDevice, r->commands.commandPool, r->core.graphicsQueue);

	// Update text quad descriptor sets to point to the text atlas
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT * MAX_VIEWS; i++) {
		VkDescriptorBufferInfo bufferInfo = {r->uniformBuffers[i], 0, sizeof(UniformBufferObject)};
		VkDescriptorImageInfo imageInfo = {r->textureSampler, r->menuTextAtlas.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
		VkWriteDescriptorSet writes[] = {
			{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, r->textQuadDescriptorSets[i], 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, NULL, &bufferInfo, NULL},
			{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, r->textQuadDescriptorSets[i], 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imageInfo, NULL, NULL},
		};
		vkUpdateDescriptorSets(r->core.device, 2, writes, 0, NULL);
	}

	// --- Upload MenuInstance buffer (background-only quads) ---
	if (instance_count > 0) {
		if (r->menuQuadVertexBuffer == VK_NULL_HANDLE) {
			QuadVertex qv[] = {{{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f}}, {{0.5f, -0.5f, 0.0f}, {1.0f, 0.0f}}, {{0.5f, 0.5f, 0.0f}, {1.0f, 1.0f}}, {{-0.5f, 0.5f, 0.0f}, {0.0f, 1.0f}}};
			uint32_t qi[] = {0, 1, 2, 2, 3, 0};

			createBuffer(r->core.device, r->core.physicalDevice, sizeof(qv), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->menuQuadVertexBuffer, &r->menuQuadVertexBufferMemory);
			updateBuffer(r->core.device, r->menuQuadVertexBufferMemory, sizeof(qv), qv);

			createBuffer(r->core.device, r->core.physicalDevice, sizeof(qi), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->menuQuadIndexBuffer, &r->menuQuadIndexBufferMemory);
			updateBuffer(r->core.device, r->menuQuadIndexBufferMemory, sizeof(qi), qi);
			r->menuQuadIndexCount = 6;
		}

		VkDeviceSize bufferSize = sizeof(MenuInstance) * instance_count;
		if (r->menuInstanceBuffer != VK_NULL_HANDLE) {
			VK_CHECK(vkDeviceWaitIdle(r->core.device), "Failed to wait for device idle before menu buffer rebuild");
			vkDestroyBuffer(r->core.device, r->menuInstanceBuffer, NULL);
			vkFreeMemory(r->core.device, r->menuInstanceBufferMemory, NULL);
		}
		createBuffer(r->core.device, r->core.physicalDevice, bufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->menuInstanceBuffer, &r->menuInstanceBufferMemory);
		updateBuffer(r->core.device, r->menuInstanceBufferMemory, bufferSize, instances);
		r->menuNodeCount = instance_count;
	}

	// --- Upload TextQuadInstance buffer (text-bearing quads) ---
	if (tq_count > 0) {
		VkDeviceSize tqBufferSize = sizeof(TextQuadInstance) * tq_count;
		if (r->textQuadInstanceBuffer != VK_NULL_HANDLE) {
			if (instance_count == 0) {
				VK_CHECK(vkDeviceWaitIdle(r->core.device), "Failed to wait for device idle before text quad buffer rebuild");
			}
			vkDestroyBuffer(r->core.device, r->textQuadInstanceBuffer, NULL);
			vkFreeMemory(r->core.device, r->textQuadInstanceBufferMemory, NULL);
		}
		createBuffer(r->core.device, r->core.physicalDevice, tqBufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->textQuadInstanceBuffer, &r->textQuadInstanceBufferMemory);
		updateBuffer(r->core.device, r->textQuadInstanceBufferMemory, tqBufferSize, tq_instances);
		r->textQuadInstanceCount = tq_count;
	}

	free(instances);
	free(tq_instances);
}
