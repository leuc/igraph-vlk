#include "vulkan/renderer_geometry.h"
#include "vulkan/renderer_compute.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "graph/layered_sphere.h"
#include "vulkan/text.h"
#include "vulkan/utils.h"

extern FontAtlas globalAtlas;

void renderer_update_graph(Renderer *r, GraphData *graph) {
	vkDeviceWaitIdle(r->device);
	r->nodeCount = graph->node_count;
	r->edgeCount = graph->edge_count;
	if (r->instanceBuffer != VK_NULL_HANDLE) {
		vkDestroyBuffer(r->device, r->instanceBuffer, NULL);
		vkFreeMemory(r->device, r->instanceBufferMemory, NULL);
		vkDestroyBuffer(r->device, r->edgeVertexBuffer, NULL);
		vkFreeMemory(r->device, r->edgeVertexBufferMemory, NULL);

		if (r->labelInstanceBuffer != VK_NULL_HANDLE) {
			vkDestroyBuffer(r->device, r->labelInstanceBuffer, NULL);
			vkFreeMemory(r->device, r->labelInstanceBufferMemory, NULL);
			r->labelInstanceBuffer = VK_NULL_HANDLE;
		}
		if (r->sphereVertexBuffer != VK_NULL_HANDLE) {
			vkDestroyBuffer(r->device, r->sphereVertexBuffer, NULL);
			vkFreeMemory(r->device, r->sphereVertexBufferMemory, NULL);
			r->sphereVertexBuffer = VK_NULL_HANDLE;
		}
		if (r->sphereIndexBuffer != VK_NULL_HANDLE) {
			vkDestroyBuffer(r->device, r->sphereIndexBuffer, NULL);
			vkFreeMemory(r->device, r->sphereIndexBufferMemory, NULL);
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
	}

	// Generate Sphere Backgrounds if Layered Layout
	if (graph->active_layout == LAYOUT_LAYERED_SPHERE &&
		graph->layered_sphere && graph->layered_sphere->initialized) {
		LayeredSphereContext *ctx = graph->layered_sphere;
		r->numSpheres = ctx->num_spheres;
		r->sphereIndexCounts = malloc(sizeof(uint32_t) * r->numSpheres);
		r->sphereIndexOffsets = malloc(sizeof(uint32_t) * r->numSpheres);

		uint32_t totalVertices = 0;
		uint32_t totalIndices = 0;

		// Pass 1: Calculate sizes
		for (int s = 0; s < ctx->num_spheres; s++) {
			int target_faces = ctx->grids[s].max_slots;
			int rings = (int)sqrt(target_faces / 8.0);
			if (rings < 4)
				rings = 4;
			if (rings > 32)
				rings = 32; // FIX: Cap sphere resolution to save FPS
			int sectors = rings * 2;

			totalVertices += (rings + 1) * (sectors + 1);
			totalIndices += rings * sectors * 6;
		}

		// Create buffers
		createBuffer(r->device, r->physicalDevice,
					 sizeof(Vertex) * totalVertices,
					 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
					 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
						 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
					 &r->sphereVertexBuffer, &r->sphereVertexBufferMemory);
		createBuffer(r->device, r->physicalDevice,
					 sizeof(uint32_t) * totalIndices,
					 VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
					 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
						 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
					 &r->sphereIndexBuffer, &r->sphereIndexBufferMemory);

		Vertex *allVerts = malloc(sizeof(Vertex) * totalVertices);
		uint32_t *allIndices = malloc(sizeof(uint32_t) * totalIndices);

		uint32_t vOffset = 0;
		uint32_t iOffset = 0;

		// Pass 2: Generate geometry
		for (int s = 0; s < ctx->num_spheres; s++) {
			double radius = 5.0; // Default
			if (ctx->grids[s].max_slots > 0) {
				double x = ctx->grids[s].slots[0].x;
				double y = ctx->grids[s].slots[0].y;
				double z = ctx->grids[s].slots[0].z;
				radius = sqrt(x * x + y * y + z * z);
			}
			float R = (float)radius * r->layoutScale;

			int target_faces = ctx->grids[s].max_slots;
			int rings = (int)sqrt(target_faces / 8.0);
			if (rings < 4)
				rings = 4;
			if (rings > 32)
				rings = 32; // FIX: Cap sphere resolution to save FPS
			int sectors = rings * 2;

			r->sphereIndexOffsets[s] = iOffset;
			uint32_t startIndex = vOffset;

			float const R_inv = 1.f / R;
			float const S = 1.f / (float)sectors;
			float const T = 1.f / (float)rings;

			for (int r_idx = 0; r_idx <= rings; r_idx++) {
				float const y = sin(-M_PI_2 + M_PI * r_idx * T);
				float const xz = cos(-M_PI_2 + M_PI * r_idx * T);

				for (int s_idx = 0; s_idx <= sectors; s_idx++) {
					float const x = xz * cos(2 * M_PI * s_idx * S);
					float const z = xz * sin(2 * M_PI * s_idx * S);

					vec3 n = {x, y, z};
					vec3 p;
					glm_vec3_scale(n, R, p);

					memcpy(allVerts[vOffset].pos, p, 12);
					memcpy(allVerts[vOffset].normal, n,
						   12); // Use normal for shading
					allVerts[vOffset].texCoord[0] = s_idx * S;
					allVerts[vOffset].texCoord[1] = r_idx * T;
					vOffset++;
				}
			}

			uint32_t indexCount = 0;
			for (int r_idx = 0; r_idx < rings; r_idx++) {
				for (int s_idx = 0; s_idx < sectors; s_idx++) {
					uint32_t curRow = startIndex + r_idx * (sectors + 1);
					uint32_t nextRow = startIndex + (r_idx + 1) * (sectors + 1);

					allIndices[iOffset++] = curRow + s_idx;
					allIndices[iOffset++] = nextRow + s_idx;
					allIndices[iOffset++] = nextRow + (s_idx + 1);

					allIndices[iOffset++] = curRow + s_idx;
					allIndices[iOffset++] = nextRow + (s_idx + 1);
					allIndices[iOffset++] = curRow + (s_idx + 1);

					indexCount += 6;
				}
			}
			r->sphereIndexCounts[s] = indexCount;
		}

		updateBuffer(r->device, r->sphereVertexBufferMemory,
					 sizeof(Vertex) * totalVertices, allVerts);
		updateBuffer(r->device, r->sphereIndexBufferMemory,
					 sizeof(uint32_t) * totalIndices, allIndices);
		free(allVerts);
		free(allIndices);
	} else {
		r->numSpheres = 0;
	}

	createBuffer(r->device, r->physicalDevice, sizeof(Node) * r->nodeCount,
				 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
				 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
					 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				 &r->instanceBuffer, &r->instanceBufferMemory);

	int segments = (r->currentRoutingMode == ROUTING_MODE_STRAIGHT) ? 1 : 15;
	r->edgeVertexCount = graph->edge_count * segments * 2;
	createBuffer(r->device, r->physicalDevice,
				 sizeof(EdgeVertex) * r->edgeVertexCount,
				 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
				 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
					 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				 &r->edgeVertexBuffer, &r->edgeVertexBufferMemory);

	Node *sorted = malloc(sizeof(Node) * graph->node_count);
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
				glm_vec3_scale(sorted[currentOffset + count].position,
							   r->layoutScale,
							   sorted[currentOffset + count].position);
				if (sorted[currentOffset + count].size < 0.1f)
					sorted[currentOffset + count].size = 0.1f;
				count++;
			}
		}
		r->platonicDrawCalls[t].count = count;
		currentOffset += count;
	}
	updateBuffer(r->device, r->instanceBufferMemory,
				 sizeof(Node) * graph->node_count, sorted);
	EdgeVertex *evs = malloc(sizeof(EdgeVertex) * r->edgeVertexCount);
	uint32_t idx = 0;
	// Dispatch edge routing compute shader if needed
	CompEdge *cEdges = NULL;
	if (r->currentRoutingMode != ROUTING_MODE_STRAIGHT) {
		cEdges = malloc(sizeof(CompEdge) * graph->edge_count);
		renderer_dispatch_edge_routing(r, graph, cEdges);
	}

	if (cEdges) {
		for (uint32_t i = 0; i < graph->edge_count; i++) {
			// CLAMP PATH LENGTH to prevent Heap Buffer Overflows!
			int pLen = cEdges[i].pathLength;
			if (pLen < 0)
				pLen = 0;
			if (pLen > 16)
				pLen = 16;

			// For routed edges, calculate normalized_pos based on segment
			// length This is an approximation: assuming uniform segment lengths
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

				memcpy(evs[idx].pos, cEdges[i].path[p], 12);
				memcpy(evs[idx].color, graph->nodes[graph->edges[i].from].color,
					   12);
				evs[idx].size = graph->edges[i].size;
				evs[idx].selected = graph->edges[i].selected;
				evs[idx].animation_progress =
					graph->edges[i].animation_progress;
				evs[idx].animation_direction =
					graph->edges[i].animation_direction;
				evs[idx].is_animating = graph->edges[i].is_animating;
				evs[idx].normalized_pos =
					current_segment_start_len / total_length;
				idx++;

				memcpy(evs[idx].pos, cEdges[i].path[p + 1], 12);
				memcpy(evs[idx].color, graph->nodes[graph->edges[i].to].color,
					   12);
				evs[idx].size = graph->edges[i].size;
				evs[idx].selected = graph->edges[i].selected;
				evs[idx].animation_progress =
					graph->edges[i].animation_progress;
				evs[idx].animation_direction =
					graph->edges[i].animation_direction;
				evs[idx].is_animating = graph->edges[i].is_animating;
				evs[idx].normalized_pos =
					(current_segment_start_len + segment_length) / total_length;
				idx++;
				current_segment_start_len += segment_length;
			}
		}
		free(cEdges);
	} else {
		for (uint32_t i = 0; i < graph->edge_count; i++) {
			vec3 p1, p2;
			glm_vec3_scale(graph->nodes[graph->edges[i].from].position,
						   r->layoutScale, p1);
			glm_vec3_scale(graph->nodes[graph->edges[i].to].position,
						   r->layoutScale, p2);
			memcpy(evs[idx].pos, p1, 12);
			memcpy(evs[idx].color, graph->nodes[graph->edges[i].from].color,
				   12);
			evs[idx].size = graph->edges[i].size;
			evs[idx].selected = graph->edges[i].selected;
			evs[idx].animation_progress = graph->edges[i].animation_progress;
			evs[idx].animation_direction = graph->edges[i].animation_direction;
			evs[idx].is_animating = graph->edges[i].is_animating;
			evs[idx].normalized_pos = 0.0f; // Start of the straight edge
			idx++;
			memcpy(evs[idx].pos, p2, 12);
			memcpy(evs[idx].color, graph->nodes[graph->edges[i].to].color, 12);
			evs[idx].size = graph->edges[i].size;
			evs[idx].selected = graph->edges[i].selected;
			evs[idx].animation_progress = graph->edges[i].animation_progress;
			evs[idx].animation_direction = graph->edges[i].animation_direction;
			evs[idx].is_animating = graph->edges[i].is_animating;
			evs[idx].normalized_pos = 1.0f; // End of the straight edge
			idx++;
		}
	}

	r->edgeVertexCount = idx;

	// PREVENT 0-byte memory maps which crash Vulkan
	if (r->edgeVertexCount > 0) {
		updateBuffer(r->device, r->edgeVertexBufferMemory,
					 sizeof(EdgeVertex) * r->edgeVertexCount, evs);
	}
	free(evs);

	uint32_t tc = 0;
	for (uint32_t i = 0; i < r->nodeCount; i++)
		if (graph->nodes[i].label)
			tc += strlen(graph->nodes[i].label);
	r->labelCharCount = tc;
	if (tc > 0) {
		createBuffer(r->device, r->physicalDevice, sizeof(LabelInstance) * tc,
					 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
					 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
						 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
					 &r->labelInstanceBuffer, &r->labelInstanceBufferMemory);
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
				CharInfo *ci =
					(c < 128) ? &globalAtlas.chars[c] : &globalAtlas.chars[32];
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
				xoff += ci->xadvance;
				k++;
			}
		}
		updateBuffer(r->device, r->labelInstanceBufferMemory,
					 sizeof(LabelInstance) * r->labelCharCount, li);
		free(li);
	} else {
		r->labelInstanceBuffer = VK_NULL_HANDLE;
	}
	free(sorted);
}
