/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "graph/layered_sphere.h"
#include "app_state.h"

#include <math.h>
#ifdef _OPENMP
#include <omp.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <igraph_progress.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define MAX_INTRA_ITERS 50
#define MAX_INTER_ITERS 100
#define HILBERT_RES 32768

static double geodesic_distance(double ux, double uy, double uz, double nx, double ny, double nz, double radius)
{
	double cos_angle = ux * nx + uy * ny + uz * nz;
	if (cos_angle > 1.0)
		cos_angle = 1.0;
	if (cos_angle < -1.0)
		cos_angle = -1.0;
	return radius * acos(cos_angle);
}

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

static void rot(int n, int *x, int *y, int rx, int ry)
{
	if (ry == 0) {
		if (rx == 1) {
			*x = n - 1 - *x;
			*y = n - 1 - *y;
		}
		int t = *x;
		*x = *y;
		*y = t;
	}
}

static int xy2d(int n, int x, int y)
{
	int rx, ry, s, d = 0;
	for (s = n / 2; s > 0; s /= 2) {
		rx = (x & s) > 0;
		ry = (y & s) > 0;
		d += s * s * ((3 * rx) ^ ry);
		rot(s, &x, &y, rx, ry);
	}
	return d;
}

static int compare_communities_kcore(const void *a, const void *b)
{
	CommData *commA = (CommData *)a;
	CommData *commB = (CommData *)b;
	double diff = commB->avg_kcore - commA->avg_kcore;
	return (diff > 0) - (diff < 0);
}

static int compare_nodes_placement(const void *a, const void *b)
{
	NodePlacement *nodeA = (NodePlacement *)a;
	NodePlacement *nodeB = (NodePlacement *)b;
	if (nodeA->community_id != nodeB->community_id) {
		return nodeA->community_id - nodeB->community_id;
	}
	if (nodeA->intra_degree != nodeB->intra_degree) {
		return nodeB->intra_degree - nodeA->intra_degree;
	}
	double diff = nodeA->density - nodeB->density;
	return (diff > 0) - (diff < 0);
}

static int compare_points(const void *a, const void *b)
{
	return ((SpherePoint *)a)->hilbert_dist - ((SpherePoint *)b)->hilbert_dist;
}

static int get_vector_int_max(const igraph_vector_int_t *v)
{
	int max_val = 0;
	for (int i = 0; i < igraph_vector_int_size(v); i++) {
		if (VECTOR(*v)[i] > max_val)
			max_val = VECTOR(*v)[i];
	}
	return max_val == 0 ? 1 : max_val;
}

static int find_closest_slot_by_hilbert(SphereGrid *grid, int target_h)
{
	int low = 0, high = grid->max_slots - 1;
	while (low < high) {
		int mid = low + (high - low) / 2;
		if (grid->slots[mid].hilbert_dist < target_h)
			low = mid + 1;
		else
			high = mid;
	}
	int best_idx = low;
	if (low > 0 && abs(grid->slots[low - 1].hilbert_dist - target_h) < abs(grid->slots[low].hilbert_dist - target_h)) {
		best_idx = low - 1;
	}
	return best_idx;
}

