/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef RENDERER_UPDATE_NODE_LABELS_H
#define RENDERER_UPDATE_NODE_LABELS_H

#include "vulkan/vulkan_types.h"

void detail_card_update(Renderer *r, GraphData *graph, int selected_node);
void label_upload_and_update_descriptors(Renderer *r, uint32_t inst_count, NodeLabelInstance *instances);

#endif
