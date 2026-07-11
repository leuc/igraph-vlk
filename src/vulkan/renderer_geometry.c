/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "vulkan/renderer_geometry.h"
#include "vulkan/renderer_compute.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "interaction/camera.h"
#include "interaction/state.h"
#include "vulkan/buffers.h"
#include "vulkan/utils.h"

void renderer_update_graph(Renderer *r, GraphData *graph)
{
	// Ring-buffered fence sync instead of vkDeviceWaitIdle
	uint32_t ringIdx = r->graphUpdateRingIndex;
	VK_CHECK(vkWaitForFences(r->core.device, 1, &r->graphUpdateFences[ringIdx], VK_TRUE, UINT64_MAX), "Failed to wait for graph update fences");
	VK_CHECK(vkResetFences(r->core.device, 1, &r->graphUpdateFences[ringIdx]), "Failed to reset graph update fences");

	// If edge count changed while SPLC was active, reset to avoid stale/out-of-bounds reads
	if (r->splc.active && r->edge.count != graph->edge_count) {
		r->splc.active = false;
	}

	r->node.count = graph->node_count;
	r->edge.count = graph->edge_count;

	// Pre-allocate or grow node buffers (split: position + attribute)
	if (r->node.capacity < graph->node_count) {
		// Wait for all in-flight draw frames — command buffers may still reference these buffers
		vkWaitForFences(r->core.device, MAX_FRAMES_IN_FLIGHT, r->commands.inFlightFences, VK_TRUE, UINT64_MAX);
		VK_DESTROY_BUFFER(r->core.device, r->node.position, r->node.position_memory);
		VK_DESTROY_BUFFER(r->core.device, r->node.attribute, r->node.attribute_memory);
		VK_DESTROY_BUFFER(r->core.device, r->node.staging, r->node.staging_memory);
		// Position buffer: HOST_COHERENT for fast mapped updates
		create_mapped_buffer(r->core.device, r->core.physicalDevice, sizeof(NodePosition) * graph->node_count, &r->node.position, &r->node.position_memory);
		// Attribute buffer: DEVICE_LOCAL with staging buffer for rare updates
		create_staging_buffer(r->core.device, r->core.physicalDevice, sizeof(NodeAttribute) * graph->node_count, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &r->node.staging, &r->node.staging_memory, &r->node.attribute, &r->node.attribute_memory);
		r->node.capacity = graph->node_count;
		r->needsAttributeUpload = VK_TRUE;
	} else if (graph->node_count < r->node.count) {
		// Node count decreased - need to re-upload attributes
		r->needsAttributeUpload = VK_TRUE;
	}

	// Pre-allocate or grow edge buffers (split: position + attribute)
	int segments = (r->currentRoutingMode == ROUTING_MODE_STRAIGHT) ? 1 : 15;
	r->edge.vertex_count = graph->edge_count * segments * 2;
	uint32_t neededEdgeVerts = r->edge.vertex_count;
	if (r->edge.capacity < neededEdgeVerts) {
		// Wait for all in-flight draw frames — command buffers may still reference these buffers
		vkWaitForFences(r->core.device, MAX_FRAMES_IN_FLIGHT, r->commands.inFlightFences, VK_TRUE, UINT64_MAX);
		VK_DESTROY_BUFFER(r->core.device, r->edge.position, r->edge.position_memory);
		VK_DESTROY_BUFFER(r->core.device, r->edge.attribute, r->edge.attribute_memory);
		VK_DESTROY_BUFFER(r->core.device, r->edge.staging, r->edge.staging_memory);
		// Position buffer: HOST_COHERENT for fast mapped updates
		create_mapped_buffer(r->core.device, r->core.physicalDevice, sizeof(EdgePosition) * neededEdgeVerts, &r->edge.position, &r->edge.position_memory);
		// Attribute buffer: DEVICE_LOCAL with staging buffer for rare updates
		create_staging_buffer(r->core.device, r->core.physicalDevice, sizeof(EdgeAttribute) * neededEdgeVerts, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &r->edge.staging, &r->edge.staging_memory, &r->edge.attribute, &r->edge.attribute_memory);
		r->edge.capacity = neededEdgeVerts;
		r->needsAttributeUpload = VK_TRUE;
	}

	// Build node instances
	NodePosition *nodePositions = malloc(sizeof(NodePosition) * graph->node_count);
	NodeAttribute *nodeAttributes = malloc(sizeof(NodeAttribute) * graph->node_count);
	for (uint32_t i = 0; i < graph->node_count; i++) {
		float size = graph->nodes[i].size;
		if (size < NODE_SIZE_MIN)
			size = NODE_SIZE_MIN;
		glm_vec3_scale(graph->nodes[i].position, r->layoutScale, nodePositions[i].pos);
		memcpy(nodeAttributes[i].color, graph->nodes[i].color, sizeof(vec3));
		nodeAttributes[i].size = size;
		nodeAttributes[i].degree = graph->nodes[i].degree;
		nodeAttributes[i].selected = graph->nodes[i].selected;
		nodeAttributes[i].visible = graph->nodes[i].visible;
	}
	// Fast path: update positions via mapped buffer
	update_buffer_mapped(r->core.device, r->node.position_memory, sizeof(NodePosition) * graph->node_count, nodePositions, &r->core.deviceProperties);
	// Rare: update attributes via staged copy
	if (r->needsAttributeUpload) {
		update_buffer_staged(r->core.device, r->commands.commandPool, r->core.graphicsQueue, sizeof(NodeAttribute) * graph->node_count, nodeAttributes, r->node.staging, r->node.staging_memory, r->node.attribute, &r->core.deviceProperties);
	}
	free(nodePositions);
	free(nodeAttributes);

	// Split edge data into position + attribute buffers
	EdgePosition *edgePositions = calloc(r->edge.vertex_count, sizeof(EdgePosition));
	EdgeAttribute *edgeAttributes = calloc(r->edge.vertex_count, sizeof(EdgeAttribute));

	// Dispatch edge routing compute shader if needed
	CompEdge *cEdges = NULL;
	if (r->currentRoutingMode != ROUTING_MODE_STRAIGHT) {
		cEdges = malloc(sizeof(CompEdge) * graph->edge_count);
		renderer_dispatch_edge_routing(r, graph, cEdges);
	}

	uint32_t idx = 0;
	if (cEdges) {
		for (uint32_t i = 0; i < graph->edge_count; i++) {
			int pLen = cEdges[i].pathLength;
			if (pLen < 0)
				pLen = 0;
			if (pLen > 16)
				pLen = 16;

			float total_length = 0.0f;
			if (pLen > 1) {
				for (int p = 0; p < pLen - 1; ++p) {
					vec3 p1, p2;
					p1[0] = cEdges[i].path[p][0];
					p1[1] = cEdges[i].path[p][1];
					p1[2] = cEdges[i].path[p][2];
					p2[0] = cEdges[i].path[p + 1][0];
					p2[1] = cEdges[i].path[p + 1][1];
					p2[2] = cEdges[i].path[p + 1][2];
					total_length += glm_vec3_distance(p1, p2);
				}
			}

			float current_segment_start_len = 0.0f;
			for (int p = 0; p < pLen - 1; p++) {
				float segment_length = 0.0f;
				if (pLen > 1) {
					vec3 p1, p2;
					p1[0] = cEdges[i].path[p][0];
					p1[1] = cEdges[i].path[p][1];
					p1[2] = cEdges[i].path[p][2];
					p2[0] = cEdges[i].path[p + 1][0];
					p2[1] = cEdges[i].path[p + 1][1];
					p2[2] = cEdges[i].path[p + 1][2];
					segment_length = glm_vec3_distance(p1, p2);
				}

				memcpy(edgePositions[idx].pos, cEdges[i].path[p], 12);
				memcpy(edgeAttributes[idx].color, graph->nodes[graph->edges[i].from].color, 12);
				edgeAttributes[idx].selected = graph->edges[i].selected;
				edgeAttributes[idx].normalized_pos = current_segment_start_len / (total_length > 0.0f ? total_length : 1.0f);
				edgeAttributes[idx].visible = (graph->nodes[graph->edges[i].from].visible > 0.5f && graph->nodes[graph->edges[i].to].visible > 0.5f) ? 1.0f : 0.0f;
				idx++;

				memcpy(edgePositions[idx].pos, cEdges[i].path[p + 1], 12);
				memcpy(edgeAttributes[idx].color, graph->nodes[graph->edges[i].to].color, 12);
				edgeAttributes[idx].selected = graph->edges[i].selected;
				edgeAttributes[idx].normalized_pos = (current_segment_start_len + segment_length) / (total_length > 0.0f ? total_length : 1.0f);
				edgeAttributes[idx].visible = edgeAttributes[idx - 1].visible;
				idx++;
				current_segment_start_len += segment_length;
			}
		}
		if (cEdges)
			free(cEdges);
	} else {
		for (uint32_t i = 0; i < graph->edge_count; i++) {
			vec3 p1, p2;
			glm_vec3_scale(graph->nodes[graph->edges[i].from].position, r->layoutScale, p1);
			glm_vec3_scale(graph->nodes[graph->edges[i].to].position, r->layoutScale, p2);

			float edge_vis = (graph->nodes[graph->edges[i].from].visible > 0.5f && graph->nodes[graph->edges[i].to].visible > 0.5f) ? 1.0f : 0.0f;

			memcpy(edgePositions[idx].pos, p1, 12);
			memcpy(edgeAttributes[idx].color, graph->nodes[graph->edges[i].from].color, 12);
			edgeAttributes[idx].selected = graph->edges[i].selected;
			edgeAttributes[idx].normalized_pos = 0.0f;
			edgeAttributes[idx].visible = edge_vis;
			idx++;

			memcpy(edgePositions[idx].pos, p2, 12);
			memcpy(edgeAttributes[idx].color, graph->nodes[graph->edges[i].to].color, 12);
			edgeAttributes[idx].selected = graph->edges[i].selected;
			edgeAttributes[idx].normalized_pos = 1.0f;
			edgeAttributes[idx].visible = edge_vis;
			idx++;
		}
	}

	r->edge.vertex_count = idx;

	// Fast path: update positions via mapped buffer
	if (r->edge.vertex_count > 0) {
		update_buffer_mapped(r->core.device, r->edge.position_memory, sizeof(EdgePosition) * r->edge.vertex_count, edgePositions, &r->core.deviceProperties);
	}
	// Rare: update attributes via staged copy (only when flag set)
	if (r->needsAttributeUpload && r->edge.vertex_count > 0) {
		update_buffer_staged(r->core.device, r->commands.commandPool, r->core.graphicsQueue, sizeof(EdgeAttribute) * r->edge.vertex_count, edgeAttributes, r->edge.staging, r->edge.staging_memory, r->edge.attribute, &r->core.deviceProperties);
	}
	free(edgePositions);
	free(edgeAttributes);
	r->needsAttributeUpload = VK_FALSE;
	r->label.tree_needs_rebuild = true;

	// Signal the ring fence for this slot so the next update can proceed
	VK_CHECK(vkQueueSubmit(r->core.graphicsQueue, 0, NULL, r->graphUpdateFences[ringIdx]), "Failed to signal graph update fence");
}

