#include "vulkan/renderer_labels.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vulkan/renderer_lifecycle.h"
#include "vulkan/text.h"
#include "vulkan/utils.h"
#include "vulkan/buffers.h"

static int sort_by_dist(const void *a, const void *b)
{
	float da = ((const DistIdxPair *)a)->dist;
	float db = ((const DistIdxPair *)b)->dist;
	return (da > db) - (da < db);
}

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
			// Grow buffer as needed
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

	// Always have at least one printable line so the atlas region is non-zero
	if (pos == 0) {
		int w = snprintf(buf, cap, "(no attributes)\n");
		if (w > 0)
			pos = (size_t)w;
	}

	*out_len = pos;
	return buf;
}

// ============================================================================
// LOD Label Helpers
// ============================================================================

// Compute per-node distances from camera, sort by distance, count visible labels.
// Returns the number of labels to render (skipping selected node and empty labels).
static uint32_t label_sort_by_distance(Renderer *r, GraphData *graph, vec3 camera_pos, int selected_node, uint32_t max_labels)
{
	uint32_t node_count = graph->node_count;

	// Ensure sort buffer is large enough
	if (!r->labelSortPairs || r->labelSortCapacity < node_count) {
		free(r->labelSortPairs);
		r->labelSortCapacity = node_count > 0 ? node_count : 256;
		r->labelSortPairs = malloc(sizeof(DistIdxPair) * r->labelSortCapacity);
		if (!r->labelSortPairs)
			return 0;
	}

	DistIdxPair *pairs = r->labelSortPairs;

	for (uint32_t i = 0; i < node_count; i++) {
		vec3 ws;
		glm_vec3_scale(graph->nodes[i].position, r->layoutScale, ws);
		pairs[i].dist = glm_vec3_distance(camera_pos, ws);
		pairs[i].idx = i;
	}

	qsort(pairs, node_count, sizeof(DistIdxPair), sort_by_dist);

	uint32_t label_count = 0;
	for (uint32_t j = 0; j < node_count && label_count < max_labels; j++) {
		uint32_t ni = pairs[j].idx;
		if ((int)ni == selected_node)
			continue;
		if (!graph->nodes[ni].label || !graph->nodes[ni].label[0])
			continue;
		label_count++;
	}
	return label_count;
}

// Ensure the node label instance buffer is large enough for 'needed' instances.
static void label_ensure_instance_buffer(Renderer *r, uint32_t needed)
{
	if (!r->nodeLabelInstanceBuffer || r->nodeLabelCapacity < needed) {
		if (r->nodeLabelInstanceBuffer != VK_NULL_HANDLE) {
			vkDeviceWaitIdle(r->core.device);
			vkDestroyBuffer(r->core.device, r->nodeLabelInstanceBuffer, NULL);
			vkFreeMemory(r->core.device, r->nodeLabelInstanceBufferMemory, NULL);
		}
		r->nodeLabelCapacity = needed > 128 ? needed : 128;
		create_buffer(r->core.device, r->core.physicalDevice, sizeof(NodeLabelInstance) * r->nodeLabelCapacity, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->nodeLabelInstanceBuffer, &r->nodeLabelInstanceBufferMemory);
	}
}

// Build NodeLabelInstance entries for the nearest nodes.
// Returns the number of instances written.
static uint32_t label_build_lod_instances(Renderer *r, GraphData *graph, uint32_t label_count, int selected_node, NodeLabelInstance *instances)
{
	uint32_t node_count = graph->node_count;
	DistIdxPair *pairs = r->labelSortPairs;

	text_atlas_clear(&r->nodeTextAtlas);

	float world_text_scale = 0.003f;
	float fixed_offset = 0.4f;
	uint32_t inst_idx = 0;

	for (uint32_t j = 0; j < node_count && inst_idx < label_count; j++) {
		uint32_t ni = pairs[j].idx;
		if ((int)ni == selected_node)
			continue;
		const char *label = graph->nodes[ni].label;
		if (!label || !label[0])
			continue;

		// Surface tangent frame
		vec3 normal, upGuide = {0.0f, 1.0f, 0.0f};
		glm_vec3_normalize_to(graph->nodes[ni].position, normal);
		if (fabsf(normal[1]) > 0.999f)
			upGuide[0] = 1.0f;
		vec3 right, up;
		glm_vec3_cross(upGuide, normal, right);
		glm_vec3_normalize(right);
		glm_vec3_cross(normal, right, up);

		// Position above node surface
		vec3 label_pos;
		glm_vec3_scale(normal, fixed_offset, label_pos);
		glm_vec3_add(graph->nodes[ni].position, label_pos, label_pos);
		glm_vec3_scale(label_pos, r->layoutScale, label_pos);

		// Render label text into LOD atlas
		TextRegion region;
		text_atlas_render(&r->nodeTextAtlas, &globalAtlas, label, &region);

		// Build instance
		NodeLabelInstance *inst = &instances[inst_idx];
		glm_vec3_copy(label_pos, inst->worldPos);
		inst->bgColor[0] = 0.05f;
		inst->bgColor[1] = 0.05f;
		inst->bgColor[2] = 0.10f;
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
		inst_idx++;
	}
	return inst_idx;
}

