#ifndef RENDERER_UI_H
#define RENDERER_UI_H

#include "vulkan/renderer.h"

// Update the UI text display (crosshair + text overlay)
void renderer_update_ui(Renderer *r, const char *text);

#endif
