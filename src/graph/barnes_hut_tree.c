#include "graph/barnes_hut_tree.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// Internal helpers
// ============================================================================

static bh_node_t *bh_node_alloc(float x, float y, float z, float s, int point_id)
{
	bh_node_t *n = (bh_node_t *)calloc(1, sizeof(bh_node_t));
	n->type = BH_LEAF;
	n->center.x = 0.0f;
	n->center.y = 0.0f;
	n->center.z = 0.0f;
	n->mass = 0.0f;
	n->cofm.x = x;
	n->cofm.y = y;
	n->cofm.z = z;
	n->s = s;
	n->point_id = point_id;
	n->num_particles = 0;
	n->dfs_index = 0;
	for (int i = 0; i < 8; i++)
		n->children[i] = NULL;
	return n;
}

void bh_tree_reset(bh_tree_t *tree)
{
	if (!tree)
		return;
	if (tree->root) {
		bh_tree_destroy(tree);
	}
	tree->root = bh_node_alloc(0.0f, 0.0f, 0.0f, tree->grid_size * 0.5f, -1);
}

// ============================================================================
// Lifecycle
// ============================================================================

bh_tree_t *bh_tree_create(float grid_size, float theta)
{
	bh_tree_t *t = (bh_tree_t *)calloc(1, sizeof(bh_tree_t));
	t->grid_size = grid_size;
	t->theta = theta;
	t->root = bh_node_alloc(0.0f, 0.0f, 0.0f, grid_size * 0.5f, -1);
	return t;
}

static void bh_node_free_recursive(bh_node_t *n)
{
	if (!n)
		return;
	if (n->type == BH_INTERNAL) {
		for (int i = 0; i < 8; i++)
			bh_node_free_recursive(n->children[i]);
	}
	free(n);
}

void bh_tree_destroy(bh_tree_t *tree)
{
	if (!tree)
		return;
	bh_node_free_recursive(tree->root);
	tree->root = NULL;
	free(tree);
}

// ============================================================================
// Z-order sort (ported from zorder_knn::Less)
// ============================================================================

static uint32_t bh_float_to_uint(float x)
{
	uint32_t xi;
	memcpy(&xi, &x, sizeof(xi));
	return xi;
}

static int bh_float_exp(uint32_t xi)
{
	uint32_t uxi = xi & 0x7fffffffu;
	if (uxi == 0 || uxi >= 0x7f800000u)
		return 0;
	uxi = uxi >> 23;
	return (uxi == 0) ? -126 : (int)uxi - 127;
}

static int bh_float_xor_msb(float p, float q)
{
	if (p == q || p == -q)
		return -2147483647 - 1; // INT_MIN

	uint32_t pui = bh_float_to_uint(p);
	uint32_t qui = bh_float_to_uint(q);

	int p_exp = bh_float_exp(pui);
	int q_exp = bh_float_exp(qui);

	if (p_exp == q_exp) {
		uint32_t xor_sig = (pui & 0x007fffff) ^ (qui & 0x007fffff);
		if (xor_sig > 0) {
			// 32-bit log base 2 lookup
			static const signed char log2_tab[256] = {-1, 0, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7};

			int l2 = -1;
			uint32_t x16 = xor_sig >> 16;
			if (x16) {
				uint32_t x8 = x16 >> 8;
				l2 = (x8) ? (int)log2_tab[x8] + 24 : (int)log2_tab[x16] + 16;
			} else {
				uint32_t x8 = xor_sig >> 8;
				l2 = (x8) ? (int)log2_tab[x8] + 8 : (int)log2_tab[xor_sig];
			}
			if (l2 < 0)
				return p_exp;
			return p_exp + l2 - 23;
		} else {
			return p_exp;
		}
	}
	return (p_exp > q_exp) ? p_exp : q_exp;
}

