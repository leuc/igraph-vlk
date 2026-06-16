/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "vulkan/renderer_update_node_labels.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <igraph.h>

#include "vulkan/buffers.h"
#include "vulkan/renderer_lifecycle.h"
#include "vulkan/text.h"
#include "vulkan/utils.h"

// Build the detail card text (multi-line attribute list) for a selected node.
// Returns malloc'd string the caller must free, or NULL on alloc failure.
static char *build_detail_card_text(igraph_t *g, int node, size_t *out_len)
{
	size_t cap = 4096;
	char *buf = calloc(cap, 1);
	if (!buf)
		return NULL;
	size_t pos = 0;

	igraph_error_handler_t *prev_handler = igraph_set_error_handler(igraph_error_handler_ignore);

	igraph_strvector_t vnames;
	igraph_vector_int_t vtypes;
	bool vnames_ok = igraph_strvector_init(&vnames, 0) == IGRAPH_SUCCESS;
	bool vtypes_ok = igraph_vector_int_init(&vtypes, 0) == IGRAPH_SUCCESS;
	igraph_error_t verr = igraph_cattribute_list(g, NULL, NULL, &vnames, &vtypes, NULL, NULL);
	if (verr != IGRAPH_SUCCESS) {
		fprintf(stderr, "renderer_update_node_labels: igraph_cattribute_list failed: %s\n", igraph_strerror(verr));
	}

	if (verr == IGRAPH_SUCCESS && vnames_ok && vtypes_ok) {
		int n_attrs = igraph_strvector_size(&vnames);
		for (int a = 0; a < n_attrs; a++) {
			if (pos + 256 >= cap) {
				size_t new_cap = cap * 2;
				char *tmp = realloc(buf, new_cap);
				if (tmp) {
					memset(tmp + cap, 0, new_cap - cap);
					buf = tmp;
					cap = new_cap;
				} else {
					fprintf(stderr, "renderer_update_node_labels: realloc failed\n");
					break;
				}
			}

			const char *name = igraph_strvector_get(&vnames, a);
			if (!name)
				continue;

			igraph_attribute_type_t atype = (igraph_attribute_type_t)VECTOR(vtypes)[a];
			if (atype == IGRAPH_ATTRIBUTE_STRING) {
				const char *v = igraph_cattribute_VAS(g, name, node);
				int w = snprintf(buf + pos, cap - pos, "%s: %s\n", name, v ? v : "");
				if (w > 0)
					pos += (size_t)w;
			} else if (atype == IGRAPH_ATTRIBUTE_NUMERIC) {
				double v = igraph_cattribute_VAN(g, name, node);
				if (!isfinite(v))
					continue;
				int w;
				if (fabs(v) < 0.001 || fabs(v) >= 10000.0)
					w = snprintf(buf + pos, cap - pos, "%s: %g\n", name, v);
				else
					w = snprintf(buf + pos, cap - pos, "%s: %.4f\n", name, v);
				if (w > 0)
					pos += (size_t)w;
			} else if (atype == IGRAPH_ATTRIBUTE_BOOLEAN) {
				bool v = (bool)igraph_cattribute_VAB(g, name, node);
				int w = snprintf(buf + pos, cap - pos, "%s: %s\n", name, v ? "true" : "false");
				if (w > 0)
					pos += (size_t)w;
			} else {
				fprintf(stderr, "renderer_update_node_labels: unknown attr type %d\n", (int)atype);
			}
		}
	}

	if (vnames_ok)
		igraph_strvector_destroy(&vnames);
	if (vtypes_ok)
		igraph_vector_int_destroy(&vtypes);
	igraph_set_error_handler(prev_handler);

	igraph_vector_int_t degree_vec;
	igraph_vector_int_init(&degree_vec, 1);
	igraph_integer_t node_id = node;
	igraph_degree(g, &degree_vec, igraph_vss_1(node_id), IGRAPH_ALL, IGRAPH_LOOPS);
	int deg = (int)VECTOR(degree_vec)[0];
	igraph_vector_int_destroy(&degree_vec);
	{
		int w = snprintf(buf + pos, cap - pos, "degree: %d\n", deg);
		if (w > 0)
			pos += (size_t)w;
	}

	if (pos == 0) {
		int w = snprintf(buf, cap, "(no attributes)\n");
		if (w > 0)
			pos = (size_t)w;
	}

	*out_len = pos;
	return buf;
}

