// =============================================================================
// Octree — Spatial Partitioning Tree for Multi-Geometry BLAS
// =============================================================================
//
// Builds an octree over 3D point positions, assigning each point to a leaf
// cell. The per-node level, aggregated mass, and center-of-mass are extracted
// for use by the multi-geometry BLAS approach, which assigns each node one
// AABB sized by its octree cell's radius.
//
// =============================================================================

#include "vulkan/octree.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// Internal Helpers
// ============================================================================

static int octree_grow(Octree *tree)
{
	if (tree->node_count >= tree->capacity) {
		int new_cap = tree->capacity ? tree->capacity * 2 : 1024;
		OctreeNode *new_nodes = realloc(tree->nodes, sizeof(OctreeNode) * new_cap);
		if (!new_nodes)
			return -1;
		tree->nodes = new_nodes;
		tree->capacity = new_cap;
	}
	return 0;
}

static int octree_alloc_node(Octree *tree)
{
	if (octree_grow(tree) != 0)
		return -1;
	return tree->node_count++;
}

static int octree_build_recursive(Octree *tree, int node_idx, const float *center, float half_size, int level, int *indices, int count)
{
	if (node_idx < 0 || count <= 0)
		return 0;

	OctreeNode *node = &tree->nodes[node_idx];
	node->geom_center[0] = center[0];
	node->geom_center[1] = center[1];
	node->geom_center[2] = center[2];
	node->size = half_size * 2.0f;
	node->level = level;
	for (int c = 0; c < 8; c++)
		node->children[c] = -1;

	// Compute mass (all masses = 1.0) and center-of-mass
	float com[3] = {0.0f, 0.0f, 0.0f};
	for (int i = 0; i < count; i++) {
		int pi = indices[i];
		for (int d = 0; d < tree->dim; d++)
			com[d] += tree->points[pi * tree->dim + d];
	}
	float inv_count = 1.0f / (float)count;
	node->mass = (float)count;
	for (int d = 0; d < 3; d++)
		node->center_of_mass[d] = com[d] * inv_count;

	// Leaf condition
	if (count <= tree->leaf_capacity || level >= tree->max_level) {
		node->is_leaf = 1;
		node->point_start = indices[0];
		node->point_count = count;
		// Permute point_indices for contiguous leaf range
		for (int i = 0; i < count; i++)
			tree->point_indices[node->point_start + i] = indices[i];
		return 0;
	}

	node->is_leaf = 0;
	node->point_start = -1;
	node->point_count = count;

	// Partition into 8 octants
	int child_counts[8] = {0};
	for (int i = 0; i < count; i++) {
		int pi = indices[i];
		int octant = 0;
		if (tree->points[pi * tree->dim + 0] >= center[0])
			octant |= 1;
		if (tree->points[pi * tree->dim + 1] >= center[1])
			octant |= 2;
		if (tree->dim > 2 && tree->points[pi * tree->dim + 2] >= center[2])
			octant |= 4;
		child_counts[octant]++;
	}

	// Compute offsets for each child
	int child_offsets[8];
	int offset = 0;
	for (int c = 0; c < 8; c++) {
		child_offsets[c] = offset;
		offset += child_counts[c];
	}

	// Temporary buffer for partitioning
	int *temp = malloc(sizeof(int) * count);
	if (!temp)
		return -1;
	memcpy(temp, indices, sizeof(int) * count);

	// Place indices into their octant's region
	int child_cursor[8];
	memcpy(child_cursor, child_offsets, sizeof(int) * 8);
	for (int i = 0; i < count; i++) {
		int pi = temp[i];
		int octant = 0;
		if (tree->points[pi * tree->dim + 0] >= center[0])
			octant |= 1;
		if (tree->points[pi * tree->dim + 1] >= center[1])
			octant |= 2;
		if (tree->dim > 2 && tree->points[pi * tree->dim + 2] >= center[2])
			octant |= 4;
		indices[child_cursor[octant]++] = pi;
	}
	free(temp);

	// Create children
	float child_half = half_size * 0.5f;
	for (int c = 0; c < 8; c++) {
		if (child_counts[c] == 0)
			continue;

		float child_center[3];
		child_center[0] = center[0] + ((c & 1) ? child_half : -child_half);
		child_center[1] = center[1] + ((c & 2) ? child_half : -child_half);
		child_center[2] = center[2] + ((c & 4) ? child_half : -child_half);

		int child_idx = octree_alloc_node(tree);
		if (child_idx < 0)
			return -1;
		node->children[c] = child_idx;

		if (octree_build_recursive(tree, child_idx, child_center, child_half, level + 1, indices + child_offsets[c], child_counts[c]) != 0)
			return -1;
	}

	return 0;
}

// ============================================================================
// octree_adaptive_levels
// ============================================================================