static int bh_zorder_less_3d(const bh_point_t *p, const bh_point_t *q)
{
	int x = -2147483647 - 1; // INT_MIN
	int k = 0;

	// dimension 2 (Z)
	if ((p->pos.z < 0.0f) != (q->pos.z < 0.0f))
		return p->pos.z < q->pos.z;
	int y = bh_float_xor_msb(p->pos.z, q->pos.z);
	if (x < y) {
		x = y;
		k = 2;
	}

	// dimension 1 (Y)
	if ((p->pos.y < 0.0f) != (q->pos.y < 0.0f))
		return p->pos.y < q->pos.y;
	y = bh_float_xor_msb(p->pos.y, q->pos.y);
	if (x < y) {
		x = y;
		k = 1;
	}

	// dimension 0 (X)
	if ((p->pos.x < 0.0f) != (q->pos.x < 0.0f))
		return p->pos.x < q->pos.x;
	y = bh_float_xor_msb(p->pos.x, q->pos.x);
	if (x < y) {
		x = y;
		k = 0;
	}

	float pk = (k == 0) ? p->pos.x : ((k == 1) ? p->pos.y : p->pos.z);
	float qk = (k == 0) ? q->pos.x : ((k == 1) ? q->pos.y : q->pos.z);
	return pk < qk;
}

static int bh_compare_points(const void *a, const void *b)
{
	const bh_point_t *pa = (const bh_point_t *)a;
	const bh_point_t *pb = (const bh_point_t *)b;
	return bh_zorder_less_3d(pa, pb) ? -1 : 1;
}

void bh_points_sort_zorder(bh_point_t *points, int count)
{
	qsort(points, (size_t)count, sizeof(bh_point_t), bh_compare_points);
}

// ============================================================================
// Tree insertion
// ============================================================================

void bh_tree_build(bh_tree_t *tree, bh_point_t *points, int count)
{
	if (!tree || !points || count < 1)
		return;

	// Reset tree (free old, create new root)
	bh_node_free_recursive(tree->root);
	tree->root = bh_node_alloc(0.0f, 0.0f, 0.0f, tree->grid_size * 0.5f, -1);

	for (int i = 0; i < count; i++) {
		// Create a leaf node for each point
		bh_node_t *pn = bh_node_alloc(points[i].pos.x, points[i].pos.y, points[i].pos.z, tree->grid_size * 0.5f, points[i].id);
		pn->mass = points[i].mass;

		// Insert into octree from root
		bh_node_t *node = tree->root;
		float s = tree->grid_size * 0.5f;

		while (1) {
			int octant = 0;
			float ox = 0.0f, oy = 0.0f, oz = 0.0f;

			if (node->cofm.z < pn->cofm.z) {
				octant = 4;
				oz = s;
			}
			if (node->cofm.y < pn->cofm.y) {
				octant += 2;
				oy = s;
			}
			if (node->cofm.x < pn->cofm.x) {
				octant += 1;
				ox = s;
			}

			bh_node_t *child = node->children[octant];

			if (child == NULL) {
				pn->s = s;
				node->children[octant] = pn;
				break;
			} else if (child->type == BH_LEAF) {
				// Split: create new internal node, reinsert both
				float half = 0.5f * s;
				float cx = (node->cofm.x - half) + ox;
				float cy = (node->cofm.y - half) + oy;
				float cz = (node->cofm.z - half) + oz;
				bh_node_t *inner = bh_node_alloc(cx, cy, cz, half, -1);
				inner->type = BH_INTERNAL;

				// Move existing child into inner
				// Need to reinsert child into inner
				{
					int co = 0;
					float cox = 0.0f, coy = 0.0f, coz = 0.0f;
					if (inner->cofm.z < child->cofm.z) {
						co = 4;
						coz = half;
					}
					if (inner->cofm.y < child->cofm.y) {
						co += 2;
						coy = half;
					}
					if (inner->cofm.x < child->cofm.x) {
						co += 1;
						cox = half;
					}
					child->s = half;
					inner->children[co] = child;
				}

				// Insert new point into inner
				{
					int po = 0;
					float pox = 0.0f, poy = 0.0f, poz = 0.0f;
					if (inner->cofm.z < pn->cofm.z) {
						po = 4;
						poz = half;
					}
					if (inner->cofm.y < pn->cofm.y) {
						po += 2;
						poy = half;
					}
					if (inner->cofm.x < pn->cofm.x) {
						po += 1;
						pox = half;
					}
					pn->s = half;
					inner->children[po] = pn;
				}

				node->children[octant] = inner;
				break;
			} else {
				// Internal child: descend
				s = 0.5f * s;
				node = child;
			}
		}
	}
}

