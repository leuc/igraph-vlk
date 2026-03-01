#ifndef RENDERER_UI_H
#define RENDERER_UI_H

#include "vulkan/renderer.h"
#include <cglm/cglm.h>

typedef struct {
    vec3 position;
    float scale;
    int icon_index;
    float padding; // Align to 32 bytes if needed
} MenuInstanceData;



// Legacy UI
void renderer_update_ui(Renderer *r, const char *text);

#endif
