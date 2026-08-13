/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "vulkan/menu_scene.h"

#include "ui/menu_metrics.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool reserve_instances(MenuScene *scene, size_t needed)
{
	if (needed <= scene->instance_capacity)
		return true;
	size_t capacity = scene->instance_capacity ? scene->instance_capacity : 128;
	while (capacity < needed)
		capacity *= 2;
	MenuInstance *instances = realloc(scene->instances, sizeof(*instances) * capacity);
	if (!instances)
		return false;
	scene->instances = instances;
	scene->instance_capacity = capacity;
	return true;
}

static bool reserve_text_instances(MenuScene *scene, size_t needed)
{
	if (needed <= scene->text_instance_capacity)
		return true;
	size_t capacity = scene->text_instance_capacity ? scene->text_instance_capacity : 256;
	while (capacity < needed)
		capacity *= 2;
	TextQuadInstance *instances = realloc(scene->text_instances, sizeof(*instances) * capacity);
	if (!instances)
		return false;
	scene->text_instances = instances;
	scene->text_instance_capacity = capacity;
	return true;
}

static MenuInstance *append_instance(MenuScene *scene)
{
	if (!reserve_instances(scene, scene->instance_count + 1))
		return NULL;
	MenuInstance *instance = &scene->instances[scene->instance_count++];
	memset(instance, 0, sizeof(*instance));
	return instance;
}

static TextQuadInstance *append_text_instance(MenuScene *scene)
{
	if (!reserve_text_instances(scene, scene->text_instance_count + 1))
		return NULL;
	TextQuadInstance *instance = &scene->text_instances[scene->text_instance_count++];
	memset(instance, 0, sizeof(*instance));
	return instance;
}

static void set_background_instance(MenuInstance *instance, const float position[3], const float rotation[4], float width, float height, float tex_id, bool hovered)
{
	memcpy(instance->worldPos, position, sizeof(vec3));
	instance->texId = tex_id;
	instance->scale[0] = width;
	instance->scale[1] = height;
	instance->scale[2] = 1.0f;
	instance->hovered = hovered ? 1.0f : 0.0f;
	memcpy(instance->rotation, rotation, sizeof(versor));
}

static bool set_text(TextQuadInstance *instance, const MenuTextProvider *text, const char *label, float width, float height, float padding)
{
	if (!label || !label[0])
		return true;
	TextRegion region;
	if (!text->resolve(text->context, label, &region))
		return false;
	instance->textUV[0] = region.u0;
	instance->textUV[1] = region.v0;
	instance->textUV[2] = region.u1;
	instance->textUV[3] = region.v1;
	float text_width = region.width_px * MENU_METRICS.text_scale / width;
	float text_height = region.height_px * MENU_METRICS.text_scale / height;
	float top = (1.0f - text_height) * 0.5f;
	instance->textRegion[0] = padding / width;
	instance->textRegion[1] = top;
	instance->textRegion[2] = instance->textRegion[0] + text_width;
	instance->textRegion[3] = top + text_height;
	return true;
}

static bool append_title(MenuScene *scene, const MenuTextProvider *text, const float position[3], const float rotation[4], float width, const char *label)
{
	MenuInstance *background = append_instance(scene);
	if (!background)
		return false;
	set_background_instance(background, position, rotation, width, MENU_METRICS.title_height, -2.0f, false);
	if (!label || !label[0])
		return true;

	TextQuadInstance *title = append_text_instance(scene);
	if (!title)
		return false;
	memcpy(title->worldPos, position, sizeof(vec3));
	title->bgColor[0] = 0.18f;
	title->bgColor[1] = 0.22f;
	title->bgColor[2] = 0.28f;
	title->bgColor[3] = 1.0f;
	title->scale[0] = width;
	title->scale[1] = MENU_METRICS.title_height;
	title->scale[2] = 1.0f;
	memcpy(title->rotation, rotation, sizeof(versor));
	return set_text(title, text, label, width, MENU_METRICS.title_height, MENU_METRICS.text_padding);
}

static bool append_row(const MenuState *menu, MenuNode *node, const MenuTextProvider *text, MenuScene *scene)
{
	bool hovered = menu->hovered_node == node;
	TextQuadInstance *label = append_text_instance(scene);
	if (!label)
		return false;
	glm_vec3_copy(node->quad_center_pos, label->worldPos);
	if (hovered) {
		label->bgColor[0] = 0.3f;
		label->bgColor[1] = 0.4f;
		label->bgColor[2] = 0.5f;
		label->bgColor[3] = 0.8f;
	}
	label->scale[0] = node->box_width;
	label->scale[1] = node->box_height;
	label->scale[2] = 1.0f;
	memcpy(label->rotation, node->rotation, sizeof(versor));
	if (!set_text(label, text, node->label, node->box_width, node->box_height, MENU_METRICS.text_padding))
		return false;

	if (node->type == NODE_BRANCH) {
		TextRegion arrow_region;
		if (!text->resolve(text->context, ">", &arrow_region))
			return false;
		TextQuadInstance *arrow = append_text_instance(scene);
		if (!arrow)
			return false;
		glm_vec3_copy(node->quad_center_pos, arrow->worldPos);
		vec3 shift;
		glm_vec3_scale(node->right_vec, node->box_width * 0.5f - MENU_METRICS.arrow_right_inset, shift);
		glm_vec3_add(arrow->worldPos, shift, arrow->worldPos);
		arrow->scale[0] = arrow_region.width_px * MENU_METRICS.text_scale;
		arrow->scale[1] = arrow_region.height_px * MENU_METRICS.text_scale;
		arrow->scale[2] = 1.0f;
		memcpy(arrow->rotation, node->rotation, sizeof(versor));
		arrow->textUV[0] = arrow_region.u0;
		arrow->textUV[1] = arrow_region.v0;
		arrow->textUV[2] = arrow_region.u1;
		arrow->textUV[3] = arrow_region.v1;
		arrow->textRegion[2] = 1.0f;
		arrow->textRegion[3] = 1.0f;
	}

	MenuInstance *background = append_instance(scene);
	if (!background)
		return false;
	set_background_instance(background, node->quad_center_pos, node->rotation, node->box_width, node->box_height, -1.0f, hovered);
	return true;
}

