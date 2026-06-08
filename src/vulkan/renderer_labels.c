#include "vulkan/renderer_labels.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <igraph.h>
#include <igraph_barnes_hut.h>

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

	// Compute graph degree
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
// LOD Label Helpers — Barnes-Hut nearest-K
// ============================================================================

// Max-heap entry for nearest-K selection
typedef struct
{
	float dist;
	uint32_t idx;
} BHHeapEntry;

static void bh_heap_sift_up(BHHeapEntry *heap, uint32_t size, uint32_t i)
{
	while (i > 0) {
		uint32_t parent = (i - 1) / 2;
		if (heap[i].dist > heap[parent].dist) {
			BHHeapEntry tmp = heap[i];
			heap[i] = heap[parent];
			heap[parent] = tmp;
			i = parent;
		} else {
			break;
		}
	}
}

static void bh_heap_sift_down(BHHeapEntry *heap, uint32_t size, uint32_t i)
{
	for (;;) {
		uint32_t largest = i;
		uint32_t left = 2 * i + 1;
		uint32_t right = 2 * i + 2;
		if (left < size && heap[left].dist > heap[largest].dist)
			largest = left;
		if (right < size && heap[right].dist > heap[largest].dist)
			largest = right;
		if (largest == i)
			break;
		BHHeapEntry tmp = heap[i];
		heap[i] = heap[largest];
		heap[largest] = tmp;
		i = largest;
	}
}

static void bh_heap_push(BHHeapEntry *heap, uint32_t *size, uint32_t capacity, float dist, uint32_t idx)
{
	if (*size < capacity) {
		heap[*size].dist = dist;
		heap[*size].idx = idx;
		(*size)++;
		bh_heap_sift_up(heap, *size, *size - 1);
	} else if (dist < heap[0].dist) {
		heap[0].dist = dist;
		heap[0].idx = idx;
		bh_heap_sift_down(heap, *size, 0);
	}
}

// Recursively traverse BH tree to find K nearest points to the query.
static void bh_traverse_nearest_k(const igraph_bh_tree_t *tree, igraph_integer_t node_idx, const float query[3], uint32_t k, BHHeapEntry *heap, uint32_t *heap_size)
{
	const igraph_bh_node_t *node = &tree->nodes[node_idx];

	if (node->point_count == 0 && !node->is_leaf)
		return;

	// Lower bound: distance from query to nearest edge of node bounding box
	float dx = query[0] - (float)node->center[0];
	float dy = query[1] - (float)node->center[1];
	float dz = query[2] - (float)node->center[2];
	float dist_sq_to_center = dx * dx + dy * dy + dz * dz;
	float half_size = (float)node->size * 0.5f;
	float center_dist = sqrtf(dist_sq_to_center);
	float min_dist = center_dist - half_size * 1.7320508f; // sqrt(3) for 3D bounding sphere
	if (min_dist < 0.0f)
		min_dist = 0.0f;

	// MAC: if lower bound exceeds current k-th farthest, prune
	if (*heap_size >= k && min_dist >= heap[0].dist)
		return;

	if (node->is_leaf) {
		// Check each point in this leaf
		for (igraph_integer_t p = 0; p < node->point_count; p++) {
			igraph_integer_t pt_idx = tree->point_indices[node->data.first_point_idx + p];
			const igraph_bh_point_t *pt = &tree->points[pt_idx];
			float px = query[0] - (float)pt->coord[0];
			float py = query[1] - (float)pt->coord[1];
			float pz = query[2] - (float)pt->coord[2];
			float d = px * px + py * py + pz * pz;
			bh_heap_push(heap, heap_size, k, d, (uint32_t)pt->id);
		}
	} else {
		// Recurse into children (2^dim = 8 for 3D)
		igraph_integer_t num_children = 1 << tree->dim;
		for (igraph_integer_t c = 0; c < num_children; c++) {
			igraph_integer_t child_idx = node->data.first_child_idx + c;
			if (child_idx < tree->node_count)
				bh_traverse_nearest_k(tree, child_idx, query, k, heap, heap_size);
		}
	}
}

// Rebuild the Barnes-Hut tree from current node positions (scaled by layoutScale).
static void label_rebuild_tree(Renderer *r, GraphData *graph)
{
	uint32_t n = graph->node_count;
	if (n == 0)
		return;

	igraph_integer_t max_level, leaf_capacity;
	igraph_bh_tree_get_scaling_params(n, 3, &max_level, &leaf_capacity);

	igraph_bh_tree_destroy(&r->labelTree);
	igraph_error_t err = igraph_bh_tree_init(&r->labelTree, 3, 0.7, max_level, leaf_capacity);
	if (err != IGRAPH_SUCCESS) {
		fprintf(stderr, "label_rebuild_tree: igraph_bh_tree_init failed\n");
		return;
	}

	igraph_matrix_t coords;
	igraph_matrix_init(&coords, n, 3);
	for (uint32_t i = 0; i < n; i++) {
		igraph_matrix_set(&coords, i, 0, (igraph_real_t)(graph->nodes[i].position[0] * r->layoutScale));
		igraph_matrix_set(&coords, i, 1, (igraph_real_t)(graph->nodes[i].position[1] * r->layoutScale));
		igraph_matrix_set(&coords, i, 2, (igraph_real_t)(graph->nodes[i].position[2] * r->layoutScale));
	}

	igraph_vector_t masses;
	igraph_vector_init(&masses, n);
	for (uint32_t i = 0; i < n; i++)
		VECTOR(masses)[i] = 1.0;

	err = igraph_bh_tree_build(&r->labelTree, &coords, &masses);
	igraph_matrix_destroy(&coords);
	igraph_vector_destroy(&masses);

	if (err != IGRAPH_SUCCESS) {
		fprintf(stderr, "label_rebuild_tree: igraph_bh_tree_build failed\n");
		return;
	}

	r->labelTreeNeedsRebuild = false;
}

