#ifndef RT_BARNES_HUT_H
#define RT_BARNES_HUT_H

#include "vulkan/vulkan_types.h"
#include <stdbool.h>
#include <stdint.h>

// ============================================================================
// Opaque handle to decoupled Barnes-Hut RT force computation engine.
//
// Provides:
//   - CPU: octree construction from particle positions (via barnes_hut_tree.h)
//   - GPU: triangle-mesh BLAS + TLAS over the DFS-linearized octree
//   - GPU: single compute dispatch (bh_force.comp) that walks the octree via
//          rayQueryEXT to accumulate O(N log N) repulsive forces
//
// Caller (any layout algorithm) provides a VkCommandBuffer into which the
// build and dispatch commands are recorded. The caller manages submission
// and synchronization. This decoupling allows the BH module to be inserted
// into any layout pipeline (YHRT, t-SNE, etc.) without owning the submission.
// ============================================================================

typedef struct BarnesHutRT BarnesHutRT;

// ---------------------------------------------------------------------------
// One-time lifecycle (called from renderer_create_pipelines)
// ---------------------------------------------------------------------------
BarnesHutRT *bhrt_create(VulkanCore *core);
void bhrt_destroy(BarnesHutRT *bh);
bool bhrt_is_supported(BarnesHutRT *bh);

// ---------------------------------------------------------------------------
// Session lifecycle (called by a layout algorithm)
// ---------------------------------------------------------------------------

// Initialize a BH session: upload initial positions, create descriptor pool
// and descriptor set. Does not build the octree or BLAS (that happens in bhrt_build).
bool bhrt_session_init(BarnesHutRT *bh, const float *positions, const float *masses, uint32_t particle_count, float theta, float G);

// Build octree from current positions, create triangle-mesh BLAS + TLAS.
// Records BLAS/TLAS build commands into the provided command buffer.
// The caller must ensure: cmd is in recording state, and a barrier to
// COMPUTE_SHADER_BIT follows before bhrt_record_dispatch.
void bhrt_build(BarnesHutRT *bh, const float *positions, VkCommandBuffer cmd);

// Record the bh_force.comp dispatch into cmd.
// The caller must ensure:
//   - BLAS/TLAS have been built (bhrt_build was called and submitted)
//   - A pipeline barrier from ACCELERATION_STRUCTURE_BUILD to COMPUTE exists
//   - cmd is in recording state
//   - The descriptor set's TLAS binding is valid (bhrt_build updated it)
void bhrt_record_dispatch(BarnesHutRT *bh, VkCommandBuffer cmd);

// Access the force output buffer (for binding to caller's own descriptor sets)
VkBuffer bhrt_force_buffer(BarnesHutRT *bh);

// Read back forces to CPU (blocking, maps staging buffer)
void bhrt_readback_forces(BarnesHutRT *bh, float *out_forces);

// Session cleanup: destroy octree data, BLAS, TLAS, staging buffers.
// Does NOT destroy pipelines/descriptor layouts (those persist across sessions).
void bhrt_session_cleanup(BarnesHutRT *bh);

#endif // RT_BARNES_HUT_H