// Upload LOD atlas texture and instance data to GPU, update descriptor sets.
static void label_upload_and_update_descriptors(Renderer *r, uint32_t inst_count, NodeLabelInstance *instances)
{
	text_atlas_ensure_uploaded(&r->nodeTextAtlas, r->core.device, r->core.physicalDevice, r->commands.commandPool, r->core.graphicsQueue);

	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT * MAX_VIEWS; i++) {
		VkDescriptorBufferInfo bufferInfo = {r->uniformBuffers[i], 0, sizeof(UniformBufferObject)};
		VkDescriptorImageInfo imageInfo = {r->textureSampler, r->nodeTextAtlas.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
		VkWriteDescriptorSet writes[] = {{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, r->nodeLabelDescSets[i], 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, NULL, &bufferInfo, NULL}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, r->nodeLabelDescSets[i], 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imageInfo, NULL, NULL}};
		vkUpdateDescriptorSets(r->core.device, 2, writes, 0, NULL);
	}

	update_buffer(r->core.device, r->nodeLabelInstanceBufferMemory, sizeof(NodeLabelInstance) * inst_count, instances);
}

// ============================================================================
// Detail Card (selected node)
// ============================================================================

// Build and upload the detail card for the selected node.
static void detail_card_update(Renderer *r, GraphData *graph, int selected_node)
{
	size_t text_len = 0;
	char *detail = build_detail_card_text(&graph->g, selected_node, &text_len);
	if (!detail) {
		fprintf(stderr, "renderer_update_node_labels: detail text alloc failed\n");
		return;
	}

	text_atlas_clear(&r->detailCardAtlas);

	// Surface tangent frame
	vec3 normal, upGuide = {0.0f, 1.0f, 0.0f};
	glm_vec3_normalize_to(graph->nodes[selected_node].position, normal);
	if (fabsf(normal[1]) > 0.999f)
		upGuide[0] = 1.0f;
	vec3 right, up;
	glm_vec3_cross(upGuide, normal, right);
	glm_vec3_normalize(right);
	glm_vec3_cross(normal, right, up);

	// Position slightly above node
	float fixed_offset = 0.4f;
	vec3 label_pos;
	glm_vec3_scale(normal, fixed_offset + 0.3f, label_pos);
	glm_vec3_add(graph->nodes[selected_node].position, label_pos, label_pos);
	glm_vec3_scale(label_pos, r->layoutScale, label_pos);

	TextRegion region;
	text_atlas_render(&r->detailCardAtlas, &globalAtlas, detail, &region);

	if (region.width_px > 0.0f && region.height_px > 0.0f) {
		// Ensure single-instance buffer exists
		if (r->detailCardInstanceBuffer == VK_NULL_HANDLE) {
			create_buffer(r->core.device, r->core.physicalDevice, sizeof(NodeLabelInstance), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->detailCardInstanceBuffer, &r->detailCardInstanceBufferMemory);
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
			VkWriteDescriptorSet writes[] = {{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, r->detailCardDescSets[i], 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, NULL, &bufferInfo, NULL}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, r->detailCardDescSets[i], 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imageInfo, NULL, NULL}};
			vkUpdateDescriptorSets(r->core.device, 2, writes, 0, NULL);
		}

		update_buffer(r->core.device, r->detailCardInstanceBufferMemory, sizeof(NodeLabelInstance), &r->detailCardInstance);
		r->detailCardVisible = true;
	} else {
		fprintf(stderr, "renderer_update_node_labels: detail card atlas render empty (text len %zu)\n", text_len);
	}

	free(detail);
}

// ============================================================================
// Public API
// ============================================================================

void renderer_update_node_labels(Renderer *r, GraphData *graph, vec3 camera_pos, int selected_node)
{
	r->nodeLabelInstanceCount = 0;
	r->detailCardVisible = false;
	if (graph->node_count == 0)
		return;

	uint32_t max_labels = 200;
	uint32_t label_count = label_sort_by_distance(r, graph, camera_pos, selected_node, max_labels);
	if (label_count == 0 && selected_node < 0)
		return;

	uint32_t nli_needed = label_count > 0 ? label_count : 1;
	label_ensure_instance_buffer(r, nli_needed);

	NodeLabelInstance *instances = NULL;
	if (label_count > 0) {
		instances = malloc(sizeof(NodeLabelInstance) * label_count);
		if (!instances)
			return;
	}

	uint32_t inst_idx = label_build_lod_instances(r, graph, label_count, selected_node, instances);
	if (inst_idx > 0)
		label_upload_and_update_descriptors(r, inst_idx, instances);

	r->nodeLabelInstanceCount = inst_idx;
	free(instances);

	if (selected_node >= 0 && selected_node < (int)graph->node_count)
		detail_card_update(r, graph, selected_node);
}