void renderer_render_ray(Renderer *r, VkCommandBuffer cmd, vec3 origin, vec3 dir, mat4 view, mat4 proj)
{
	// Ray length
	float length = 5.0f;
	vec3 end;
	glm_vec3_scale(dir, length, end);
	glm_vec3_add(origin, end, end);

	// Ray vertices (pos: vec3, color: vec4)
	float vertices[] = {
		origin[0], origin[1], origin[2], 1.0f, 1.0f, 1.0f, 1.0f, // Origin (white)
		end[0],	   end[1],	  end[2],	 1.0f, 0.0f, 0.0f, 0.5f	 // End (red, semi-transparent)
	};

	// Update ray buffer (host-visible)
	if (r->rayVertexBuffer == VK_NULL_HANDLE) {
		VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, sizeof(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &r->rayVertexBuffer, &r->rayVertexBufferMemory);
		r->rayVertexCount = 2;
	}
	update_buffer(r->core.device, r->rayVertexBufferMemory, sizeof(vertices), vertices);

	// Draw
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipelines.ray);

	struct
	{
		mat4 view;
		mat4 proj;
	} push;
	glm_mat4_copy(view, push.view);
	glm_mat4_copy(proj, push.proj);
	vkCmdPushConstants(cmd, r->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);

	VkDeviceSize offset = 0;
	vkCmdBindVertexBuffers(cmd, 0, 1, &r->rayVertexBuffer, &offset);
	vkCmdDraw(cmd, 2, 1, 0, 0);
}
