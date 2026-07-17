/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "graph/layered_sphere_common.h"

double geodesic_distance(double ux, double uy, double uz, double nx, double ny, double nz, double radius)
{
	double cos_angle = ux * nx + uy * ny + uz * nz;
	if (cos_angle > 1.0)
		cos_angle = 1.0;
	if (cos_angle < -1.0)
		cos_angle = -1.0;
	return radius * acos(cos_angle);
}

void rot(int n, int *x, int *y, int rx, int ry)
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

int xy2d(int n, int x, int y)
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

int compare_communities_kcore(const void *a, const void *b)
{
	CommData *commA = (CommData *)a;
	CommData *commB = (CommData *)b;
	double diff = commB->avg_kcore - commA->avg_kcore;
	return (diff > 0) - (diff < 0);
}

int compare_nodes_placement(const void *a, const void *b)
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

int compare_points(const void *a, const void *b)
{
	return ((SpherePoint *)a)->hilbert_dist - ((SpherePoint *)b)->hilbert_dist;
}

int get_vector_int_max(const igraph_vector_int_t *v)
{
	int max_val = 0;
	for (int i = 0; i < igraph_vector_int_size(v); i++) {
		if (VECTOR(*v)[i] > max_val)
			max_val = VECTOR(*v)[i];
	}
	return max_val == 0 ? 1 : max_val;
}

int find_closest_slot_by_hilbert(SphereGrid *grid, int target_h)
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

double calculate_move_delta_intra(const igraph_t *ig, const igraph_matrix_t *layout, LayeredSphereContext *ctx, int u, int target_sphere_s, int target_slot_k)
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

double calculate_move_delta_inter(const igraph_t *ig, const igraph_matrix_t *layout, LayeredSphereContext *ctx, int u, int target_sphere_s, int target_slot_k)
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

double sphere_radius_for(int s, int n_in_group, double prev_radius)
{
	double required_area = n_in_group * 40.0;
	double needed_r = sqrt(required_area / (4.0 * M_PI));

	if (s == 0) {
		return fmax(5.0, needed_r);
	}
	double log_gap = 8.0 + (20.0 / log2(s + 2.0));
	return fmax(prev_radius + log_gap, needed_r);
}

int sphere_slot_count(int n_in_group, double radius)
{
	return (int)fmin(100000, fmax(n_in_group * 3.0, (4.0 * M_PI * radius * radius) / 20.0));
}

void compute_slot_point(int i, int M_s, double radius, int hilbert_res, SpherePoint *out)
{
	double phi = acos(1.0 - 2.0 * (i + 0.5) / M_s);
	double theta = M_PI * (1.0 + sqrt(5.0)) * i;
	out->x = radius * cos(theta) * sin(phi);
	out->y = radius * sin(theta) * sin(phi);
	out->z = radius * cos(phi);
	double nx = fmod(theta, 2 * M_PI) / (2 * M_PI);
	if (nx < 0)
		nx += 1.0;
	out->hilbert_dist = xy2d(hilbert_res, (int)(nx * (hilbert_res - 1)), (int)((phi / M_PI) * (hilbert_res - 1)));
}

bool build_sphere_grid(SphereGrid *grid, int n_in_group, double radius, int hilbert_res)
{
	int M_s = sphere_slot_count(n_in_group, radius);
	grid->radius = radius;
	grid->max_slots = M_s;
	grid->slot_occupant = malloc(M_s * sizeof(int));
	grid->slots = malloc(M_s * sizeof(SpherePoint));
	if (!grid->slot_occupant || !grid->slots) {
		free(grid->slot_occupant);
		free(grid->slots);
		grid->slot_occupant = NULL;
		grid->slots = NULL;
		return false;
	}
	if (igraph_vector_int_init(&grid->neis, 0) != IGRAPH_SUCCESS) {
		free(grid->slot_occupant);
		free(grid->slots);
		grid->slot_occupant = NULL;
		grid->slots = NULL;
		return false;
	}

	for (int k = 0; k < M_s; k++)
		grid->slot_occupant[k] = -1;

	for (int i = 0; i < M_s; i++)
		compute_slot_point(i, M_s, radius, hilbert_res, &grid->slots[i]);

	qsort(grid->slots, M_s, sizeof(SpherePoint), compare_points);

	return true;
}

