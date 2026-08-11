/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "vulkan/renderer_labels.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <igraph.h>
#include <igraph_barnes_hut.h>

#include "vulkan/buffers.h"
#include "vulkan/renderer_lifecycle.h"
#include "vulkan/renderer_update_node_labels.h"
#include "vulkan/text.h"
#include "vulkan/utils.h"


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
static bool label_rebuild_tree(Renderer *r, GraphData *graph)
{
	uint32_t n = graph->node_count;
	if (n == 0) {
		r->label.tree_needs_rebuild = false;
		return true;
	}

	igraph_integer_t max_level, leaf_capacity;
	igraph_bh_tree_get_scaling_params(n, 3, &max_level, &leaf_capacity);

	igraph_bh_tree_destroy(&r->label.tree);
	igraph_error_t err = igraph_bh_tree_init(&r->label.tree, 3, 0.7, max_level, leaf_capacity);
	if (err != IGRAPH_SUCCESS) {
		fprintf(stderr, "label_rebuild_tree: igraph_bh_tree_init failed\n");
		return false;
	}

	igraph_matrix_t coords;
	if (igraph_matrix_init(&coords, n, 3) != IGRAPH_SUCCESS) {
		fprintf(stderr, "label_rebuild_tree: igraph_matrix_init coords failed\n");
		return false;
	}
	for (uint32_t i = 0; i < n; i++) {
		igraph_matrix_set(&coords, i, 0, (igraph_real_t)(graph->nodes[i].position[0] * r->layoutScale));
		igraph_matrix_set(&coords, i, 1, (igraph_real_t)(graph->nodes[i].position[1] * r->layoutScale));
		igraph_matrix_set(&coords, i, 2, (igraph_real_t)(graph->nodes[i].position[2] * r->layoutScale));
	}

	igraph_vector_t masses;
	if (igraph_vector_init(&masses, n) != IGRAPH_SUCCESS) {
		fprintf(stderr, "label_rebuild_tree: igraph_vector_init masses failed\n");
		igraph_matrix_destroy(&coords);
		return false;
	}
	for (uint32_t i = 0; i < n; i++)
		VECTOR(masses)[i] = 1.0;

	err = igraph_bh_tree_build(&r->label.tree, &coords, &masses);
	igraph_matrix_destroy(&coords);
	igraph_vector_destroy(&masses);

	if (err != IGRAPH_SUCCESS) {
		fprintf(stderr, "label_rebuild_tree: igraph_bh_tree_build failed\n");
		return false;
	}

	r->label.tree_needs_rebuild = false;
	return true;
}

// Find K nearest nodes to the query point using BH tree traversal.
// Returns the number of results written. Results are written to `out` as DistIdxPair.
static uint32_t bh_find_nearest_k(Renderer *r, const float query[3], uint32_t k, DistIdxPair *out, int selected_node)
{
	BHHeapEntry *heap = malloc(sizeof(BHHeapEntry) * k);
	uint32_t heap_size = 0;

	bh_traverse_nearest_k(&r->label.tree, 0, query, k, heap, &heap_size);

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
	if (!r->label.instance || r->label.capacity < needed) {
		VK_DESTROY_BUFFER(r->core.device, r->label.instance, r->label.instance_memory);
		r->label.capacity = needed > 128 ? needed : 128;
		VK_CREATE_HOST_BUFFER(r->core.device, r->core.physicalDevice, sizeof(NodeLabelInstance) * r->label.capacity, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &r->label.instance, &r->label.instance_memory);
	}
}

// Build NodeLabelInstance entries for the nearest nodes.
// Returns the number of instances written.
static uint32_t label_build_lod_instances(Renderer *r, GraphData *graph, uint32_t label_count, DistIdxPair *sorted, int selected_node, NodeLabelInstance *instances)
{
	text_atlas_clear(&r->label.atlas);

	float world_text_scale = 0.003f;
	float fixed_offset = 0.4f;
	uint32_t inst_idx = 0;

	for (uint32_t j = 0; j < label_count && inst_idx < label_count; j++) {
		uint32_t ni = sorted[j].idx;
		if ((int)ni == selected_node)
			continue;
		if (graph->nodes[ni].visible < 0.5f)
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
		text_atlas_render(&r->label.atlas, &globalAtlas, label, &region);

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


void renderer_update_node_labels(Renderer *r, GraphData *graph, vec3 camera_pos, int selected_node)
{
	if (graph->node_count == 0) {
		r->label.count = 0;
		r->detail.visible = false;
		return;
	}

	// Check if labels can be skipped (camera hasn't moved, selection unchanged)
	if (!r->label.tree_needs_rebuild && r->label.cache_valid) {
		vec3 delta;
		glm_vec3_sub(camera_pos, r->label.camera_pos, delta);
		float dist2 = glm_vec3_dot(delta, delta);
		bool camera_moved = dist2 > 0.0001f;
		bool selection_changed = (selected_node != r->label.selected_node);

		if (!camera_moved && !selection_changed) {
			if (selected_node >= 0 && selected_node < (int)graph->node_count)
				r->detail.visible = (r->detail.node >= 0);
			return;
		}
	}

	// Rebuild BH tree if positions changed
	if (r->label.tree_needs_rebuild) {
		if (!label_rebuild_tree(r, graph))
			fprintf(stderr, "renderer_update_node_labels: label_rebuild_tree failed\n");
	}

	r->label.count = 0;

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

	r->label.count = inst_idx;
	free(instances);

	glm_vec3_copy(camera_pos, r->label.camera_pos);
	r->label.selected_node = selected_node;
	r->label.cache_valid = true;

	// Detail card: only rebuild atlas on selection change
	if (selected_node >= 0 && selected_node < (int)graph->node_count) {
		if (selected_node != r->detail.node)
			detail_card_update(r, graph, selected_node);
		else
			r->detail.visible = true;
	} else {
		r->detail.visible = false;
		r->detail.node = -1;
	}
}