// ============================================================================
// Center of mass computation (post-order)
// ============================================================================

void bh_tree_compute_com(bh_tree_t *tree)
{
	// Iterative post-order traversal using a stack
	if (!tree || !tree->root)
		return;

// We'll use a simple recursive approach since the tree depth is limited
// (max depth ~ log2(grid / min_dist))
#define MAX_STACK 1024
	bh_node_t *stack[MAX_STACK];
	int visit_count[MAX_STACK]; // 0 = not visited, 1 = visited once
	int sp = 0;

	stack[sp] = tree->root;
	visit_count[sp] = 0;
	sp++;

	while (sp > 0) {
		bh_node_t *n = stack[sp - 1];
		if (n->type == BH_LEAF || visit_count[sp - 1] == 1) {
			// Process: compute COM from children
			sp--;
			if (n->type == BH_INTERNAL) {
				float total_mass = 0.0f;
				float com_x = 0.0f, com_y = 0.0f, com_z = 0.0f;
				for (int i = 0; i < 8; i++) {
					bh_node_t *c = n->children[i];
					if (c) {
						total_mass += c->mass;
						com_x += c->cofm.x * c->mass;
						com_y += c->cofm.y * c->mass;
						com_z += c->cofm.z * c->mass;
					}
				}
				if (total_mass != 0.0f) {
					n->mass = total_mass;
					n->cofm.x = com_x / total_mass;
					n->cofm.y = com_y / total_mass;
					n->cofm.z = com_z / total_mass;
				}
			}
		} else {
			visit_count[sp - 1] = 1;
			// Push children
			for (int i = 0; i < 8; i++) {
				bh_node_t *c = n->children[i];
				if (c) {
					if (sp >= MAX_STACK) {
						fprintf(stderr, "[BH] Stack overflow in compute_com\n");
						return;
					}
					stack[sp] = c;
					visit_count[sp] = 0;
					sp++;
				}
			}
		}
	}
}

// ============================================================================
// DFS linearization (produces triangle mesh + device nodes)
// ============================================================================