static double calculate_move_delta_intra(const igraph_t *ig, const igraph_matrix_t *layout, LayeredSphereContext *ctx, int u, int target_sphere_s, int target_slot_k)
{
	int v = ctx->grids[target_sphere_s].slot_occupant[target_slot_k];
	double current_score = 0.0, potential_score = 0.0;
	double radius = ctx->grids[target_sphere_s].radius;

	double tx = ctx->grids[target_sphere_s].slots[target_slot_k].x;
	double ty = ctx->grids[target_sphere_s].slots[target_slot_k].y;
	double tz = ctx->grids[target_sphere_s].slots[target_slot_k].z;

	int u_slot = ctx->node_to_slot_idx[u];
	double ux = ctx->grids[target_sphere_s].slots[u_slot].x;
	double uy = ctx->grids[target_sphere_s].slots[u_slot].y;
	double uz = ctx->grids[target_sphere_s].slots[u_slot].z;

	double tx_n = tx / radius;
	double ty_n = ty / radius;
	double tz_n = tz / radius;

	double ux_n = ux / radius;
	double uy_n = uy / radius;
	double uz_n = uz / radius;

	igraph_vector_int_t neis;
	if (igraph_vector_int_init(&neis, 0) != IGRAPH_SUCCESS)
		return 0.0;
	igraph_incident(ig, &neis, u, IGRAPH_ALL, IGRAPH_NO_LOOPS);
	for (int i = 0; i < igraph_vector_int_size(&neis); i++) {
		igraph_integer_t from, to;
		igraph_edge(ig, VECTOR(neis)[i], &from, &to);
		int neighbor = (from == u) ? to : from;

		if (neighbor == v || ctx->node_to_sphere_id[neighbor] != target_sphere_s)
			continue;

		double nx = MATRIX(*layout, neighbor, 0), ny = MATRIX(*layout, neighbor, 1), nz = MATRIX(*layout, neighbor, 2);
		double nr = sqrt(nx * nx + ny * ny + nz * nz);
		if (nr < 0.001)
			continue;
		double nx_n = nx / nr, ny_n = ny / nr, nz_n = nz / nr;

		current_score += geodesic_distance(ux_n, uy_n, uz_n, nx_n, ny_n, nz_n, radius);
		potential_score += geodesic_distance(tx_n, ty_n, tz_n, nx_n, ny_n, nz_n, radius);
	}
	igraph_vector_int_destroy(&neis);

	if (v != -1) {
		if (igraph_vector_int_init(&neis, 0) != IGRAPH_SUCCESS)
			return 0.0;
		igraph_incident(ig, &neis, v, IGRAPH_ALL, IGRAPH_NO_LOOPS);
		for (int i = 0; i < igraph_vector_int_size(&neis); i++) {
			igraph_integer_t from, to;
			igraph_edge(ig, VECTOR(neis)[i], &from, &to);
			int neighbor = (from == v) ? to : from;
			if (neighbor == u || ctx->node_to_sphere_id[neighbor] != target_sphere_s)
				continue;

			double nx = MATRIX(*layout, neighbor, 0), ny = MATRIX(*layout, neighbor, 1), nz = MATRIX(*layout, neighbor, 2);
			double nr = sqrt(nx * nx + ny * ny + nz * nz);
			if (nr < 0.001)
				continue;
			double nx_n = nx / nr, ny_n = ny / nr, nz_n = nz / nr;

			current_score += geodesic_distance(tx_n, ty_n, tz_n, nx_n, ny_n, nz_n, radius);
			potential_score += geodesic_distance(ux_n, uy_n, uz_n, nx_n, ny_n, nz_n, radius);
		}
		igraph_vector_int_destroy(&neis);
	}
	return potential_score - current_score;
}

static double calculate_move_delta_inter(const igraph_t *ig, const igraph_matrix_t *layout, LayeredSphereContext *ctx, int u, int target_sphere_s, int target_slot_k)
{
	int v = ctx->grids[target_sphere_s].slot_occupant[target_slot_k];
	double current_score = 0.0, potential_score = 0.0;
	double target_r = ctx->grids[target_sphere_s].radius;

	double tx = ctx->grids[target_sphere_s].slots[target_slot_k].x;
	double ty = ctx->grids[target_sphere_s].slots[target_slot_k].y;
	double tz = ctx->grids[target_sphere_s].slots[target_slot_k].z;

	int u_slot = ctx->node_to_slot_idx[u];
	int u_sphere = ctx->node_to_sphere_id[u];
	double u_r = ctx->grids[u_sphere].radius;
	double ux = ctx->grids[u_sphere].slots[u_slot].x;
	double uy = ctx->grids[u_sphere].slots[u_slot].y;
	double uz = ctx->grids[u_sphere].slots[u_slot].z;

	double avg_radius = (target_r + u_r) * 0.5;

	double tx_n = tx / target_r, ty_n = ty / target_r, tz_n = tz / target_r;
	double ux_n = ux / u_r, uy_n = uy / u_r, uz_n = uz / u_r;

	igraph_vector_int_t neis;
	if (igraph_vector_int_init(&neis, 0) != IGRAPH_SUCCESS)
		return 0.0;
	igraph_incident(ig, &neis, u, IGRAPH_ALL, IGRAPH_NO_LOOPS);
	for (int i = 0; i < igraph_vector_int_size(&neis); i++) {
		igraph_integer_t from, to;
		igraph_edge(ig, VECTOR(neis)[i], &from, &to);
		int neighbor = (from == u) ? to : from;
		if (neighbor == v)
			continue;

		double nx = MATRIX(*layout, neighbor, 0), ny = MATRIX(*layout, neighbor, 1), nz = MATRIX(*layout, neighbor, 2);
		double n_len = sqrt(nx * nx + ny * ny + nz * nz);
		if (n_len < 0.001)
			continue;
		double nx_n = nx / n_len, ny_n = ny / n_len, nz_n = nz / n_len;

		current_score += geodesic_distance(ux_n, uy_n, uz_n, nx_n, ny_n, nz_n, avg_radius);
		potential_score += geodesic_distance(tx_n, ty_n, tz_n, nx_n, ny_n, nz_n, avg_radius);
	}
	igraph_vector_int_destroy(&neis);

	if (v != -1) {
		if (igraph_vector_int_init(&neis, 0) != IGRAPH_SUCCESS)
			return 0.0;
		igraph_incident(ig, &neis, v, IGRAPH_ALL, IGRAPH_NO_LOOPS);
		for (int i = 0; i < igraph_vector_int_size(&neis); i++) {
			igraph_integer_t from, to;
			igraph_edge(ig, VECTOR(neis)[i], &from, &to);
			int neighbor = (from == v) ? to : from;
			if (neighbor == u)
				continue;

			double nx = MATRIX(*layout, neighbor, 0), ny = MATRIX(*layout, neighbor, 1), nz = MATRIX(*layout, neighbor, 2);
			double n_len = sqrt(nx * nx + ny * ny + nz * nz);
			if (n_len < 0.001)
				continue;
			double nx_n = nx / n_len, ny_n = ny / n_len, nz_n = nz / n_len;

			current_score += geodesic_distance(tx_n, ty_n, tz_n, nx_n, ny_n, nz_n, avg_radius);
			potential_score += geodesic_distance(ux_n, uy_n, uz_n, nx_n, ny_n, nz_n, avg_radius);
		}
		igraph_vector_int_destroy(&neis);
	}
	return potential_score - current_score;
}