static bool append_card(MenuNode *node, const MenuTextProvider *text, MenuScene *scene)
{
	MenuInstance *card = append_instance(scene);
	if (!card)
		return false;
	set_background_instance(card, node->card_bg_pos, node->rotation, node->card_width, node->card_height, -1.0f, false);

	vec3 title_position;
	glm_vec3_copy(node->card_bg_pos, title_position);
	vec3 shift;
	glm_vec3_scale(node->up_vec, node->card_height * 0.5f - MENU_METRICS.title_height * 0.5f, shift);
	glm_vec3_add(title_position, shift, title_position);

	return append_title(scene, text, title_position, node->rotation, node->card_width, node->label);
}

static bool append_tree(const MenuState *menu, MenuNode *root, MenuNode *node, const MenuTextProvider *text, MenuScene *scene)
{
	if (node != root && node->is_visible && !append_row(menu, node, text, scene))
		return false;
	if (node->type != NODE_BRANCH || node->num_children == 0 || !node->is_expanded)
		return true;
	if (!append_card(node, text, scene))
		return false;
	for (int i = 0; i < node->num_children; i++) {
		if (!append_tree(menu, root, node->children[i], text, scene))
			return false;
	}
	return true;
}

static bool append_info_card(const MenuState *menu, const MenuTextProvider *text, MenuScene *scene)
{
	if (!menu->info_card.is_visible || !menu->active_level)
		return true;
	MenuNode *root = menu->root;
	float width = text->measure_width(text->context, menu->info_card.title);
	for (int i = 0; i < menu->info_card.num_pairs; i++) {
		char row[128];
		snprintf(row, sizeof(row), "%s: %s", menu->info_card.pairs[i].key, menu->info_card.pairs[i].value);
		float row_width = text->measure_width(text->context, row);
		if (row_width > width)
			width = row_width;
	}
	width += MENU_METRICS.title_height;
	float height = MENU_METRICS.title_height + menu->info_card.num_pairs * MENU_METRICS.item_height;

	vec3 position;
	glm_vec3_copy(menu->active_level->card_bg_pos, position);
	vec3 right_shift;
	vec3 up_shift;
	glm_vec3_scale(root->right_vec, menu->active_level->card_width * 0.5f + width * 0.5f, right_shift);
	glm_vec3_scale(root->up_vec, menu->active_level->card_height * 0.5f - height * 0.5f, up_shift);
	glm_vec3_add(position, right_shift, position);
	glm_vec3_add(position, up_shift, position);

	MenuInstance *card = append_instance(scene);
	if (!card)
		return false;
	set_background_instance(card, position, root->rotation, width, height, -3.0f, false);

	vec3 title_position;
	glm_vec3_copy(position, title_position);
	glm_vec3_scale(root->up_vec, height * 0.5f - MENU_METRICS.title_height * 0.5f, up_shift);
	glm_vec3_add(title_position, up_shift, title_position);
	if (!append_title(scene, text, title_position, root->rotation, width, menu->info_card.title))
		return false;

	for (int i = 0; i < menu->info_card.num_pairs; i++) {
		char row[128];
		snprintf(row, sizeof(row), "%s: %s", menu->info_card.pairs[i].key, menu->info_card.pairs[i].value);
		TextQuadInstance *entry = append_text_instance(scene);
		if (!entry)
			return false;
		glm_vec3_copy(position, entry->worldPos);
		glm_vec3_scale(root->up_vec, height * 0.5f - MENU_METRICS.title_height - i * MENU_METRICS.item_height - MENU_METRICS.item_height * 0.5f, up_shift);
		glm_vec3_add(entry->worldPos, up_shift, entry->worldPos);
		entry->scale[0] = width;
		entry->scale[1] = MENU_METRICS.item_height;
		entry->scale[2] = 1.0f;
		memcpy(entry->rotation, root->rotation, sizeof(versor));
		if (!set_text(entry, text, row, width, MENU_METRICS.item_height, MENU_METRICS.text_padding))
			return false;
	}
	return true;
}

bool menu_scene_build(const MenuState *menu, const MenuTextProvider *text, MenuScene *scene)
{
	if (!menu || !menu->root || !text || !text->resolve || !text->measure_width || !scene)
		return false;
	scene->instance_count = 0;
	scene->text_instance_count = 0;
	return append_tree(menu, menu->root, menu->root, text, scene) && append_info_card(menu, text, scene);
}

void menu_scene_destroy(MenuScene *scene)
{
	if (!scene)
		return;
	free(scene->instances);
	free(scene->text_instances);
	memset(scene, 0, sizeof(*scene));
}
