/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "vulkan/menu.h"

#include "ui/menu_metrics.h"
#include "vulkan/buffers.h"
#include "vulkan/menu_scene.h"
#include "vulkan/renderer_lifecycle.h"
#include "vulkan/text.h"
#include "vulkan/utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern FontAtlas globalAtlas;

typedef struct
{
	char *text;
	TextRegion region;
} MenuTextCacheEntry;

typedef struct
{
	MenuTextCacheEntry *entries;
	size_t count;
	size_t capacity;
	TextAtlas *atlas;
	bool atlas_dimensions_changed;
} MenuTextCache;

static MenuTextCache *menu_text_cache_get(MenuBuffers *menu)
{
	if (menu->text_cache)
		return menu->text_cache;
	MenuTextCache *cache = calloc(1, sizeof(*cache));
	if (!cache)
		return NULL;
	cache->atlas = &menu->text_atlas;
	menu->text_cache = cache;
	return cache;
}

static void menu_text_cache_clear(MenuTextCache *cache)
{
	if (!cache)
		return;
	for (size_t i = 0; i < cache->count; i++)
		free(cache->entries[i].text);
	cache->count = 0;
}

static void menu_text_cache_destroy(MenuTextCache *cache)
{
	if (!cache)
		return;
	menu_text_cache_clear(cache);
	free(cache->entries);
	free(cache);
}

static bool menu_text_resolve(void *context, const char *text, TextRegion *region)
{
	MenuTextCache *cache = context;
	for (size_t i = 0; i < cache->count; i++) {
		if (strcmp(cache->entries[i].text, text) == 0) {
			*region = cache->entries[i].region;
			return true;
		}
	}

	char *copy = strdup(text);
	if (!copy)
		return false;
	if (cache->count == cache->capacity) {
		size_t capacity = cache->capacity ? cache->capacity * 2 : 128;
		MenuTextCacheEntry *entries = realloc(cache->entries, sizeof(*entries) * capacity);
		if (!entries) {
			free(copy);
			return false;
		}
		cache->entries = entries;
		cache->capacity = capacity;
	}
	MenuTextCacheEntry *entry = &cache->entries[cache->count];
	entry->text = copy;
	int atlas_height = cache->atlas->height;
	if (!text_atlas_render(cache->atlas, &globalAtlas, text, &entry->region)) {
		free(copy);
		return false;
	}
	cache->atlas_dimensions_changed |= cache->atlas->height != atlas_height;
	*region = entry->region;
	cache->count++;
	return true;
}

static float menu_text_measure_width(void *context, const char *text)
{
	(void)context;
	if (!text)
		return 0.0f;
	float width = 0.0f;
	for (size_t i = 0; text[i]; i++) {
		unsigned char c = (unsigned char)text[i];
		width += globalAtlas.chars[c < 128 ? c : 32].xadvance;
	}
	return width * MENU_METRICS.text_scale;
}

static bool build_scene(MenuBuffers *menu, const MenuState *state, MenuScene *scene)
{
	MenuTextCache *cache = menu_text_cache_get(menu);
	if (!cache)
		return false;
	if (menu->consumed_text_revision != state->text_revision) {
		menu_text_cache_clear(cache);
		text_atlas_clear(&menu->text_atlas);
	}

	MenuTextProvider provider = {
		.context = cache,
		.resolve = menu_text_resolve,
		.measure_width = menu_text_measure_width,
	};
	for (;;) {
		cache->atlas_dimensions_changed = false;
		if (!menu_scene_build(state, &provider, scene))
			return false;
		if (!cache->atlas_dimensions_changed)
			return true;
		menu_text_cache_clear(cache);
		text_atlas_clear(&menu->text_atlas);
	}
}

static uint32_t grow_capacity(uint32_t capacity, size_t needed)
{
	uint32_t result = capacity ? capacity : 128;
	while ((size_t)result < needed)
		result *= 2;
	return result;
}

static bool create_menu_buffer(Renderer *renderer, VkDeviceSize element_size, uint32_t capacity, VkBuffer *buffer, VkDeviceMemory *memory)
{
	VkResult result = try_create_buffer(renderer->core.device, renderer->core.physicalDevice, element_size * capacity, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, buffer, memory);
	if (result == VK_SUCCESS)
		return true;
	fprintf(stderr, "Failed to grow menu buffer: %d\n", result);
	return false;
}