// Find K nearest nodes to the query point using BH tree traversal.
// Returns the number of results written. Results are written to `out` as DistIdxPair.
static uint32_t bh_find_nearest_k(Renderer *r, const float query[3], uint32_t k, DistIdxPair *out, int selected_node)
{
	BHHeapEntry *heap = malloc(sizeof(BHHeapEntry) * k);
	uint32_t heap_size = 0;

	bh_traverse_nearest_k(&r->labelTree, 0, query, k, heap, &heap_size);

	// Extract results, sort by distance ascending for label_build_lod_instances
	// Convert max-heap to sorted array: repeatedly extract max, then reverse
	for (uint32_t i = heap_size; i > 0; i--) {
		BHHeapEntry top = heap[0];
		heap[0] = heap[i - 1];
		bh_heap_sift_down(heap, i - 1, 0);
		out[heap_size - i].dist = sqrtf(top.dist);
		out[heap_size - i].idx = top.idx;
	}

	free(heap);
	return heap_size;
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
static uint32_t label_build_lod_instances(Renderer *r, GraphData *graph, uint32_t label_count, DistIdxPair *sorted, int selected_node, NodeLabelInstance *instances)
{
	text_atlas_clear(&r->nodeTextAtlas);

	float world_text_scale = 0.003f;
	float fixed_offset = 0.4f;
	uint32_t inst_idx = 0;

	for (uint32_t j = 0; j < label_count && inst_idx < label_count; j++) {
		uint32_t ni = sorted[j].idx;
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
		r->detailCardNode = selected_node;
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
	if (graph->node_count == 0) {
		r->nodeLabelInstanceCount = 0;
		r->detailCardVisible = false;
		return;
	}

	// Check if labels can be skipped (camera hasn't moved, selection unchanged)
	if (!r->labelTreeNeedsRebuild && r->labelCacheValid) {
		vec3 delta;
		glm_vec3_sub(camera_pos, r->labelCameraPos, delta);
		float dist2 = glm_vec3_dot(delta, delta);
		bool camera_moved = dist2 > 0.0001f;
		bool selection_changed = (selected_node != r->labelSelectedNode);

		if (!camera_moved && !selection_changed) {
			if (selected_node >= 0 && selected_node < (int)graph->node_count)
				r->detailCardVisible = (r->detailCardNode >= 0);
			return;
		}
	}

	// Rebuild BH tree if positions changed
	if (r->labelTreeNeedsRebuild)
		label_rebuild_tree(r, graph);

	r->nodeLabelInstanceCount = 0;

	// Find K nearest nodes via BH traversal
	uint32_t max_labels = 200;
	float query[3] = {camera_pos[0], camera_pos[1], camera_pos[2]};

	// Allocate sort buffer for BH results
	DistIdxPair *sorted = malloc(sizeof(DistIdxPair) * max_labels);
	if (!sorted)
		return;

	uint32_t found = bh_find_nearest_k(r, query, max_labels, sorted, selected_node);

	// Count valid labels from BH results
	uint32_t label_count = 0;
	for (uint32_t j = 0; j < found && label_count < max_labels; j++) {
		uint32_t ni = sorted[j].idx;
		if ((int)ni == selected_node)
			continue;
		if (!graph->nodes[ni].label || !graph->nodes[ni].label[0])
			continue;
		label_count++;
	}

	if (label_count == 0 && selected_node < 0) {
		free(sorted);
		return;
	}

	uint32_t nli_needed = label_count > 0 ? label_count : 1;
	label_ensure_instance_buffer(r, nli_needed);

	NodeLabelInstance *instances = NULL;
	if (label_count > 0) {
		instances = malloc(sizeof(NodeLabelInstance) * label_count);
		if (!instances) {
			free(sorted);
			return;
		}
	}

	uint32_t inst_idx = label_build_lod_instances(r, graph, label_count, sorted, selected_node, instances);
	free(sorted);

	if (inst_idx > 0)
		label_upload_and_update_descriptors(r, inst_idx, instances);

	r->nodeLabelInstanceCount = inst_idx;
	free(instances);

	glm_vec3_copy(camera_pos, r->labelCameraPos);
	r->labelSelectedNode = selected_node;
	r->labelCacheValid = true;

	// Detail card: only rebuild atlas on selection change
	if (selected_node >= 0 && selected_node < (int)graph->node_count) {
		if (selected_node != r->detailCardNode)
			detail_card_update(r, graph, selected_node);
		else
			r->detailCardVisible = true;
	} else {
		r->detailCardVisible = false;
		r->detailCardNode = -1;
	}
}
