/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef RENDERER_TRANSITION_H
#define RENDERER_TRANSITION_H

#include "graph/graph_types.h"
#include "vulkan/vulkan_types.h"

void renderer_transition_init(Renderer *r);
void renderer_transition_cleanup(Renderer *r);
void renderer_transition_request(Renderer *r, TransitionSource source, float duration, GraphData *graph);
void renderer_transition_update(Renderer *r, float delta_time);
bool renderer_transition_is_active(Renderer *r);

#endif