static void layered_sphere_cleanup(LayeredSphereContext *ctx)
{
	if (ctx->node_to_sphere_id)
		free(ctx->node_to_sphere_id);
	if (ctx->node_to_slot_idx)
		free(ctx->node_to_slot_idx);
	if (ctx->grids) {
		for (int s = 0; s < ctx->num_spheres; s++) {
			if (ctx->grids[s].slots)
				free(ctx->grids[s].slots);
			if (ctx->grids[s].slot_occupant)
				free(ctx->grids[s].slot_occupant);
		}
		free(ctx->grids);
	}
	free(ctx);
}

static bool layered_sphere_iterate(LayeredSphereContext *ctx, const igraph_t *ig)
{
	if (ctx->phase == PHASE_DONE)
		return false;

	int vcount = ctx->vcount;
	int hilbert_res = HILBERT_RES;

	if (ctx->phase == PHASE_INIT) {
		ctx->node_to_sphere_id = malloc(vcount * sizeof(int));
		ctx->node_to_slot_idx = malloc(vcount * sizeof(int));

		igraph_t undirected_ig;
		igraph_copy(&undirected_ig, ig);
		igraph_to_undirected(&undirected_ig, IGRAPH_TO_UNDIRECTED_COLLAPSE, NULL);

		igraph_vector_int_t coreness;
		if (igraph_vector_int_init(&coreness, vcount) != IGRAPH_SUCCESS) {
			igraph_destroy(&undirected_ig);
			return false;
		}
		igraph_coreness(&undirected_ig, &coreness, IGRAPH_ALL);

		igraph_vector_int_t membership;
		if (igraph_vector_int_init(&membership, vcount) != IGRAPH_SUCCESS) {
			igraph_vector_int_destroy(&coreness);
			igraph_destroy(&undirected_ig);
			return false;
		}

		igraph_integer_t ecount = igraph_ecount(ig);
		double graph_density = (vcount > 1) ? (2.0 * ecount) / ((double)vcount * (vcount - 1)) : 0.0;
		double cpm_resolution = fmax(graph_density * 3.0, 0.001);

		if (igraph_is_directed(ig)) {
			igraph_community_leiden(ig, NULL, NULL, NULL, cpm_resolution, 0.01, 1, -1, &membership, NULL, NULL);
		} else {
			igraph_community_leiden_simple(ig, NULL, IGRAPH_LEIDEN_OBJECTIVE_CPM, cpm_resolution, 0.01, 1, -1, &membership, NULL, NULL);
		}

		int num_communities = get_vector_int_max(&membership) + 1;

		CommData *comms = calloc(num_communities, sizeof(CommData));
		for (int i = 0; i < vcount; i++) {
			int c = VECTOR(membership)[i];
			comms[c].comm_id = c;
			comms[c].avg_kcore += VECTOR(coreness)[i];
			comms[c].node_count++;
		}
		for (int i = 0; i < num_communities; i++) {
			if (comms[i].node_count > 0)
				comms[i].avg_kcore /= comms[i].node_count;
		}
		qsort(comms, num_communities, sizeof(CommData), compare_communities_kcore);

		int nucleus_capacity = comms[0].node_count;
		int remaining_nodes = vcount - nucleus_capacity;
		int base_capacity = (int)fmax(15.0, remaining_nodes * 0.015);

		int current_sphere = 0;
		int current_load = 0;
		int *comm_to_sphere = malloc(num_communities * sizeof(int));

		for (int i = 0; i < num_communities; i++) {
			int c_size = comms[i].node_count;
			if (c_size == 0)
				continue;

			int sphere_capacity;
			if (current_sphere == 0) {
				sphere_capacity = nucleus_capacity;
			} else {
				sphere_capacity = base_capacity * current_sphere * current_sphere;
			}

			if (current_load > 0 && current_load + c_size > sphere_capacity) {
				current_sphere++;
				current_load = 0;
			}

			comm_to_sphere[comms[i].comm_id] = current_sphere;
			current_load += c_size;
		}

		ctx->num_spheres = current_sphere + 1;

		for (int i = 0; i < vcount; i++)
			ctx->node_to_sphere_id[i] = comm_to_sphere[VECTOR(membership)[i]];

		free(comms);
		free(comm_to_sphere);
		igraph_vector_int_destroy(&coreness);

		igraph_vector_t transitivity;
		if (igraph_vector_init(&transitivity, vcount) != IGRAPH_SUCCESS) {
			igraph_vector_int_destroy(&membership);
			igraph_destroy(&undirected_ig);
			return false;
		}
		igraph_transitivity_local_undirected(&undirected_ig, &transitivity, igraph_vss_all(), IGRAPH_TRANSITIVITY_ZERO);

		int *intra_degree = calloc(vcount, sizeof(int));
		igraph_integer_t uecount = igraph_ecount(&undirected_ig);
		for (int e = 0; e < uecount; e++) {
			igraph_integer_t from, to;
			igraph_edge(&undirected_ig, e, &from, &to);
			if (VECTOR(membership)[from] == VECTOR(membership)[to]) {
				intra_degree[from]++;
				intra_degree[to]++;
			}
		}

		igraph_destroy(&undirected_ig);

		ctx->grids = calloc(ctx->num_spheres, sizeof(SphereGrid));
		double current_radius = 0.0;

		for (int s = 0; s < ctx->num_spheres; s++) {
			int n_in_group = 0;
			for (int i = 0; i < vcount; i++)
				if (ctx->node_to_sphere_id[i] == s)
					n_in_group++;
			if (n_in_group == 0)
				continue;

			double required_area = n_in_group * 40.0;
			double needed_r = sqrt(required_area / (4.0 * M_PI));

			if (s == 0) {
				current_radius = fmax(5.0, needed_r);
			} else {
				double log_gap = 8.0 + (20.0 / log2(s + 2.0));
				current_radius = fmax(current_radius + log_gap, needed_r);
			}

			ctx->grids[s].radius = current_radius;

			int M_s = (int)fmin(100000, fmax(n_in_group * 3.0, (4.0 * M_PI * current_radius * current_radius) / 20.0));
			ctx->grids[s].max_slots = M_s;
			ctx->grids[s].slot_occupant = malloc(M_s * sizeof(int));
			ctx->grids[s].slots = malloc(M_s * sizeof(SpherePoint));
			for (int k = 0; k < M_s; k++)
				ctx->grids[s].slot_occupant[k] = -1;

			for (int i = 0; i < M_s; i++) {
				double phi = acos(1.0 - 2.0 * (i + 0.5) / M_s);
				double theta = M_PI * (1.0 + sqrt(5.0)) * i;
				ctx->grids[s].slots[i].x = current_radius * cos(theta) * sin(phi);
				ctx->grids[s].slots[i].y = current_radius * sin(theta) * sin(phi);
				ctx->grids[s].slots[i].z = current_radius * cos(phi);
				double nx = fmod(theta, 2 * M_PI) / (2 * M_PI);
				if (nx < 0)
					nx += 1.0;
				ctx->grids[s].slots[i].hilbert_dist = xy2d(hilbert_res, (int)(nx * (hilbert_res - 1)), (int)((phi / M_PI) * (hilbert_res - 1)));
			}
			qsort(ctx->grids[s].slots, M_s, sizeof(SpherePoint), compare_points);

			NodePlacement *group_nodes = malloc(n_in_group * sizeof(NodePlacement));
			int g_idx = 0;
			for (int i = 0; i < vcount; i++) {
				if (ctx->node_to_sphere_id[i] == s) {
					group_nodes[g_idx].id = i;
					group_nodes[g_idx].community_id = VECTOR(membership)[i];
					group_nodes[g_idx].density = VECTOR(transitivity)[i];
					group_nodes[g_idx].intra_degree = intra_degree[i];
					g_idx++;
				}
			}
			qsort(group_nodes, n_in_group, sizeof(NodePlacement), compare_nodes_placement);

			int step = M_s / n_in_group;
			for (int i = 0; i < n_in_group; i++) {
				int nid = group_nodes[i].id;
				int sid = fmin(M_s - 1, i * step);
				ctx->grids[s].slot_occupant[sid] = nid;
				ctx->node_to_slot_idx[nid] = sid;
				MATRIX(*ctx->layout, nid, 0) = ctx->grids[s].slots[sid].x;
				MATRIX(*ctx->layout, nid, 1) = ctx->grids[s].slots[sid].y;
				MATRIX(*ctx->layout, nid, 2) = ctx->grids[s].slots[sid].z;
			}
			free(group_nodes);
		}

		free(intra_degree);
		igraph_vector_int_destroy(&membership);
		igraph_vector_destroy(&transitivity);

		ctx->phase = PHASE_INTRA_SPHERE;
		ctx->phase_iter = 0;
		return true;
	}

	int local_moves = 0;
	bool is_intra = (ctx->phase == PHASE_INTRA_SPHERE);
	int start_s = is_intra ? 0 : (ctx->inter_sphere_pass % 2);
	int step_s = is_intra ? 1 : 2;

	double damping_factor = fmax(0.05, 0.4 * pow(0.95, ctx->phase_iter));

#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic) reduction(+ : local_moves)
#endif
	for (int s = start_s; s < ctx->num_spheres; s += step_s) {
		double radius = ctx->grids[s].radius;

		for (int u = 0; u < vcount; u++) {
			if (ctx->node_to_sphere_id[u] != s)
				continue;
			int current_slot = ctx->node_to_slot_idx[u];

			double bx = 0, by = 0, bz = 0;
			int neighbor_count = 0;

			igraph_vector_int_t neis;
			if (igraph_vector_int_init(&neis, 0) != IGRAPH_SUCCESS)
				continue;
			igraph_incident(ig, &neis, u, IGRAPH_ALL, IGRAPH_NO_LOOPS);

			for (int j = 0; j < (int)igraph_vector_int_size(&neis); j++) {
				igraph_integer_t from, to;
				igraph_edge(ig, VECTOR(neis)[j], &from, &to);
				int neighbor = (from == u) ? to : from;
				int n_sphere = ctx->node_to_sphere_id[neighbor];

				if (is_intra) {
					if (n_sphere != s)
						continue;
					bx += MATRIX(*ctx->layout, neighbor, 0);
					by += MATRIX(*ctx->layout, neighbor, 1);
					bz += MATRIX(*ctx->layout, neighbor, 2);
				} else {
					double nx = MATRIX(*ctx->layout, neighbor, 0);
					double ny = MATRIX(*ctx->layout, neighbor, 1);
					double nz = MATRIX(*ctx->layout, neighbor, 2);
					double n_len = sqrt(nx * nx + ny * ny + nz * nz);
					if (n_len > 0.001) {
						bx += (nx / n_len);
						by += (ny / n_len);
						bz += (nz / n_len);
					}
				}
				neighbor_count++;
			}
			igraph_vector_int_destroy(&neis);

			if (neighbor_count == 0)
				continue;

			double len = sqrt(bx * bx + by * by + bz * bz);
			if (len < 0.0001)
				continue;
			bx = (bx / len) * radius;
			by = (by / len) * radius;
			bz = (bz / len) * radius;

			double phi = acos(bz / radius);
			double theta = atan2(by, bx);
			if (theta < 0)
				theta += 2 * M_PI;

			int target_h = xy2d(hilbert_res, (int)((theta / (2 * M_PI)) * (hilbert_res - 1)), (int)((phi / M_PI) * (hilbert_res - 1)));

			int current_h = ctx->grids[s].slots[current_slot].hilbert_dist;
			int total_h = hilbert_res * hilbert_res;
			int h_delta = target_h - current_h;

			if (h_delta > total_h / 2)
				h_delta -= total_h;
			if (h_delta < -total_h / 2)
				h_delta += total_h;

			int damped_h = current_h + (int)(h_delta * damping_factor);
			if (damped_h < 0)
				damped_h += total_h;
			if (damped_h >= total_h)
				damped_h -= total_h;

			int target_slot = find_closest_slot_by_hilbert(&ctx->grids[s], damped_h);
			if (target_slot == current_slot)
				continue;

			double delta;
			if (is_intra)
				delta = calculate_move_delta_intra(ig, ctx->layout, ctx, u, s, target_slot);
			else
				delta = calculate_move_delta_inter(ig, ctx->layout, ctx, u, s, target_slot);

			if (delta < -0.001) {
				int v = ctx->grids[s].slot_occupant[target_slot];

				MATRIX(*ctx->layout, u, 0) = ctx->grids[s].slots[target_slot].x;
				MATRIX(*ctx->layout, u, 1) = ctx->grids[s].slots[target_slot].y;
				MATRIX(*ctx->layout, u, 2) = ctx->grids[s].slots[target_slot].z;
				ctx->grids[s].slot_occupant[target_slot] = u;
				ctx->node_to_slot_idx[u] = target_slot;

				if (v != -1) {
					MATRIX(*ctx->layout, v, 0) = ctx->grids[s].slots[current_slot].x;
					MATRIX(*ctx->layout, v, 1) = ctx->grids[s].slots[current_slot].y;
					MATRIX(*ctx->layout, v, 2) = ctx->grids[s].slots[current_slot].z;
					ctx->grids[s].slot_occupant[current_slot] = v;
					ctx->node_to_slot_idx[v] = current_slot;
				} else {
					ctx->grids[s].slot_occupant[current_slot] = -1;
				}

				local_moves++;
			}
		}
	}

	if (ctx->phase == PHASE_INTRA_SPHERE) {
		if (local_moves == 0 || ctx->phase_iter > MAX_INTRA_ITERS) {
			ctx->phase = PHASE_INTER_SPHERE;
			ctx->phase_iter = 0;
		} else {
			ctx->phase_iter++;
		}
	} else if (ctx->phase == PHASE_INTER_SPHERE) {
		ctx->inter_sphere_pass++;
		if ((local_moves == 0 && (ctx->inter_sphere_pass % 2 == 0)) || ctx->phase_iter > MAX_INTER_ITERS) {
			ctx->phase = PHASE_DONE;
			return false;
		}
		ctx->phase_iter++;
	}

	ctx->current_iter++;
	return (ctx->phase != PHASE_DONE);
}