bh_dfs_output_t bh_tree_to_dfs_array(bh_tree_t *tree)
{
	bh_dfs_output_t out;
	memset(&out, 0, sizeof(out));

	if (!tree || !tree->root)
		return out;

	// Estimate capacity: worst case ~ 2 * num_particles (leaf + internal nodes)
	// Start with 256 and grow dynamically
	int capacity = 256;
	out.vertices = (float *)malloc(sizeof(float) * 9 * capacity);
	out.indices = (uint32_t *)malloc(sizeof(uint32_t) * 3 * capacity);
	out.device_nodes = (bh_device_node_t *)calloc(capacity, sizeof(bh_device_node_t));
	out.num_nodes = 0;
	out.min_s = 1e10f;

	// Stack-based DFS (iterative, matching OWL's hostCode.cu treeToDFSArray)
	typedef struct
	{
		bh_node_t *node;
		int level;
	} stack_entry_t;

	stack_entry_t *node_stack = (stack_entry_t *)malloc(sizeof(stack_entry_t) * capacity);
	int sp = 0;

	float triangle_x_offset = 0.0f;
	int dfs_index = 0;

	node_stack[sp].node = tree->root;
	node_stack[sp].level = 0;
	sp++;

	while (sp > 0) {
		sp--;
		bh_node_t *cur = node_stack[sp].node;
		int level = node_stack[sp].level;

		// Grow arrays if needed
		if (out.num_nodes >= capacity) {
			capacity *= 2;
			out.vertices = (float *)realloc(out.vertices, sizeof(float) * 9 * capacity);
			out.indices = (uint32_t *)realloc(out.indices, sizeof(uint32_t) * 3 * capacity);
			out.device_nodes = (bh_device_node_t *)realloc(out.device_nodes, sizeof(bh_device_node_t) * capacity);
			node_stack = (stack_entry_t *)realloc(node_stack, sizeof(stack_entry_t) * capacity);
		}

		float triangle_x_loc = cur->s + triangle_x_offset;
		if (cur->s < out.min_s)
			out.min_s = cur->s;

		// Triangle vertices (matching OWL: X = s + offset, Y = depth, Z = ±0.5)
		int vi = out.num_nodes * 9;
		out.vertices[vi + 0] = triangle_x_loc;
		out.vertices[vi + 1] = (float)level;
		out.vertices[vi + 2] = -0.5f;
		out.vertices[vi + 3] = triangle_x_loc;
		out.vertices[vi + 4] = (float)(level - 1);
		out.vertices[vi + 5] = 0.5f;
		out.vertices[vi + 6] = triangle_x_loc;
		out.vertices[vi + 7] = (float)(level + 1);
		out.vertices[vi + 8] = 0.5f;

		// Indices
		int ii = out.num_nodes * 3;
		int base_vert = out.num_nodes * 3;
		out.indices[ii + 0] = (uint32_t)(base_vert);
		out.indices[ii + 1] = (uint32_t)(base_vert + 1);
		out.indices[ii + 2] = (uint32_t)(base_vert + 2);

		// Device node
		bh_device_node_t *dn = &out.device_nodes[out.num_nodes];
		dn->mass = cur->mass;
		dn->center_of_mass_x = cur->cofm.x;
		dn->center_of_mass_y = cur->cofm.y;
		dn->center_of_mass_z = cur->cofm.z;
		dn->is_leaf = (cur->type == BH_LEAF) ? 1 : 0;
		dn->next_ray_location_x = 0.0f; // set by install_auto_ropes
		dn->next_ray_location_y = 0.0f;
		dn->next_prim_id = (int)(out.num_nodes + 1); // DFS successor
		dn->auto_rope_ray_location_x = 0.0f;
		dn->auto_rope_ray_location_y = 0.0f;
		dn->auto_rope_prim_id = -1; // sentinel = terminate
		dn->num_particles = cur->num_particles;
		for (int p = 0; p < cur->num_particles && p < BH_BUCKET_SIZE; p++)
			dn->particles[p] = cur->particles[p];

		cur->dfs_index = (uint32_t)out.num_nodes;

		// Update triangle_x_offset for next node
		triangle_x_offset = triangle_x_loc;
		out.num_nodes++;

		// Push children in reverse order (matching OWL: for i = 7..0)
		for (int i = 7; i >= 0; i--) {
			if (cur->children[i]) {
				if (sp >= capacity) {
					capacity *= 2;
					node_stack = (stack_entry_t *)realloc(node_stack, sizeof(stack_entry_t) * capacity);
				}
				node_stack[sp].node = cur->children[i];
				node_stack[sp].level = level + 1;
				sp++;
			}
		}
	}

	// Fix the last node's next_prim_id to be num_nodes (sentinel)
	if (out.num_nodes > 0)
		out.device_nodes[out.num_nodes - 1].next_prim_id = out.num_nodes;

	free(node_stack);
	return out;
}

void bh_dfs_output_free(bh_dfs_output_t *out)
{
	if (!out)
		return;
	free(out->vertices);
	free(out->indices);
	free(out->device_nodes);
	out->vertices = NULL;
	out->indices = NULL;
	out->device_nodes = NULL;
	out->num_nodes = 0;
}

// ============================================================================
// Auto-rope installation (BFS, matching OWL hostCode.cu installAutoRopes)
// ============================================================================

