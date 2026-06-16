/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef RENDERER_XR_H
#define RENDERER_XR_H

#include "vulkan/vulkan_types.h"
#include "xr/openxr_context.h"

void renderer_setup_xr(Renderer *r, XrContext *xr);

#endif
