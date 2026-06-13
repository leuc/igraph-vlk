#ifndef RT_LAYOUT_H
#define RT_LAYOUT_H

#include "vulkan/rt_base.h"
#include "vulkan/vulkan_types.h"
#include <igraph.h>
#include <stdbool.h>

void yhrt_init_pipelines(Renderer *r);

void yhrt_init_pipelines(Renderer *r);

void yhrt_destroy(Renderer *r);

// Worker-thread API: blocking GPU iterations with igraph_progress
bool yhrt_worker_init(Renderer *r, igraph_t *graph, igraph_matrix_t *init_positions, igraph_int_t maxiter);
bool yhrt_worker_step(Renderer *r);
bool yhrt_worker_readback(Renderer *r, igraph_matrix_t *out_positions);
void yhrt_worker_cleanup(Renderer *r);

#endif // RT_LAYOUT_H