void detail_card_update(Renderer *r, GraphData *graph, int selected_node)
{
	size_t text_len = 0;
	char *detail = build_detail_card_text(&graph->g, selected_node, &text_len);
	if (!detail) {
		fprintf(stderr, "renderer_update_node_labels: detail text alloc failed\n");
		return;
	}

	text_atlas_clear(&r->detailCardAtlas);

	vec3 normal, upGuide = {0.0f, 1.0f, 0.0f};
	glm_vec3_normalize_to(graph->nodes[selected_node].position, normal);
	if (fabsf(normal[1]) > 0.999f)
		upGuide[0] = 1.0f;
	vec3 right, up;
	glm_vec3_cross(upGuide, normal, right);
	glm_vec3_normalize(right);
	glm_vec3_cross(normal, right, up);

	float fixed_offset = 0.4f;
	vec3 label_pos;
	glm_vec3_scale(normal, fixed_offset + 0.3f, label_pos);
	glm_vec3_add(graph->nodes[selected_node].position, label_pos, label_pos);
	glm_vec3_scale(label_pos, r->layoutScale, label_pos);

	TextRegion region;
	text_atlas_render(&r->detailCardAtlas, &globalAtlas, detail, &region);

	if (region.width_px > 0.0f && region.height_px > 0.0f) {
		if (r->detailCardInstanceBuffer == VK_NULL_HANDLE) {
			VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, sizeof(NodeLabelInstance), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &r->detailCardInstanceBuffer, &r->detailCardInstanceBufferMemory);
		}

		float world_text_scale = 0.003f;
		NodeLabelInstance *inst = &r->detailCardInstance;
		glm_vec3_copy(label_pos, inst->worldPos);
		inst->bgColor[0] = 0.02f;
		inst->bgColor[1] = 0.02f;
		inst->bgColor[2] = 0.04f;
		inst->bgColor[3] = 1.0f;
		inst->scale[0] = region.width_px * world_text_scale;
		inst->scale[1] = region.height_px * world_text_scale;
		inst->scale[2] = 1.0f;
		glm_vec3_copy(right, inst->right);
		glm_vec3_copy(up, inst->up);
		inst->textUV[0] = region.u0;
		inst->textUV[1] = region.v0;
		inst->textUV[2] = region.u1;
		inst->textUV[3] = region.v1;
		inst->textRegion[0] = 0.0f;
		inst->textRegion[1] = 0.0f;
		inst->textRegion[2] = 1.0f;
		inst->textRegion[3] = 1.0f;

		text_atlas_ensure_uploaded(&r->detailCardAtlas, r->core.device, r->core.physicalDevice, r->commands.commandPool, r->core.graphicsQueue);

		for (int i = 0; i < MAX_FRAMES_IN_FLIGHT * MAX_VIEWS; i++) {
			VkDescriptorBufferInfo bufferInfo = {r->uniformBuffers[i], 0, sizeof(UniformBufferObject)};
			VkDescriptorImageInfo imageInfo = {r->textureSampler, r->detailCardAtlas.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
			VkWriteDescriptorSet writes[] = {VK_WRITE_DESC_BUFFER(r->detailCardDescSets[i], 0, &bufferInfo, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER), VK_WRITE_DESC_IMAGE(r->detailCardDescSets[i], 1, &imageInfo, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)};
			vkUpdateDescriptorSets(r->core.device, 2, writes, 0, NULL);
		}

		update_buffer(r->core.device, r->detailCardInstanceBufferMemory, sizeof(NodeLabelInstance), &r->detailCardInstance);
		r->detailCardVisible = true;
		r->detailCardNode = selected_node;
	} else {
		fprintf(stderr, "renderer_update_node_labels: detail card atlas render empty (text len %zu)\n", text_len);
	}

	free(detail);
}

void label_upload_and_update_descriptors(Renderer *r, uint32_t inst_count, NodeLabelInstance *instances)
{
	text_atlas_ensure_uploaded(&r->nodeTextAtlas, r->core.device, r->core.physicalDevice, r->commands.commandPool, r->core.graphicsQueue);

	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT * MAX_VIEWS; i++) {
		VkDescriptorBufferInfo bufferInfo = {r->uniformBuffers[i], 0, sizeof(UniformBufferObject)};
		VkDescriptorImageInfo imageInfo = {r->textureSampler, r->nodeTextAtlas.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
		VkWriteDescriptorSet writes[] = {VK_WRITE_DESC_BUFFER(r->nodeLabelDescSets[i], 0, &bufferInfo, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER), VK_WRITE_DESC_IMAGE(r->nodeLabelDescSets[i], 1, &imageInfo, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)};
		vkUpdateDescriptorSets(r->core.device, 2, writes, 0, NULL);
	}

	update_buffer(r->core.device, r->nodeLabelInstanceBufferMemory, sizeof(NodeLabelInstance) * inst_count, instances);
}
