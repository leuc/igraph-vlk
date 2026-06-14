#ifndef RT_BASE_H
#define RT_BASE_H

#include "vulkan/vulkan_types.h"
#include <igraph.h>
#include <stdbool.h>

// Opaque handle to reusable RT infrastructure
typedef struct RTBase RTBase;

// One-time init / destroy
bool rt_base_check_support(VkPhysicalDevice device);
RTBase *rt_base_create(VulkanCore *core);
void rt_base_destroy(RTBase *base);

// Per-session setup / teardown (creates BLAS, TLAS, instance buffer, cmd pool, staging)
bool rt_base_session_init(RTBase *base, igraph_t *graph, igraph_matrix_t *init_positions, float search_radius);
void rt_base_session_cleanup(RTBase *base);

// Accessors
bool rt_base_is_supported(RTBase *base);
VkBuffer rt_base_instance_buf(RTBase *base);
VkAccelerationStructureKHR rt_base_tlas(RTBase *base);
VkBuffer rt_base_node_staging_buf(RTBase *base);
uint32_t rt_base_vcount(RTBase *base);
uint32_t rt_base_ecount(RTBase *base);
bool rt_base_fp64_supported(RTBase *base);

// Buffer creation helpers (for algorithm-owned buffers)
void rt_base_create_buffer(RTBase *base, VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer *buf, VkDeviceMemory *mem);
void rt_base_create_device_buffer(RTBase *base, VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer *buf, VkDeviceMemory *mem);
void rt_base_staging_upload(RTBase *base, VkBuffer dst, const void *data, VkDeviceSize size, bool thread_safe);

// Command buffer lifecycle: begin, record dispatches, record TLAS update, submit
VkCommandBuffer rt_base_begin_commands(RTBase *base);
void rt_base_record_tlas_update(RTBase *base, VkCommandBuffer cmd, uint32_t active_vcount);
bool rt_base_submit_commands(RTBase *base, VkFence fence);

// Position readback (algorithm must have copied node_buf -> node_staging_buf first)
bool rt_base_readback_positions(RTBase *base, uint32_t vcount, igraph_matrix_t *out);

#endif // RT_BASE_H
