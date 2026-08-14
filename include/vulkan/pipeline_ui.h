/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef PIPELINE_UI_H
#define PIPELINE_UI_H

#include "vulkan/vulkan_types.h"

void renderer_create_ui_pipelines(Renderer *r, Pipelines *pipelines, VkRenderPass render_pass, bool linear_output);

#endif // PIPELINE_UI_H
