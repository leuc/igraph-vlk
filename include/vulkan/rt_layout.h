#ifndef RT_LAYOUT_H
#define RT_LAYOUT_H

#include "vulkan/vulkan_types.h"
#include <igraph.h>
#include <stdbool.h>

bool yhrt_check_support(VkPhysicalDevice device);

void yhrt_init_pipelines(Renderer *r);

void yhrt_start(Renderer *r, igraph_t *graph, igraph_matrix_t *init_positions, igraph_int_t maxiter);

bool yhrt_dispatch_step(Renderer *r, VkCommandBuffer cmd);

void yhrt_finish(Renderer *r, GraphData *graph);

void yhrt_destroy(Renderer *r);

#endif // RT_LAYOUT_H
