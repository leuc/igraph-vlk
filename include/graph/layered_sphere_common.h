/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef LAYERED_SPHERE_COMMON_H
#define LAYERED_SPHERE_COMMON_H

#include <igraph.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define MAX_INTRA_ITERS 50
#define MAX_INTER_ITERS 100
#define HILBERT_RES 32768

typedef struct
{
	int comm_id;
	double avg_kcore;
	int node_count;
} CommData;

typedef struct
{
	int id;
	int community_id;
	double density;
	int intra_degree;
} NodePlacement;

typedef struct
{
	double x, y, z;
	int hilbert_dist;
} SpherePoint;

typedef struct
{
	int max_slots;
	int num_occupants;
	double radius;
	SpherePoint *slots;
	int *slot_occupant;
	igraph_vector_int_t neis;
} SphereGrid;

// Rotates point (x,y,z) by unit quaternion q (w,x,y,z) via the standard
// v' = v + 2*w*(qv x v) + 2*(qv x (qv x v)) formula, qv = q's vector part.
static inline void quat_rotate_point(const double q[4], double x, double y, double z, double *ox, double *oy, double *oz)
{
	double qw = q[0], qx = q[1], qy = q[2], qz = q[3];
	double t0 = qy * z - qz * y;
	double t1 = qz * x - qx * z;
	double t2 = qx * y - qy * x;
	double u0 = qy * t2 - qz * t1;
	double u1 = qz * t0 - qx * t2;
	double u2 = qx * t1 - qy * t0;
	*ox = x + 2.0 * (qw * t0 + u0);
	*oy = y + 2.0 * (qw * t1 + u1);
	*oz = z + 2.0 * (qw * t2 + u2);
}

// Writes a sphere slot's 3D coordinates into the layout matrix for node_id,
// applying an optional per-sphere rotation quaternion (w,x,y,z) first — quat
// NULL means unrotated. Shared by every placement path (seed, local-append,
// disconnected-append, try_move_node) so the MATRIX(...) triple isn't
// repeated at each call site.
static inline void write_slot_position(igraph_matrix_t *layout, igraph_integer_t node_id, const SpherePoint *p, const double *quat)
{
	double x = p->x, y = p->y, z = p->z;
	if (quat)
		quat_rotate_point(quat, x, y, z, &x, &y, &z);
	MATRIX(*layout, node_id, 0) = x;
	MATRIX(*layout, node_id, 1) = y;
	MATRIX(*layout, node_id, 2) = z;
}

typedef enum { PHASE_INIT = 0, PHASE_INTRA_SPHERE = 1, PHASE_INTER_SPHERE = 2, PHASE_DONE = 3 } LayoutPhase;

typedef struct LayeredSphereContext
{
	LayoutPhase phase;
	int current_iter;
	int phase_iter;
	int num_spheres;
	SphereGrid *grids;
	int *node_to_sphere_id;
	int *node_to_slot_idx;
	int inter_sphere_pass;
	int vcount;
	igraph_matrix_t *layout;
	const double *sphere_rotation; // optional: flat 4*num_spheres array of per-sphere unit quaternions (w,x,y,z), NULL if no sphere is rotated. Owned by the caller (e.g. DynLayeredSphere); write_slot_position call sites read &sphere_rotation[4*s] for their own sphere s.
} LayeredSphereContext;

double geodesic_distance(double ux, double uy, double uz, double nx, double ny, double nz, double radius);
void rot(int n, int *x, int *y, int rx, int ry);
int xy2d(int n, int x, int y);

int compare_communities_kcore(const void *a, const void *b);
int compare_nodes_placement(const void *a, const void *b);
int compare_points(const void *a, const void *b);

int get_vector_int_max(const igraph_vector_int_t *v);
int find_closest_slot_by_hilbert(SphereGrid *grid, int target_h);

double calculate_move_delta_intra(const igraph_t *ig, const igraph_matrix_t *layout, LayeredSphereContext *ctx, int u, int target_sphere_s, int target_slot_k);
double calculate_move_delta_inter(const igraph_t *ig, const igraph_matrix_t *layout, LayeredSphereContext *ctx, int u, int target_sphere_s, int target_slot_k);

int bucket_communities_into_spheres(const CommData *comms, int num_communities, int total_node_count, int *comm_to_sphere);
double sphere_radius_for(int s, int n_in_group, double prev_radius);
int sphere_slot_count(int n_in_group, double radius);
void compute_slot_point(int i, int M_s, double radius, int hilbert_res, SpherePoint *out);
bool build_sphere_grid(SphereGrid *grid, int n_in_group, double radius, int hilbert_res);
NodePlacement *placement_order_for_sphere(LayeredSphereContext *ctx, int s, int n_in_group, const igraph_vector_int_t *membership, const igraph_vector_t *transitivity, const int *intra_degree, int *out_count);
void seed_slots_for_sphere(LayeredSphereContext *ctx, int s, NodePlacement *grp, int n_in_group);

int node_hilbert_target(const igraph_t *ig, const igraph_matrix_t *layout, LayeredSphereContext *ctx, int u, int s, bool is_intra, double damping_factor, int hilbert_res);
void try_move_node(const igraph_t *ig, const igraph_matrix_t *layout, LayeredSphereContext *ctx, int u, int s, int target_slot, int current_slot, bool is_intra, int *out_local_moves);
void advance_phase(LayeredSphereContext *ctx, int local_moves);

void layered_sphere_cleanup(LayeredSphereContext *ctx);

#endif
