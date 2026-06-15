#ifndef BARNES_HUT_TREE_H
#define BARNES_HUT_TREE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BH_BUCKET_SIZE 32
#define BH_THRESHOLD_DEFAULT 0.5f
#define BH_GRAV_DEFAULT 0.1f

// ---------------------------------------------------------------------------
// 3D vector
// ---------------------------------------------------------------------------
typedef struct
{
	float x, y, z;
} bh_vec3f;

// ---------------------------------------------------------------------------
// Particle point (positions + mass)
// ---------------------------------------------------------------------------
typedef struct
{
	bh_vec3f pos;
	float mass;
	int id;
} bh_point_t;

// ---------------------------------------------------------------------------
// BH octree node types
// ---------------------------------------------------------------------------
typedef enum { BH_INTERNAL, BH_LEAF } bh_node_type_t;

// ---------------------------------------------------------------------------
// CPU-side octree node
// ---------------------------------------------------------------------------
typedef struct bh_node
{
	bh_node_type_t type;
	bh_vec3f center; // quadrant center (not used in force calc)
	float mass;		 // total mass of subtree
	float s;		 // half-side-length
	bh_vec3f cofm;	 // center of mass
	struct bh_node *children[8];
	int particles[BH_BUCKET_SIZE];
	int num_particles;
	int point_id;
	uint32_t dfs_index;
} bh_node_t;

// ---------------------------------------------------------------------------
// GPU-facing device node (matches GLSL struct in bh_force.comp)
// ---------------------------------------------------------------------------
typedef struct
{
	float mass;
	float center_of_mass_x;
	float center_of_mass_y;
	float center_of_mass_z;
	float next_ray_location_x;
	float next_ray_location_y;
	int next_prim_id;
	float auto_rope_ray_location_x;
	float auto_rope_ray_location_y;
	int auto_rope_prim_id;
	int particles[BH_BUCKET_SIZE];
	int num_particles;
	uint32_t is_leaf;
} bh_device_node_t;

// ---------------------------------------------------------------------------
// Output arrays from DFS linearization (caller must free)
// ---------------------------------------------------------------------------
typedef struct
{
	float *aabbs; // 6 floats per node (minXYZ, maxXYZ) — matches VkAabbPositionsKHR
	bh_device_node_t *device_nodes;
	int num_nodes;
	float min_s;
} bh_dfs_output_t;

// ---------------------------------------------------------------------------
// BH tree (opaque handle)
// ---------------------------------------------------------------------------
typedef struct
{
	bh_node_t *root;
	float grid_size;
	float theta;
} bh_tree_t;

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

// Lifecycle
bh_tree_t *bh_tree_create(float grid_size, float theta);
void bh_tree_destroy(bh_tree_t *tree);
void bh_tree_reset(bh_tree_t *tree);

// Z-order sort (ported from less.hpp)
void bh_points_sort_zorder(bh_point_t *points, int count);

// Build tree from sorted points
void bh_tree_build(bh_tree_t *tree, bh_point_t *points, int count);

// Compute center of mass (post-order traversal)
void bh_tree_compute_com(bh_tree_t *tree);

// DFS linearization (produces vertex/index/device arrays)
// Returns a bh_dfs_output_t that the caller must free with bh_dfs_output_free
bh_dfs_output_t bh_tree_to_dfs_array(bh_tree_t *tree);

// Install auto-rope pointers (must be called after dfs_output is populated)
void bh_tree_install_auto_ropes(bh_tree_t *tree, bh_device_node_t *device_nodes, int num_nodes);

// Free DFS output
void bh_dfs_output_free(bh_dfs_output_t *out);

// Debug: print tree
void bh_tree_print(bh_tree_t *tree);

#endif // BARNES_HUT_TREE_H
