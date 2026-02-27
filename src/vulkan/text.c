#define STB_TRUETYPE_IMPLEMENTATION
#include "vulkan/text.h"

#include <stb/stb_truetype.h>
#include <stdio.h>
#include <stdlib.h>

int text_generate_atlas(const char *fontPath, FontAtlas *atlas) {
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
	stbtt_BakeFontBitmap(fontBuffer, 0, 32.0, atlas->atlasData, atlas->width,
						 atlas->height, 32, 96, baked);

	for (int i = 0; i < 128; i++) {
		if (i >= 32 && i < 128) {
			stbtt_bakedchar *b = &baked[i - 32];
			atlas->chars[i].x0 = b->xoff;
			atlas->chars[i].y0 = b->yoff;
			atlas->chars[i].x1 = b->xoff + (b->x1 - b->x0);
			atlas->chars[i].y1 = b->yoff + (b->y1 - b->y0);
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

void text_free_atlas(FontAtlas *atlas) { free(atlas->atlasData); }
