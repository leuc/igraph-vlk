#ifndef TEXT_H
#define TEXT_H

#include <stdint.h>

typedef struct {
	float x0, y0, x1, y1; // Quad bounds
	float u0, v0, u1, v1; // Texture coordinates
	float xadvance;
} CharInfo;

typedef struct {
	uint8_t *atlasData;
	int width, height;
	CharInfo chars[128];
} FontAtlas;

int text_generate_atlas(const char *fontPath, FontAtlas *atlas);
void text_free_atlas(FontAtlas *atlas);

#endif
