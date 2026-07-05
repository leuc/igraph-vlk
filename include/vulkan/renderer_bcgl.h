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
bool renderer_init_bcgl_buffers(Renderer *r, GraphData *graph);

/**
 * Dispatch a chunk of BCGL layout iterations on the GPU (non-blocking).
 * Records and submits the command buffer, returns immediately.
 * Caller must check the fence before dispatching another chunk.
 */
void renderer_dispatch_bcgl_chunk(Renderer *r, GraphData *graph, uint32_t iterations);

/**
 * Check if the most recent BCGL dispatch chunk has completed (non-blocking).
 * Returns VK_SUCCESS if complete, VK_NOT_READY if still in flight.
 */
VkResult renderer_bcgl_fence_status(Renderer *r);

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