static bool upload_scene(Renderer *renderer, const MenuScene *scene)
{
	MenuBuffers *menu = &renderer->menu;
	VkBuffer new_instance = VK_NULL_HANDLE;
	VkDeviceMemory new_instance_memory = VK_NULL_HANDLE;
	VkBuffer new_text = VK_NULL_HANDLE;
	VkDeviceMemory new_text_memory = VK_NULL_HANDLE;
	uint32_t instance_capacity = menu->instance_capacity;
	uint32_t text_capacity = menu->text_quad_instance_capacity;

	if (scene->instance_count > instance_capacity) {
		instance_capacity = grow_capacity(instance_capacity, scene->instance_count);
		if (!create_menu_buffer(renderer, sizeof(MenuInstance), instance_capacity, &new_instance, &new_instance_memory))
			return false;
	}
	if (scene->text_instance_count > text_capacity) {
		text_capacity = grow_capacity(text_capacity, scene->text_instance_count);
		if (!create_menu_buffer(renderer, sizeof(TextQuadInstance), text_capacity, &new_text, &new_text_memory)) {
			VK_DESTROY_BUFFER(renderer->core.device, new_instance, new_instance_memory);
			return false;
		}
	}

	if (new_instance != VK_NULL_HANDLE) {
		VK_DESTROY_BUFFER(renderer->core.device, menu->instance, menu->instance_memory);
		menu->instance = new_instance;
		menu->instance_memory = new_instance_memory;
		menu->instance_capacity = instance_capacity;
	}
	if (new_text != VK_NULL_HANDLE) {
		VK_DESTROY_BUFFER(renderer->core.device, menu->text_quad_instance, menu->text_quad_instance_memory);
		menu->text_quad_instance = new_text;
		menu->text_quad_instance_memory = new_text_memory;
		menu->text_quad_instance_capacity = text_capacity;
	}

	if (scene->instance_count > 0)
		update_buffer(renderer->core.device, menu->instance_memory, sizeof(MenuInstance) * scene->instance_count, scene->instances);
	if (scene->text_instance_count > 0)
		update_buffer(renderer->core.device, menu->text_quad_instance_memory, sizeof(TextQuadInstance) * scene->text_instance_count, scene->text_instances);
	menu->node_count = (uint32_t)scene->instance_count;
	menu->text_quad_instance_count = (uint32_t)scene->text_instance_count;
	return true;
}

static void update_text_descriptors(Renderer *renderer)
{
	MenuBuffers *menu = &renderer->menu;
	if (menu->descriptor_image_revision == menu->text_atlas.image_revision)
		return;
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT * MAX_VIEWS; i++) {
		VkDescriptorBufferInfo buffer_info = {renderer->ubo.buffers[i], 0, sizeof(UniformBufferObject)};
		VkDescriptorImageInfo image_info = {renderer->texture.sampler, menu->text_atlas.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
		VkWriteDescriptorSet writes[] = {
			VK_WRITE_DESC_BUFFER(renderer->descriptors.text_quad_sets[i], 0, &buffer_info, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER),
			VK_WRITE_DESC_IMAGE(renderer->descriptors.text_quad_sets[i], 1, &image_info, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
		};
		vkUpdateDescriptorSets(renderer->core.device, 2, writes, 0, NULL);
	}
	menu->descriptor_image_revision = menu->text_atlas.image_revision;
}

bool renderer_menu_init(Renderer *renderer)
{
	memset(&renderer->menu, 0, sizeof(renderer->menu));
	if (!text_atlas_init(&renderer->menu.text_atlas, 2048, 512)) {
		fprintf(stderr, "Failed to initialize menu text atlas\n");
		return false;
	}
	return true;
}

bool renderer_menu_update(Renderer *renderer, const MenuState *state, bool visible)
{
	if (!renderer || !state)
		return false;
	renderer->menu.visible = visible;
	if (!visible)
		return true;
	if (renderer->menu.consumed_scene_revision == state->scene_revision && renderer->menu.consumed_text_revision == state->text_revision)
		return true;

	renderer_wait_frames_idle(renderer);
	MenuScene scene = {0};
	bool built = build_scene(&renderer->menu, state, &scene);
	if (!built || !upload_scene(renderer, &scene)) {
		menu_scene_destroy(&scene);
		return false;
	}
	text_atlas_ensure_uploaded(&renderer->menu.text_atlas, renderer->core.device, renderer->core.physicalDevice, renderer->commands.commandPool, renderer->core.graphicsQueue);
	update_text_descriptors(renderer);
	renderer->menu.consumed_scene_revision = state->scene_revision;
	renderer->menu.consumed_text_revision = state->text_revision;
	menu_scene_destroy(&scene);
	return true;
}

void renderer_menu_destroy(Renderer *renderer)
{
	if (!renderer)
		return;
	VK_DESTROY_BUFFER(renderer->core.device, renderer->menu.instance, renderer->menu.instance_memory);
	VK_DESTROY_BUFFER(renderer->core.device, renderer->menu.text_quad_instance, renderer->menu.text_quad_instance_memory);
	menu_text_cache_destroy(renderer->menu.text_cache);
	renderer->menu.text_cache = NULL;
	text_atlas_destroy(&renderer->menu.text_atlas, renderer->core.device);
}
