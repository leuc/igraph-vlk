#include "vulkan/renderer_geometry.h"
#include "vulkan/renderer_compute.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "interaction/camera.h"
#include "interaction/state.h"
#include "vulkan/text.h"
#include "vulkan/utils.h"

extern FontAtlas globalAtlas;

void renderer_update_graph(Renderer *r, GraphData *graph)
{
	// Ring-buffered fence sync instead of vkDeviceWaitIdle
	uint32_t ringIdx = r->graphUpdateRingIndex;
	if (r->nodePositionBuffer != VK_NULL_HANDLE) {
		vkWaitForFences(r->core.device, 1, &r->graphUpdateFences[ringIdx], VK_TRUE, UINT64_MAX);
		vkResetFences(r->core.device, 1, &r->graphUpdateFences[ringIdx]);
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
		createMappedBuffer(r->core.device, r->core.physicalDevice, sizeof(NodePosition) * graph->node_count, &r->nodePositionBuffer, &r->nodePositionMemory);
		// Attribute buffer: DEVICE_LOCAL with staging buffer for rare updates
		createStagingBuffer(r->core.device, r->core.physicalDevice, sizeof(NodeAttribute) * graph->node_count, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &r->nodeAttributeStagingBuffer, &r->nodeAttributeStagingMemory, &r->nodeAttributeBuffer, &r->nodeAttributeMemory);
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
		createMappedBuffer(r->core.device, r->core.physicalDevice, sizeof(EdgePosition) * neededEdgeVerts, &r->edgePositionBuffer, &r->edgePositionMemory);
		// Attribute buffer: DEVICE_LOCAL with staging buffer for rare updates
		createStagingBuffer(r->core.device, r->core.physicalDevice, sizeof(EdgeAttribute) * neededEdgeVerts, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &r->edgeAttributeStagingBuffer, &r->edgeAttributeStagingMemory, &r->edgeAttributeBuffer, &r->edgeAttributeMemory);
		r->edgeCapacity = neededEdgeVerts;
		r->needsAttributeUpload = VK_TRUE;
	}

	// Destroy label/sphere buffers if they exist (these are always rebuilt)
	if (r->labelInstanceBuffer != VK_NULL_HANDLE) {
		vkDestroyBuffer(r->core.device, r->labelInstanceBuffer, NULL);
		vkFreeMemory(r->core.device, r->labelInstanceBufferMemory, NULL);
		vkDestroyBuffer(r->core.device, r->labelStagingBuffer, NULL);
		vkFreeMemory(r->core.device, r->labelStagingBufferMemory, NULL);
		r->labelInstanceBuffer = VK_NULL_HANDLE;
		r->labelStagingBuffer = VK_NULL_HANDLE;
	}
	if (r->sphereVertexBuffer != VK_NULL_HANDLE) {
		vkDestroyBuffer(r->core.device, r->sphereVertexBuffer, NULL);
		vkFreeMemory(r->core.device, r->sphereVertexBufferMemory, NULL);
		r->sphereVertexBuffer = VK_NULL_HANDLE;
	}
	if (r->sphereIndexBuffer != VK_NULL_HANDLE) {
		vkDestroyBuffer(r->core.device, r->sphereIndexBuffer, NULL);
		vkFreeMemory(r->core.device, r->sphereIndexBufferMemory, NULL);
		r->sphereIndexBuffer = VK_NULL_HANDLE;
	}
	if (r->sphereIndexCounts) {
		free(r->sphereIndexCounts);
		r->sphereIndexCounts = NULL;
	}
	if (r->sphereIndexOffsets) {
		free(r->sphereIndexOffsets);
		r->sphereIndexOffsets = NULL;
	}

	r->numSpheres = 0;

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
				nodeAttributes[currentOffset + count].glow = sorted[currentOffset + count].glow;
				nodeAttributes[currentOffset + count].selected = sorted[currentOffset + count].selected;
				count++;
			}
		}
		r->platonicDrawCalls[t].count = count;
		currentOffset += count;
	}
	// Fast path: update positions via mapped buffer
	updateBufferMapped(r->core.device, r->nodePositionMemory, sizeof(NodePosition) * graph->node_count, nodePositions);
	// Rare: update attributes via staged copy
	if (r->needsAttributeUpload) {
		updateBufferStaged(r->core.device, r->commands.commandPool, r->core.graphicsQueue, sizeof(NodeAttribute) * graph->node_count, nodeAttributes, r->nodeAttributeStagingBuffer, r->nodeAttributeStagingMemory, r->nodeAttributeBuffer);
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
				edgeAttributes[idx].size = graph->edges[i].size;
				edgeAttributes[idx].selected = graph->edges[i].selected;
				edgeAttributes[idx].animation_progress = graph->edges[i].animation_progress;
				edgeAttributes[idx].animation_direction = graph->edges[i].animation_direction;
				edgeAttributes[idx].is_animating = graph->edges[i].is_animating;
				edgeAttributes[idx].normalized_pos = current_segment_start_len / (total_length > 0.0f ? total_length : 1.0f);
				idx++;

				memcpy(edgePositions[idx].pos, cEdges[i].path[p + 1], 12);
				memcpy(edgeAttributes[idx].color, graph->nodes[graph->edges[i].to].color, 12);
				edgeAttributes[idx].size = graph->edges[i].size;
				edgeAttributes[idx].selected = graph->edges[i].selected;
				edgeAttributes[idx].animation_progress = graph->edges[i].animation_progress;
				edgeAttributes[idx].animation_direction = graph->edges[i].animation_direction;
				edgeAttributes[idx].is_animating = graph->edges[i].is_animating;
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
			edgeAttributes[idx].size = graph->edges[i].size;
			edgeAttributes[idx].selected = graph->edges[i].selected;
			edgeAttributes[idx].animation_progress = graph->edges[i].animation_progress;
			edgeAttributes[idx].animation_direction = graph->edges[i].animation_direction;
			edgeAttributes[idx].is_animating = graph->edges[i].is_animating;
			edgeAttributes[idx].normalized_pos = 0.0f;
			idx++;

			memcpy(edgePositions[idx].pos, p2, 12);
			memcpy(edgeAttributes[idx].color, graph->nodes[graph->edges[i].to].color, 12);
			edgeAttributes[idx].size = graph->edges[i].size;
			edgeAttributes[idx].selected = graph->edges[i].selected;
			edgeAttributes[idx].animation_progress = graph->edges[i].animation_progress;
			edgeAttributes[idx].animation_direction = graph->edges[i].animation_direction;
			edgeAttributes[idx].is_animating = graph->edges[i].is_animating;
			edgeAttributes[idx].normalized_pos = 1.0f;
			idx++;
		}
	}

	r->edgeVertexCount = idx;

	// Fast path: update positions via mapped buffer
	if (r->edgeVertexCount > 0) {
		updateBufferMapped(r->core.device, r->edgePositionMemory, sizeof(EdgePosition) * r->edgeVertexCount, edgePositions);
	}
	// Rare: update attributes via staged copy (only when flag set)
	if (r->needsAttributeUpload && r->edgeVertexCount > 0) {
		updateBufferStaged(r->core.device, r->commands.commandPool, r->core.graphicsQueue, sizeof(EdgeAttribute) * r->edgeVertexCount, edgeAttributes, r->edgeAttributeStagingBuffer, r->edgeAttributeStagingMemory, r->edgeAttributeBuffer);
	}
	free(edgePositions);
	free(edgeAttributes);
	r->needsAttributeUpload = VK_FALSE;

	uint32_t tc = 0;
	for (uint32_t i = 0; i < r->nodeCount; i++)
		if (graph->nodes[i].label)
			tc += strlen(graph->nodes[i].label);
	r->labelCharCount = tc;
	if (tc > 0) {
		createStagingBuffer(r->core.device, r->core.physicalDevice, sizeof(LabelInstance) * tc, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &r->labelStagingBuffer, &r->labelStagingBufferMemory, &r->labelInstanceBuffer, &r->labelInstanceBufferMemory);
		LabelInstance *li = malloc(sizeof(LabelInstance) * r->labelCharCount);
		uint32_t k = 0;
		for (uint32_t i = 0; i < graph->node_count; i++) {
			if (!graph->nodes[i].label)
				continue;
			int len = strlen(graph->nodes[i].label);
			float xoff = 0;
			vec3 pos;
			glm_vec3_scale(graph->nodes[i].position, r->layoutScale, pos);
			for (int j = 0; j < len; j++) {
				unsigned char c = graph->nodes[i].label[j];
				CharInfo *ci = (c < 128) ? &globalAtlas.chars[c] : &globalAtlas.chars[32];
				memcpy(li[k].nodePos, pos, 12);
				li[k].nodePos[1] += (0.5f * graph->nodes[i].size) + 0.3f;
				li[k].charRect[0] = xoff + ci->x0;
				li[k].charRect[1] = ci->y0;
				li[k].charRect[2] = xoff + ci->x1;
				li[k].charRect[3] = ci->y1;
				li[k].charUV[0] = ci->u0;
				li[k].charUV[1] = ci->v0;
				li[k].charUV[2] = ci->u1;
				li[k].charUV[3] = ci->v1;

				// Standard billboard orientation for node labels, scaled to 0.01f
				float node_text_scale = 0.01f;
				li[k].right[0] = node_text_scale;
				li[k].right[1] = 0.0f;
				li[k].right[2] = 0.0f;
				li[k].up[0] = 0.0f;
				li[k].up[1] = node_text_scale;
				li[k].up[2] = 0.0f;

				xoff += ci->xadvance;
				k++;
			}
		}
		updateBufferStaged(r->core.device, r->commands.commandPool, r->core.graphicsQueue, sizeof(LabelInstance) * r->labelCharCount, li, r->labelStagingBuffer, r->labelStagingBufferMemory, r->labelInstanceBuffer);
		free(li);
	} else {
		r->labelInstanceBuffer = VK_NULL_HANDLE;
	}

	// Signal the ring fence for this slot so the next update can proceed
	vkQueueSubmit(r->core.graphicsQueue, 0, NULL, r->graphUpdateFences[ringIdx]);
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
		createBuffer(r->core.device, r->core.physicalDevice, sizeof(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->rayVertexBuffer, &r->rayVertexBufferMemory);
		r->rayVertexCount = 2;
	}
	updateBuffer(r->core.device, r->rayVertexBufferMemory, sizeof(vertices), vertices);

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