int octree_adaptive_levels(int n)
{
	if (n <= 1)
		return 1;
	double log_val = log2((double)n) / 3.0;
	int L = (int)ceil(log_val) + 2;
	if (L < 3)
		L = 3;
	if (L > 8)
		L = 8;
	return L;
}

// ============================================================================
// octree_build
// ============================================================================

int octree_build(Octree *tree, const float *positions, int n, int dim)
{
	memset(tree, 0, sizeof(Octree));
	tree->point_count = n;
	tree->dim = dim;
	tree->leaf_capacity = 8;
	tree->max_level = octree_adaptive_levels(n);
	tree->capacity = 0;
	tree->nodes = NULL;
	tree->node_count = 0;

	// Allocate point storage and indices
	tree->points = malloc(sizeof(float) * n * dim);
	tree->point_indices = malloc(sizeof(int) * n);
	if (!tree->points || !tree->point_indices) {
		free(tree->points);
		free(tree->point_indices);
		return -1;
	}
	memcpy(tree->points, positions, sizeof(float) * n * dim);
	for (int i = 0; i < n; i++)
		tree->point_indices[i] = i;

	// Compute bounding box
	float bbox_min[3] = {INFINITY, INFINITY, INFINITY};
	float bbox_max[3] = {-INFINITY, -INFINITY, -INFINITY};
	for (int i = 0; i < n; i++) {
		for (int d = 0; d < dim; d++) {
			float v = positions[i * dim + d];
			if (v < bbox_min[d])
				bbox_min[d] = v;
			if (v > bbox_max[d])
				bbox_max[d] = v;
		}
	}
	for (int d = dim; d < 3; d++) {
		bbox_min[d] = 0.0f;
		bbox_max[d] = 0.0f;
	}

	float extent[3] = {bbox_max[0] - bbox_min[0], bbox_max[1] - bbox_min[1], bbox_max[2] - bbox_min[2]};
	float max_extent = fmaxf(extent[0], fmaxf(extent[1], extent[2]));
	if (max_extent < 1e-8f)
		max_extent = 1.0f;

	tree->root_size = max_extent * 1.01f;
	for (int d = 0; d < 3; d++)
		tree->root_center[d] = (bbox_min[d] + bbox_max[d]) * 0.5f;

	// Allocate root node
	int root_idx = octree_alloc_node(tree);
	if (root_idx != 0) {
		octree_destroy(tree);
		return -1;
	}

	// Build recursively
	int *indices = malloc(sizeof(int) * n);
	if (!indices) {
		octree_destroy(tree);
		return -1;
	}
	for (int i = 0; i < n; i++)
		indices[i] = i;

	float half_size = tree->root_size * 0.5f;
	int ret = octree_build_recursive(tree, 0, tree->root_center, half_size, 0, indices, n);
	free(indices);

	if (ret != 0) {
		octree_destroy(tree);
		return -1;
	}

	return 0;
}

// ============================================================================
// octree_destroy
// ============================================================================

void octree_destroy(Octree *tree)
{
	free(tree->nodes);
	free(tree->points);
	free(tree->point_indices);
	memset(tree, 0, sizeof(Octree));
}

// ============================================================================
// octree_get_node_levels
// ============================================================================

void octree_get_node_levels(const Octree *tree, int *node_level, float *masses, float *centers)
{
	for (int i = 0; i < tree->point_count; i++) {
		int pi = tree->point_indices[i];
		float px = tree->points[pi * tree->dim + 0];
		float py = (tree->dim > 1) ? tree->points[pi * tree->dim + 1] : 0.0f;
		float pz = (tree->dim > 2) ? tree->points[pi * tree->dim + 2] : 0.0f;

		// Traverse from root to leaf
		int idx = 0;
		while (idx >= 0) {
			OctreeNode *node = &tree->nodes[idx];
			if (node->is_leaf) {
				node_level[i] = node->level;
				masses[i] = node->mass;
				centers[i * 3 + 0] = node->center_of_mass[0];
				centers[i * 3 + 1] = node->center_of_mass[1];
				centers[i * 3 + 2] = node->center_of_mass[2];
				break;
			}
			// Determine which child contains this point
			int octant = 0;
			if (px >= node->geom_center[0])
				octant |= 1;
			if (py >= node->geom_center[1])
				octant |= 2;
			if (pz >= node->geom_center[2])
				octant |= 4;
			idx = node->children[octant];
			if (idx < 0) {
				// Point falls outside children — shouldn't happen, but handle gracefully
				node_level[i] = node->level;
				masses[i] = node->mass;
				centers[i * 3 + 0] = node->center_of_mass[0];
				centers[i * 3 + 1] = node->center_of_mass[1];
				centers[i * 3 + 2] = node->center_of_mass[2];
				break;
			}
		}
	}
}

// ============================================================================
// octree_level_radii
// ============================================================================

void octree_level_radii(const Octree *tree, int num_levels, float *radii)
{
	for (int k = 0; k < num_levels; k++)
		radii[k] = tree->root_size / (2.0f * (float)(1 << k));
}