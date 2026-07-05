/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef RENDERER_COMPUTE_H
#define RENDERER_COMPUTE_H

#include "renderer.h"

// Compute shader data structures (forward declarations for public API)
typedef struct
{
	vec3 position;
	float pad1;
	vec3 color;
	float size;
	int degree;
	int pad2, pad3, pad4;
} CompNode;

typedef struct
{
	int sourceId;
	int targetId;
	int elevationLevel;
	int pathLength;
	vec4 path[16];
} CompEdge;

typedef struct
{
	float position[3];
	float pad;
} CompHub;

/**
 * Dispatch edge routing computation on the GPU.
 *
 * This function runs the compute shader to calculate curved edge paths
 * between graph nodes using spherical PCB routing or 3D hub-spoke routing.
 *
 * @param r           The renderer instance
 * @param graph       The graph data containing nodes, edges, and hubs
 * @param edgeResults Output buffer to receive computed edge paths (must be sized
 *                    for graph->edge_count elements)
 * @return VK_SUCCESS on success, Vulkan error code on failure
 */
VkResult renderer_dispatch_edge_routing(Renderer *r, GraphData *graph, CompEdge *edgeResults);

/**
 * Initialize SPLC buffers from a GraphData instance.
 * Creates node/edge topology buffers and traffic buffers for the compute shader.
 * Must be called after graph is loaded or changes.
 */
bool renderer_init_splc_buffers(Renderer *r, GraphData *graph);

/**
 * Dispatch one level of the SPLC animation.
 * Processes all nodes at splc_current_level, pushing traffic forward.
 * Must be called between vkBeginCommandBuffer and vkCmdBeginRenderPass.
 */
void renderer_dispatch_splc_level(Renderer *r, VkCommandBuffer cmd);

/**
 * Read back SPLC edge weights from the GPU and sync them to host-side GraphData.
 * Maps the GPU edge buffer, walks the same adjacency traversal used during init,
 * and writes weights to graph->edges[].weight via igraph_get_eid.
 */
void renderer_readback_splc_weights(Renderer *r, GraphData *graph);

#endif
