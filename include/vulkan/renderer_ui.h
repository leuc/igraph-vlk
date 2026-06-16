/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef RENDERER_UI_H
#define RENDERER_UI_H

#include "vulkan/renderer.h"
#include <cglm/cglm.h>

// Redundant definitions moved to vulkan_types.h

void renderer_update_ui(Renderer *r, const char *text);

#endif
