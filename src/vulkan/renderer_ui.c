#include "vulkan/renderer_ui.h"

#include <string.h>

#include "vulkan/renderer_geometry.h"
#include "vulkan/text.h"
#include "vulkan/utils.h"

extern FontAtlas globalAtlas;

void renderer_update_ui(Renderer *r, const char *text) {
	// Max text characters (1024 - 1 for crosshair)
	int max_text_len = 1024 - 1;

	int len = strlen(text);
	if (len > max_text_len) // Cap text length
		len = max_text_len;
	UIInstance
		instances[1024]; // This array can hold up to 1024 elements (0 to 1023)
	float xoff = -0.98f;
	float scale = 0.65f;
	for (int i = 0; i < len; i++) {
		unsigned char c = text[i];
		CharInfo *ci =
			(c < 128) ? &globalAtlas.chars[c] : &globalAtlas.chars[32];
		instances[i].screenPos[0] = xoff;
		instances[i].screenPos[1] = 0.95f;
		instances[i].charRect[0] = ci->x0 * scale;
		instances[i].charRect[1] = ci->y0 * scale;
		instances[i].charRect[2] = ci->x1 * scale;
		instances[i].charRect[3] = ci->y1 * scale;
		instances[i].charUV[0] = ci->u0;
		instances[i].charUV[1] = ci->v0;
		instances[i].charUV[2] = ci->u1;
		instances[i].charUV[3] = ci->v1;
		instances[i].color[0] = 1;
		instances[i].color[1] = 1;
		instances[i].color[2] = 1;
		instances[i].color[3] = 1;
		xoff += (ci->xadvance * scale) / 1720.0f;
	}

	// Add crosshair at the center
	unsigned char crossChar = '+';
	CharInfo *ci_cross = &globalAtlas.chars[crossChar];
	instances[len].screenPos[0] = 0.0f;
	instances[len].screenPos[1] = 0.0f;
	float crossScale = 1.5f;
	float cw = (ci_cross->x1 - ci_cross->x0) * crossScale;
	float ch = (ci_cross->y1 - ci_cross->y0) * crossScale;
	instances[len].charRect[0] = -cw * 0.5f;
	instances[len].charRect[1] = -ch * 0.5f;
	instances[len].charRect[2] = cw * 0.5f;
	instances[len].charRect[3] = ch * 0.5f;
	instances[len].charUV[0] = ci_cross->u0;
	instances[len].charUV[1] = ci_cross->v0;
	instances[len].charUV[2] = ci_cross->u1;
	instances[len].charUV[3] = ci_cross->v1;
	instances[len].color[0] = 0.0f;
	instances[len].color[1] = 1.0f;
	instances[len].color[2] = 0.0f;
	instances[len].color[3] = 1.0f;

	r->uiTextCharCount = len + 1;
	updateBuffer(r->device, r->uiTextInstanceBufferMemory,
				 sizeof(UIInstance) * r->uiTextCharCount, instances);
}
