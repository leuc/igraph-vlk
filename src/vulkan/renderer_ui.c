#include "vulkan/renderer_ui.h"

#include <string.h>

#include "vulkan/renderer_geometry.h"
#include "vulkan/text.h"
#include "vulkan/utils.h"

extern FontAtlas globalAtlas;

void renderer_update_ui(Renderer *r, const char *text) {
	// Max text characters (reserve space for crosshair and optional numeric value)
	int max_text_len = 1024 - 1; // total 1024 instances, reserve last for crosshair
	int total_len = 0;

	UIInstance instances[1024];
	float xoff = -0.98f;
	float scale = 0.65f;

	// Start with main HUD text
	int len = strlen(text);
	if (len > max_text_len)
		len = max_text_len;
	for (int i = 0; i < len; i++) {
		unsigned char c = text[i];
		CharInfo *ci =
			(c < 128) ? &globalAtlas.chars[c] : &globalAtlas.chars[32];
		instances[total_len].screenPos[0] = xoff;
		instances[total_len].screenPos[1] = 0.95f;
		instances[total_len].charRect[0] = ci->x0 * scale;
		instances[total_len].charRect[1] = ci->y0 * scale;
		instances[total_len].charRect[2] = ci->x1 * scale;
		instances[total_len].charRect[3] = ci->y1 * scale;
		instances[total_len].charUV[0] = ci->u0;
		instances[total_len].charUV[1] = ci->v0;
		instances[total_len].charUV[2] = ci->u1;
		instances[total_len].charUV[3] = ci->v1;
		instances[total_len].color[0] = 1;
		instances[total_len].color[1] = 1;
		instances[total_len].color[2] = 1;
		instances[total_len].color[3] = 1;
		xoff += (ci->xadvance * scale) / 1720.0f;
		total_len++;
	}

	// Optionally add numeric widget value as HUD element
	if (r->showNumericValue && r->numericValueString[0] != '\0') {
		xoff = -0.98f;
		len = strlen(r->numericValueString);
		if (total_len + len < 1024 - 1) {
			for (int i = 0; i < len; i++) {
				unsigned char c = r->numericValueString[i];
				CharInfo *ci = (c < 128) ? &globalAtlas.chars[c] : &globalAtlas.chars[32];
				instances[total_len].screenPos[0] = xoff;
				instances[total_len].screenPos[1] = -0.85f; // bottom HUD
				instances[total_len].charRect[0] = ci->x0 * scale;
				instances[total_len].charRect[1] = ci->y0 * scale;
				instances[total_len].charRect[2] = ci->x1 * scale;
				instances[total_len].charRect[3] = ci->y1 * scale;
				instances[total_len].charUV[0] = ci->u0;
				instances[total_len].charUV[1] = ci->v0;
				instances[total_len].charUV[2] = ci->u1;
				instances[total_len].charUV[3] = ci->v1;
				instances[total_len].color[0] = 1.0f;
				instances[total_len].color[1] = 1.0f;
				instances[total_len].color[2] = 0.0f; // yellow
				instances[total_len].color[3] = 1.0f;
				xoff += (ci->xadvance * scale) / 1720.0f;
				total_len++;
			}
		}
	}

	// Add crosshair at the center
	unsigned char crossChar = '+';
	CharInfo *ci_cross = &globalAtlas.chars[crossChar];
	instances[total_len].screenPos[0] = 0.0f;
	instances[total_len].screenPos[1] = 0.0f;
	float crossScale = 1.5f;
	float cw = (ci_cross->x1 - ci_cross->x0) * crossScale;
	float ch = (ci_cross->y1 - ci_cross->y0) * crossScale;
	instances[total_len].charRect[0] = -cw * 0.5f;
	instances[total_len].charRect[1] = -ch * 0.5f;
	instances[total_len].charRect[2] = cw * 0.5f;
	instances[total_len].charRect[3] = ch * 0.5f;
	instances[total_len].charUV[0] = ci_cross->u0;
	instances[total_len].charUV[1] = ci_cross->v0;
	instances[total_len].charUV[2] = ci_cross->u1;
	instances[total_len].charUV[3] = ci_cross->v1;
	instances[total_len].color[0] = 0.0f;
	instances[total_len].color[1] = 1.0f;
	instances[total_len].color[2] = 0.0f;
	instances[total_len].color[3] = 1.0f;
	total_len++;

	r->uiTextCharCount = total_len;
	updateBuffer(r->device, r->uiTextInstanceBufferMemory,
				 sizeof(UIInstance) * r->uiTextCharCount, instances);
}
