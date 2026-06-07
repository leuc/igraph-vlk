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
	if (r->nodePositionBuffer != VK_NULL_HANDLE) {
		VK_CHECK(vkWaitForFences(r->core.device, 1, &r->graphUpdateFences[ringIdx], VK_TRUE, UINT64_MAX), "Failed to wait for graph update fences");
		VK_CHECK(vkResetFences(r->core.device, 1, &r->graphUpdateFences[ringIdx]), "Failed to reset graph update fences");
	}

	// If edge count changed while SPLC was active, reset to avoid stale/out-of-bounds reads
	if (r->splc_active && r->edgeCount != graph->edge_count) {
		r->splc_active = false;
		r->splc_max_weight = 0.0f;
	}

	r->nodeCount = graph->node_count;
	r->edgeCount = graph->edge_count;

	// Pre-allocate or grow node buffers (split: position + attribute)
	if (r->nodeCapacity < graph->node_count) {
		if (r->nodePositionBuffer != VK_NULL_HANDLE) {
			vkDestroyBuffer(r->core.device, r->nodePositionBuffer, NULL);
			vkFreeMemory(r->core.device, r->nodePositionMemory, NULL);
			vkDestroyBuffer(r->core.device, r->nodeAttributeBuffer, NULL);
			vkFreeMemory(r->core.device, r->nodeAttributeMemory, NULL);
			vkDestroyBuffer(r->core.device, r->nodeAttributeStagingBuffer, NULL);
			vkFreeMemory(r->core.device, r->nodeAttributeStagingMemory, NULL);
		}
		// Position buffer: HOST_COHERENT for fast mapped updates
		create_mapped_buffer(r->core.device, r->core.physicalDevice, sizeof(NodePosition) * graph->node_count, &r->nodePositionBuffer, &r->nodePositionMemory);
		// Attribute buffer: DEVICE_LOCAL with staging buffer for rare updates
		create_staging_buffer(r->core.device, r->core.physicalDevice, sizeof(NodeAttribute) * graph->node_count, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &r->nodeAttributeStagingBuffer, &r->nodeAttributeStagingMemory, &r->nodeAttributeBuffer, &r->nodeAttributeMemory);
		r->nodeCapacity = graph->node_count;
		r->needsAttributeUpload = VK_TRUE;
	} else if (graph->node_count < r->nodeCount) {
		// Node count decreased - need to re-upload attributes
		r->needsAttributeUpload = VK_TRUE;
	}

	// Pre-allocate or grow edge buffers (split: position + attribute)
	int segments = (r->currentRoutingMode == ROUTING_MODE_STRAIGHT) ? 1 : 15;
	r->edgeVertexCount = graph->edge_count * segments * 2;
	uint32_t neededEdgeVerts = r->edgeVertexCount;
	if (r->edgeCapacity < neededEdgeVerts) {
		if (r->edgePositionBuffer != VK_NULL_HANDLE) {
			vkDestroyBuffer(r->core.device, r->edgePositionBuffer, NULL);
			vkFreeMemory(r->core.device, r->edgePositionMemory, NULL);
			vkDestroyBuffer(r->core.device, r->edgeAttributeBuffer, NULL);
			vkFreeMemory(r->core.device, r->edgeAttributeMemory, NULL);
			vkDestroyBuffer(r->core.device, r->edgeAttributeStagingBuffer, NULL);
			vkFreeMemory(r->core.device, r->edgeAttributeStagingMemory, NULL);
		}
		// Position buffer: HOST_COHERENT for fast mapped updates
		create_mapped_buffer(r->core.device, r->core.physicalDevice, sizeof(EdgePosition) * neededEdgeVerts, &r->edgePositionBuffer, &r->edgePositionMemory);
		// Attribute buffer: DEVICE_LOCAL with staging buffer for rare updates
		create_staging_buffer(r->core.device, r->core.physicalDevice, sizeof(EdgeAttribute) * neededEdgeVerts, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &r->edgeAttributeStagingBuffer, &r->edgeAttributeStagingMemory, &r->edgeAttributeBuffer, &r->edgeAttributeMemory);
		r->edgeCapacity = neededEdgeVerts;
		r->needsAttributeUpload = VK_TRUE;
	}

	// Build node instances sorted by platonic type
	Node *sorted = malloc(sizeof(Node) * graph->node_count);
	NodePosition *nodePositions = malloc(sizeof(NodePosition) * graph->node_count);
	NodeAttribute *nodeAttributes = malloc(sizeof(NodeAttribute) * graph->node_count);
	uint32_t currentOffset = 0;
	for (int t = 0; t < PLATONIC_COUNT; t++) {
		r->platonicDrawCalls[t].firstInstance = currentOffset;
		uint32_t count = 0;
		for (uint32_t i = 0; i < graph->node_count; i++) {
			int deg = graph->nodes[i].degree;
			PlatonicType pt;
			if (deg < 4)
				pt = PLATONIC_TETRAHEDRON;
			else if (deg < 6)
				pt = PLATONIC_CUBE;
			else if (deg < 8)
				pt = PLATONIC_OCTAHEDRON;
			else if (deg < 12)
				pt = PLATONIC_DODECAHEDRON;
			else
				pt = PLATONIC_ICOSAHEDRON;
			if (pt == (PlatonicType)t) {
				sorted[currentOffset + count] = graph->nodes[i];
				float size = sorted[currentOffset + count].size;
				if (size < 0.1f)
					size = 0.1f;
				// Split into position (scaled) + attributes
				glm_vec3_scale(sorted[currentOffset + count].position, r->layoutScale, nodePositions[currentOffset + count].pos);
				memcpy(nodeAttributes[currentOffset + count].color, sorted[currentOffset + count].color, sizeof(vec3));
				nodeAttributes[currentOffset + count].size = size;
				nodeAttributes[currentOffset + count].degree = sorted[currentOffset + count].degree;
				nodeAttributes[currentOffset + count].selected = sorted[currentOffset + count].selected;
				count++;
			}
		}
		r->platonicDrawCalls[t].count = count;
		currentOffset += count;
	}
	// Fast path: update positions via mapped buffer
	update_buffer_mapped(r->core.device, r->nodePositionMemory, sizeof(NodePosition) * graph->node_count, nodePositions, &r->core.deviceProperties);
	// Rare: update attributes via staged copy
	if (r->needsAttributeUpload) {
		update_buffer_staged(r->core.device, r->commands.commandPool, r->core.graphicsQueue, sizeof(NodeAttribute) * graph->node_count, nodeAttributes, r->nodeAttributeStagingBuffer, r->nodeAttributeStagingMemory, r->nodeAttributeBuffer, &r->core.deviceProperties);
	}
	free(sorted);
	free(nodePositions);
	free(nodeAttributes);

	// Split edge data into position + attribute buffers
	EdgePosition *edgePositions = calloc(r->edgeVertexCount, sizeof(EdgePosition));
	EdgeAttribute *edgeAttributes = calloc(r->edgeVertexCount, sizeof(EdgeAttribute));

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
				idx++;

				memcpy(edgePositions[idx].pos, cEdges[i].path[p + 1], 12);
				memcpy(edgeAttributes[idx].color, graph->nodes[graph->edges[i].to].color, 12);
				edgeAttributes[idx].selected = graph->edges[i].selected;
				edgeAttributes[idx].normalized_pos = (current_segment_start_len + segment_length) / (total_length > 0.0f ? total_length : 1.0f);
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

			memcpy(edgePositions[idx].pos, p1, 12);
			memcpy(edgeAttributes[idx].color, graph->nodes[graph->edges[i].from].color, 12);
			edgeAttributes[idx].selected = graph->edges[i].selected;
			edgeAttributes[idx].normalized_pos = 0.0f;
			idx++;

			memcpy(edgePositions[idx].pos, p2, 12);
			memcpy(edgeAttributes[idx].color, graph->nodes[graph->edges[i].to].color, 12);
			edgeAttributes[idx].selected = graph->edges[i].selected;
			edgeAttributes[idx].normalized_pos = 1.0f;
			idx++;
		}
	}

	r->edgeVertexCount = idx;

	// Fast path: update positions via mapped buffer
	if (r->edgeVertexCount > 0) {
		update_buffer_mapped(r->core.device, r->edgePositionMemory, sizeof(EdgePosition) * r->edgeVertexCount, edgePositions, &r->core.deviceProperties);
	}
	// Rare: update attributes via staged copy (only when flag set)
	if (r->needsAttributeUpload && r->edgeVertexCount > 0) {
		update_buffer_staged(r->core.device, r->commands.commandPool, r->core.graphicsQueue, sizeof(EdgeAttribute) * r->edgeVertexCount, edgeAttributes, r->edgeAttributeStagingBuffer, r->edgeAttributeStagingMemory, r->edgeAttributeBuffer, &r->core.deviceProperties);
	}
	free(edgePositions);
	free(edgeAttributes);
	r->needsAttributeUpload = VK_FALSE;
	r->labelTreeNeedsRebuild = true;

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
		create_buffer(r->core.device, r->core.physicalDevice, sizeof(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->rayVertexBuffer, &r->rayVertexBufferMemory);
		r->rayVertexCount = 2;
	}
	update_buffer(r->core.device, r->rayVertexBufferMemory, sizeof(vertices), vertices);

	// Draw
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->rayPipeline);

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
