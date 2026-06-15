#ifndef RENDERER_BCGL_H
#define RENDERER_BCGL_H

#include "renderer.h"

/**
 * Create the BCGL compute pipeline, descriptor set layout, and pipeline layout.
 * Called once from renderer_create_compute_pipelines.
 */
void renderer_create_bcgl_compute_pipeline(Renderer *r);

/**
 * Initialize BCGL compute buffers (node positions + CSR topology).
 * Destroys any prior buffers, seeds node positions from current layout.
 */
void renderer_init_bcgl_buffers(Renderer *r, GraphData *graph);

/**
 * Dispatch the BCGL layout optimization on the GPU.
 * Runs `iterations` SGD steps, blocking until complete.
 */
void renderer_dispatch_bcgl_layout(Renderer *r, GraphData *graph, uint32_t iterations);

/**
 * Read back BCGL node positions from GPU and write to graph->nodes[].position.
 * Also updates graph->current_layout matrix.
 */
void renderer_readback_bcgl_positions(Renderer *r, GraphData *graph);

/**
 * Destroy BCGL compute resources (pipeline, buffers, descriptor pool).
 */
void renderer_cleanup_bcgl(Renderer *r);

#endif // RENDERER_BCGL_H
