#ifndef TEXT_H
#define TEXT_H

#include <stdbool.h>
#include <stdint.h>

#include <vulkan/vulkan.h>

typedef struct
{
	float x0, y0, x1, y1; // Pen offsets (pen-to-bitmap placement)
	int src_x0, src_y0;	  // Bitmap pixel position in font atlas
	int src_x1, src_y1;
	float u0, v0, u1, v1; // Normalized UV coordinates in font atlas
	float xadvance;
} CharInfo;

typedef struct
{
	uint8_t *atlasData;
	int width, height;
	CharInfo chars[128];
	float ascent;
	float descent;
} FontAtlas;

typedef struct
{
	float u0, v0, u1, v1; // UV coordinates in atlas
	float width_px;		  // Width in pixels
	float height_px;	  // Height in pixels
} TextRegion;

typedef struct
{
	uint8_t *pixels;
	int width, height; // Atlas dimensions (e.g. 2048x512)
	int cursor_x;
	int cursor_y;
	int row_height;
	bool dirty; // Needs GPU upload

	// GPU resources (lazy-created on first upload)
	VkImage image;
	VkDeviceMemory memory;
	VkImageView view;
} TextAtlas;

int text_generate_atlas(const char *fontPath, FontAtlas *atlas);

bool text_atlas_init(TextAtlas *ta, int width, int height);
void text_atlas_clear(TextAtlas *ta);
void text_atlas_render(TextAtlas *ta, const FontAtlas *font, const char *text, TextRegion *out);
void text_atlas_ensure_uploaded(TextAtlas *ta, VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue queue);
void text_atlas_destroy(TextAtlas *ta, VkDevice device);

#endif
