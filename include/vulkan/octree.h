#ifndef OCTREE_H
#define OCTREE_H

#include <stdint.h>

typedef struct
{
	float mass;
	float center_of_mass[3];
	float geom_center[3];
	float size;
	int children[8];
	int point_start;
	int point_count;
	int is_leaf;
	int level;
} OctreeNode;

typedef struct
{
	OctreeNode *nodes;
	int node_count;
	int capacity;
	float *points;		// [N*3], permuted
	int *point_indices; // permuted for contiguous leaf ranges
	int point_count;
	int dim;
	int max_level;
	int leaf_capacity;
	float root_size;
	float root_center[3];
} Octree;

int octree_build(Octree *tree, const float *positions, int n, int dim);
void octree_destroy(Octree *tree);
int octree_adaptive_levels(int n);

void octree_get_node_levels(const Octree *tree, int *node_level, float *masses, float *centers);

void octree_level_radii(const Octree *tree, int num_levels, float *radii);

#endif