void bh_tree_install_auto_ropes(bh_tree_t *tree, bh_device_node_t *device_nodes, int num_nodes)
{
	if (!tree || !tree->root || !device_nodes || num_nodes < 1)
		return;

	// BFS using a stack (matching OWL's ropeStack)
	typedef struct
	{
		bh_node_t *node;
	} rope_entry_t;

	int capacity = num_nodes;
	rope_entry_t *rope_stack = (rope_entry_t *)malloc(sizeof(rope_entry_t) * capacity);
	int sp = 0;

	rope_stack[sp].node = tree->root;
	sp++;

	int initial = 0;
	while (sp > 0) {
		sp--;
		bh_node_t *cur = rope_stack[sp].node;
		int dfs_idx = (int)cur->dfs_index;

		if (initial) {
			if (sp > 0) {
				bh_node_t *rope_node = rope_stack[sp - 1].node;
				int rope_idx = (int)rope_node->dfs_index;
				if (rope_idx > 0) {
					device_nodes[dfs_idx].auto_rope_ray_location_x = device_nodes[rope_idx - 1].next_ray_location_x;
					device_nodes[dfs_idx].auto_rope_ray_location_y = device_nodes[rope_idx - 1].next_ray_location_y;
					device_nodes[dfs_idx].auto_rope_prim_id = device_nodes[rope_idx - 1].next_prim_id;
				} else {
					device_nodes[dfs_idx].auto_rope_ray_location_x = 0.0f;
					device_nodes[dfs_idx].auto_rope_ray_location_y = 0.0f;
					device_nodes[dfs_idx].auto_rope_prim_id = -1;
				}
			} else {
				device_nodes[dfs_idx].auto_rope_ray_location_x = 0.0f;
				device_nodes[dfs_idx].auto_rope_ray_location_y = 0.0f;
				device_nodes[dfs_idx].auto_rope_prim_id = -1;
			}
		} else {
			initial = 1;
		}

		// Push children in reverse order (i = 7..0)
		for (int i = 7; i >= 0; i--) {
			if (cur->children[i]) {
				if (sp >= capacity) {
					capacity *= 2;
					rope_stack = (rope_entry_t *)realloc(rope_stack, sizeof(rope_entry_t) * capacity);
				}
				rope_stack[sp].node = cur->children[i];
				sp++;
			}
		}
	}

	free(rope_stack);
}

// ============================================================================
// Debug print
// ============================================================================

void bh_tree_print(bh_tree_t *tree)
{
	if (!tree || !tree->root)
		return;

	// BFS print
	typedef struct
	{
		bh_node_t *node;
		int depth;
	} print_entry_t;
	print_entry_t *queue = (print_entry_t *)malloc(sizeof(print_entry_t) * 4096);
	int head = 0, tail = 0;

	queue[tail].node = tree->root;
	queue[tail].depth = 0;
	tail++;

	while (head < tail) {
		bh_node_t *n = queue[head].node;
		int d = queue[head].depth;
		head++;

		for (int i = 0; i < d; i++)
			fprintf(stderr, "  ");
		fprintf(stderr, "Node[%u]: mass=%.4f cofm=(%.4f,%.4f,%.4f) s=%.4f type=%s particles=[", n->dfs_index, n->mass, n->cofm.x, n->cofm.y, n->cofm.z, n->s, (n->type == BH_LEAF) ? "LEAF" : "INT");
		for (int i = 0; i < n->num_particles; i++)
			fprintf(stderr, "%d ", n->particles[i]);
		fprintf(stderr, "]\n");

		if (n->type == BH_INTERNAL) {
			for (int i = 0; i < 8; i++) {
				if (n->children[i]) {
					if (tail >= 4096) {
						fprintf(stderr, "[BH] Print queue overflow\n");
						goto done;
					}
					queue[tail].node = n->children[i];
					queue[tail].depth = d + 1;
					tail++;
				}
			}
		}
	}
done:
	free(queue);
}
