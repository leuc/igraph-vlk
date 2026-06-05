#define STB_TRUETYPE_IMPLEMENTATION
#include "vulkan/text.h"

#include "vulkan/utils.h"
#include <stb/stb_truetype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int text_generate_atlas(const char *fontPath, FontAtlas *atlas)
{
	FILE *fp = fopen(fontPath, "rb");
	if (!fp)
		return -1;
	fseek(fp, 0, SEEK_END);
	long size = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	unsigned char *fontBuffer = malloc(size);
	fread(fontBuffer, 1, size, fp);
	fclose(fp);

	atlas->width = 512;
	atlas->height = 512;
	atlas->atlasData = malloc(atlas->width * atlas->height);

	stbtt_bakedchar baked[96]; // ASCII 32..126
	stbtt_BakeFontBitmap(fontBuffer, 0, 32.0, atlas->atlasData, atlas->width, atlas->height, 32, 96, baked);

	stbtt_fontinfo finfo;
	stbtt_InitFont(&finfo, fontBuffer, stbtt_GetFontOffsetForIndex(fontBuffer, 0));
	int asc, desc, lineGap;
	stbtt_GetFontVMetrics(&finfo, &asc, &desc, &lineGap);
	float scale = stbtt_ScaleForPixelHeight(&finfo, 32.0f);
	atlas->ascent = asc * scale;
	atlas->descent = desc * scale;

	for (int i = 0; i < 128; i++) {
		if (i >= 32 && i < 128) {
			stbtt_bakedchar *b = &baked[i - 32];
			atlas->chars[i].x0 = b->xoff;
			atlas->chars[i].y0 = b->yoff;
			atlas->chars[i].x1 = b->xoff + (b->x1 - b->x0);
			atlas->chars[i].y1 = b->yoff + (b->y1 - b->y0);
			atlas->chars[i].src_x0 = b->x0;
			atlas->chars[i].src_y0 = b->y0;
			atlas->chars[i].src_x1 = b->x1;
			atlas->chars[i].src_y1 = b->y1;
			atlas->chars[i].u0 = (float)b->x0 / atlas->width;
			atlas->chars[i].v0 = (float)b->y0 / atlas->height;
			atlas->chars[i].u1 = (float)b->x1 / atlas->width;
			atlas->chars[i].v1 = (float)b->y1 / atlas->height;
			atlas->chars[i].xadvance = b->xadvance;
		} else {
			atlas->chars[i].xadvance = 0;
		}
	}

	free(fontBuffer);
	return 0;
}

// ============================================================================
// Text Atlas — composites glyph bitmaps into a single texture
// ============================================================================

void text_atlas_init(TextAtlas *ta, int width, int height)
{
	ta->width = width;
	ta->height = height;
	ta->pixels = (uint8_t *)calloc(1, (size_t)width * height);
	ta->cursor_x = 0;
	ta->cursor_y = 0;
	ta->row_height = 0;
	ta->dirty = true;
	ta->image = VK_NULL_HANDLE;
	ta->memory = VK_NULL_HANDLE;
	ta->view = VK_NULL_HANDLE;
}

void text_atlas_clear(TextAtlas *ta)
{
	memset(ta->pixels, 0, (size_t)ta->width * ta->height);
	ta->cursor_x = 0;
	ta->cursor_y = 0;
	ta->row_height = 0;
	ta->dirty = true;
}

