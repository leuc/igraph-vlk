/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef RENDERER_LABELS_H
#define RENDERER_LABELS_H

#include "vulkan/vulkan_types.h"

void renderer_update_node_labels(Renderer *r, GraphData *graph, vec3 camera_pos, int selected_node);

#endif
