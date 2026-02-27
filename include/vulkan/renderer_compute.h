#ifndef RENDERER_COMPUTE_H
#define RENDERER_COMPUTE_H

#include "renderer.h"

// Compute shader data structures (forward declarations for public API)
typedef struct {
	vec3 position;
	float pad1;
	vec3 color;
	float size;
	int degree;
	int pad2, pad3, pad4;
} CompNode;

typedef struct {
	int sourceId;
	int targetId;
	int elevationLevel;
	int pathLength;
	vec4 path[16];
	float animation_progress;
	int animation_direction;
	int is_animating;
	int pad;
} CompEdge;

typedef struct {
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

#endif
