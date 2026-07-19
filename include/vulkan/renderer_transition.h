#ifndef RENDERER_TRANSITION_H
#define RENDERER_TRANSITION_H

#include "graph/graph_types.h"
#include "vulkan/vulkan_types.h"

void renderer_transition_init(Renderer *r);
void renderer_transition_cleanup(Renderer *r);
void renderer_transition_begin(Renderer *r, float duration);
void renderer_transition_reconcile(Renderer *r, GraphData *graph);
void renderer_transition_update(Renderer *r, float delta_time);
bool renderer_transition_is_active(Renderer *r);

#endif
