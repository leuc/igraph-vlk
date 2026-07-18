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