NodePlacement *placement_order_for_sphere(LayeredSphereContext *ctx, int s, int n_in_group, const igraph_vector_int_t *membership, const igraph_vector_t *transitivity, const int *intra_degree, int *out_count)
{
	NodePlacement *group_nodes = malloc(n_in_group * sizeof(NodePlacement));
	int g_idx = 0;
	for (int i = 0; i < ctx->vcount; i++) {
		if (ctx->node_to_sphere_id[i] == s) {
			group_nodes[g_idx].id = i;
			group_nodes[g_idx].community_id = VECTOR(*membership)[i];
			group_nodes[g_idx].density = VECTOR(*transitivity)[i];
			group_nodes[g_idx].intra_degree = intra_degree[i];
			g_idx++;
		}
	}
	qsort(group_nodes, g_idx, sizeof(NodePlacement), compare_nodes_placement);
	*out_count = g_idx;
	return group_nodes;
}

void seed_slots_for_sphere(LayeredSphereContext *ctx, int s, NodePlacement *grp, int n_in_group)
{
	int M_s = ctx->grids[s].max_slots;
	int step = M_s / n_in_group;
	for (int i = 0; i < n_in_group; i++) {
		int nid = grp[i].id;
		int sid = fmin(M_s - 1, i * step);
		ctx->grids[s].slot_occupant[sid] = nid;
		ctx->node_to_slot_idx[nid] = sid;
		MATRIX(*ctx->layout, nid, 0) = ctx->grids[s].slots[sid].x;
		MATRIX(*ctx->layout, nid, 1) = ctx->grids[s].slots[sid].y;
		MATRIX(*ctx->layout, nid, 2) = ctx->grids[s].slots[sid].z;
	}
}

int node_hilbert_target(const igraph_t *ig, const igraph_matrix_t *layout, LayeredSphereContext *ctx, int u, int s, bool is_intra, double damping_factor, int hilbert_res)
{
	int current_slot = ctx->node_to_slot_idx[u];
	double radius = ctx->grids[s].radius;

	double bx = 0, by = 0, bz = 0;
	int neighbor_count = 0;

	igraph_vector_int_t *neis = &ctx->grids[s].neis;
	igraph_vector_int_clear(neis);
	igraph_incident(ig, neis, u, IGRAPH_ALL, IGRAPH_NO_LOOPS);

	for (int j = 0; j < (int)igraph_vector_int_size(neis); j++) {
		igraph_integer_t from, to;
		igraph_edge(ig, VECTOR(*neis)[j], &from, &to);
		int neighbor = (from == u) ? to : from;
		int n_sphere = ctx->node_to_sphere_id[neighbor];

		if (is_intra) {
			if (n_sphere != s)
				continue;
			bx += MATRIX(*layout, neighbor, 0);
			by += MATRIX(*layout, neighbor, 1);
			bz += MATRIX(*layout, neighbor, 2);
		} else {
			double nx = MATRIX(*layout, neighbor, 0);
			double ny = MATRIX(*layout, neighbor, 1);
			double nz = MATRIX(*layout, neighbor, 2);
			double n_len = sqrt(nx * nx + ny * ny + nz * nz);
			if (n_len > 0.001) {
				bx += (nx / n_len);
				by += (ny / n_len);
				bz += (nz / n_len);
			}
		}
		neighbor_count++;
	}

	if (neighbor_count == 0)
		return current_slot;

	double len = sqrt(bx * bx + by * by + bz * bz);
	if (len < 0.0001)
		return current_slot;
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

	return find_closest_slot_by_hilbert(&ctx->grids[s], damped_h);
}

void try_move_node(const igraph_t *ig, const igraph_matrix_t *layout, LayeredSphereContext *ctx, int u, int s, int target_slot, int current_slot, bool is_intra, int *out_local_moves)
{
	double delta;
	if (is_intra)
		delta = calculate_move_delta_intra(ig, layout, ctx, u, s, target_slot);
	else
		delta = calculate_move_delta_inter(ig, layout, ctx, u, s, target_slot);

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

		(*out_local_moves)++;
	}
}

void advance_phase(LayeredSphereContext *ctx, int local_moves)
{
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
			return;
		}
		ctx->phase_iter++;
	}
}

void layered_sphere_cleanup(LayeredSphereContext *ctx)
{
	if (ctx->node_to_sphere_id)
		free(ctx->node_to_sphere_id);
	if (ctx->node_to_slot_idx)
		free(ctx->node_to_slot_idx);
	if (ctx->grids) {
		for (int s = 0; s < ctx->num_spheres; s++) {
			if (ctx->grids[s].slots)
				free(ctx->grids[s].slots);
			if (ctx->grids[s].slot_occupant) {
				free(ctx->grids[s].slot_occupant);
				igraph_vector_int_destroy(&ctx->grids[s].neis);
			}
		}
		free(ctx->grids);
	}
	free(ctx);
}