void text_atlas_render(TextAtlas *ta, const FontAtlas *font, const char *text, TextRegion *out)
{
	if (!text || !text[0]) {
		out->u0 = out->v0 = out->u1 = out->v1 = 0;
		out->width_px = out->height_px = 0;
		return;
	}

	int len = (int)strlen(text);

	// Compute total width and row height
	float total_width = 0;
	float max_h = 0;
	for (int i = 0; i < len; i++) {
		unsigned char c = (unsigned char)text[i];
		CharInfo *ci = (c < 128) ? &((FontAtlas *)font)->chars[c] : &((FontAtlas *)font)->chars[32];
		total_width += ci->xadvance;
		float h = ci->y1 - ci->y0;
		if (h > max_h)
			max_h = h;
	}

	// Round up row height to power-of-two-ish for alignment
	int text_h = (int)(max_h + 1.5f) & ~1; // round to even, minimum padding
	if (text_h < 1)
		text_h = 1;

	// Advance to new row if needed
	if (ta->cursor_x + (int)total_width + 1 > ta->width) {
		ta->cursor_x = 0;
		ta->cursor_y += ta->row_height + 1;
		ta->row_height = 0;
	}

	// Check atlas overflow
	if (ta->cursor_y + text_h > ta->height) {
		out->u0 = out->v0 = out->u1 = out->v1 = 0;
		out->width_px = out->height_px = 0;
		return;
	}

	// Composite each glyph into the atlas
	float x_cursor = 0;
	for (int i = 0; i < len; i++) {
		unsigned char c = (unsigned char)text[i];
		CharInfo *ci = (c < 128) ? &((FontAtlas *)font)->chars[c] : &((FontAtlas *)font)->chars[32];

		int glyph_w = (int)(ci->x1 - ci->x0);
		int glyph_h = (int)(ci->y1 - ci->y0);

		if (glyph_w > 0 && glyph_h > 0) {
			int dst_x = ta->cursor_x + (int)(x_cursor + ci->x0);
			int dst_y = ta->cursor_y + (int)ci->y0;

			// Blit glyph from font atlas into text atlas
			for (int gy = 0; gy < glyph_h; gy++) {
				int sy = ci->src_y0 + gy;
				int dy = dst_y + gy;
				if (sy < 0 || sy >= font->height || dy < 0 || dy >= ta->height)
					continue;
				for (int gx = 0; gx < glyph_w; gx++) {
					int sx = ci->src_x0 + gx;
					int dx = dst_x + gx;
					if (sx < 0 || sx >= font->width || dx < 0 || dx >= ta->width)
						continue;
					ta->pixels[dy * ta->width + dx] = font->atlasData[sy * font->width + sx];
				}
			}
		}

		x_cursor += ci->xadvance;
	}

	// Output UV and pixel dimensions
	out->u0 = (float)ta->cursor_x / ta->width;
	out->v0 = (float)ta->cursor_y / ta->height;
	out->u1 = (float)(ta->cursor_x + (int)(total_width + 0.5f)) / ta->width;
	out->v1 = (float)(ta->cursor_y + text_h) / ta->height;
	out->width_px = total_width;
	out->height_px = (float)text_h;

	// Advance cursor
	ta->cursor_x += (int)(total_width + 0.5f) + 1;
	if (text_h > ta->row_height)
		ta->row_height = text_h;
	ta->dirty = true;
}

void text_atlas_ensure_uploaded(TextAtlas *ta, VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue queue)
{
	if (!ta->dirty)
		return;

	VkDeviceSize imgSize = (VkDeviceSize)ta->width * ta->height;

	if (ta->image == VK_NULL_HANDLE) {
		createImage(device, physicalDevice, ta->width, ta->height, VK_FORMAT_R8_UNORM, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &ta->image, &ta->memory);

		VkImageViewCreateInfo viewInfo = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = ta->image, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = VK_FORMAT_R8_UNORM, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
		vkCreateImageView(device, &viewInfo, NULL, &ta->view);

		transitionImageLayout(device, commandPool, queue, ta->image, VK_FORMAT_R8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	} else {
		transitionImageLayout(device, commandPool, queue, ta->image, VK_FORMAT_R8_UNORM, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	}

	// Upload via staging buffer
	VkBuffer stagingBuf;
	VkDeviceMemory stagingMem;
	createBuffer(device, physicalDevice, imgSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &stagingBuf, &stagingMem);

	void *mapped;
	vkMapMemory(device, stagingMem, 0, imgSize, 0, &mapped);
	memcpy(mapped, ta->pixels, imgSize);
	vkUnmapMemory(device, stagingMem);

	VkCommandBuffer cmd = begin_single_time_commands(device, commandPool);
	VkBufferImageCopy region = {.bufferOffset = 0, .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageExtent = {(uint32_t)ta->width, (uint32_t)ta->height, 1}};
	vkCmdCopyBufferToImage(cmd, stagingBuf, ta->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
	end_single_time_commands(device, commandPool, queue, cmd);

	vkDestroyBuffer(device, stagingBuf, NULL);
	vkFreeMemory(device, stagingMem, NULL);

	transitionImageLayout(device, commandPool, queue, ta->image, VK_FORMAT_R8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	ta->dirty = false;
}

void text_atlas_destroy(TextAtlas *ta, VkDevice device)
{
	if (ta->view != VK_NULL_HANDLE)
		vkDestroyImageView(device, ta->view, NULL);
	if (ta->image != VK_NULL_HANDLE)
		vkDestroyImage(device, ta->image, NULL);
	if (ta->memory != VK_NULL_HANDLE)
		vkFreeMemory(device, ta->memory, NULL);
	free(ta->pixels);
	ta->pixels = NULL;
	ta->image = VK_NULL_HANDLE;
	ta->memory = VK_NULL_HANDLE;
	ta->view = VK_NULL_HANDLE;
}