void *compute_layout_layered_sphere(ExecutionContext *ctx)
{
	igraph_t *graph = &ctx->app_state->current_graph.g;
	if (!ctx->app_state->current_graph.graph_initialized) {
		fprintf(stderr, "[%s] Graph not initialized\n", __func__);
		return NULL;
	}
	igraph_integer_t vcount = igraph_vcount(graph);
	igraph_matrix_t *result = IGRAPH_MALLOC(sizeof(igraph_matrix_t));
	if (igraph_matrix_init(result, vcount, 3) != IGRAPH_SUCCESS) {
		IGRAPH_FREE(result);
		return NULL;
	}

	LayeredSphereContext *ls_ctx = calloc(1, sizeof(LayeredSphereContext));
	ls_ctx->vcount = vcount;
	ls_ctx->layout = result;
	ls_ctx->phase = PHASE_INIT;
	ls_ctx->current_iter = 0;

	igraph_progress("Layered Sphere layout", 0.0, NULL);

	const double intra_weight = 50.0;
	const double inter_weight = 50.0;

	while (layered_sphere_iterate(ls_ctx, graph)) {
		double pct = 0.0;
		if (ls_ctx->phase == PHASE_INTRA_SPHERE) {
			pct = intra_weight * ((double)ls_ctx->phase_iter / MAX_INTRA_ITERS);
		} else if (ls_ctx->phase == PHASE_INTER_SPHERE) {
			pct = intra_weight + inter_weight * ((double)ls_ctx->phase_iter / MAX_INTER_ITERS);
		}
		igraph_progress("Layered Sphere layout", pct, NULL);
	}

	igraph_progress("Layered Sphere layout", 100.0, NULL);

	layered_sphere_cleanup(ls_ctx);

	return result;
}
