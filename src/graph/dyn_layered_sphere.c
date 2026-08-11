/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "graph/dyn_layered_sphere.h"
#include "graph/dyn_ls_sphere_rotation.h"
#include "graph/layered_sphere_common.h"

#include <igraph_step.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DYN_LS_TIMESTAMP_ATTR "timestamp"
#define DYN_LS_SLOT_HEADROOM_BASE 4.0
#define DYN_LS_SLOT_HEADROOM_PER_SPHERE 0.5
#define DYN_LS_RADIUS_EPS 1e-9
#define DYN_LS_MAX_GRID_SLOTS 100000

static inline double dyn_ls_timer_us(const struct timespec *start, const struct timespec *end)
{
	return (double)(end->tv_sec - start->tv_sec) * 1000000.0 + (double)(end->tv_nsec - start->tv_nsec) / 1000.0;
}

typedef enum {
	DYN_LS_EVENT_NODE_PAIR_CONNECTED,	/* new vertex arrived already wired to the graph (degree > 0) */
	DYN_LS_EVENT_NODE_DISJOINT_ADDED,	/* new vertex arrived isolated (degree == 0) */
	DYN_LS_EVENT_LEVEL_TOUCHED,			/* a coreness level's tree membership changed */
	DYN_LS_EVENT_COMMUNITY_REASSIGNED,	/* a vertex's community changed */
	DYN_LS_EVENT_SPHERE_GEOMETRY_DIRTY, /* a sphere's grid is new/overflowed and needs rebuild/rescale */
	DYN_LS_EVENT_SPHERE_RESEEDED,		/* a dirty sphere was fully re-seeded this batch */
	DYN_LS_EVENT_SPHERE_UNCHANGED,		/* a sphere has no dirty event this batch; positions are re-emitted as-is */
	DYN_LS_EVENT_BATCH_NO_OP,			/* batch produced no vertex/level/community changes at all; layout maintenance skipped entirely */
} DynLsEventType;

static const char *dyn_ls_event_name(DynLsEventType type)
{
	switch (type) {
	case DYN_LS_EVENT_NODE_PAIR_CONNECTED:
		return "node_pair_connected";
	case DYN_LS_EVENT_NODE_DISJOINT_ADDED:
		return "node_disjoint_added";
	case DYN_LS_EVENT_LEVEL_TOUCHED:
		return "level_touched";
	case DYN_LS_EVENT_COMMUNITY_REASSIGNED:
		return "community_reassigned";
	case DYN_LS_EVENT_SPHERE_GEOMETRY_DIRTY:
		return "sphere_geometry_dirty";
	case DYN_LS_EVENT_SPHERE_RESEEDED:
		return "sphere_reseeded";
	case DYN_LS_EVENT_SPHERE_UNCHANGED:
		return "sphere_unchanged";
	case DYN_LS_EVENT_BATCH_NO_OP:
		return "batch_no_op";
	default:
		return "unknown";
	}
}

static DynLsEventType dyn_ls_classify_node_arrival(const igraph_t *g, igraph_integer_t v)
{
	igraph_integer_t deg;
	if (igraph_degree_1(g, &deg, v, IGRAPH_ALL, IGRAPH_NO_LOOPS) != IGRAPH_SUCCESS)
		deg = 0;
	return deg > 0 ? DYN_LS_EVENT_NODE_PAIR_CONNECTED : DYN_LS_EVENT_NODE_DISJOINT_ADDED;
}

/* Per-recompute event counts for throttled diagnostics. */
typedef struct
{
	int level_touched;
	int community_reassigned;
	int sphere_geometry_built;
	int sphere_geometry_overflow;
	int sphere_geometry_rescaled;
	int sphere_reseeded;
	int sphere_unchanged;
} DynLsEventLog;

static void dyn_ls_report_event_log(const DynLsEventLog *elog, int num_spheres, int grid_slot_count, double us_rank, double us_geometry, double us_dirty_mark, double us_remap, double us_seed_or_reflow, double us_total)
{
	static int log_counter = 0;
	if (++log_counter % 32 != 1)
		return;
	fprintf(stderr, "dyn_ls_recompute: spheres=%d slots=%d %s=%d %s=%d %s=%d/%d/%d(built/overflow/rescaled) %s=%d %s=%d | rank=%.0f geometry=%.0f dirty_mark=%.0f remap=%.0f seed_or_reflow=%.0f tot=%.0f us\n", num_spheres, grid_slot_count, dyn_ls_event_name(DYN_LS_EVENT_LEVEL_TOUCHED), elog->level_touched, dyn_ls_event_name(DYN_LS_EVENT_COMMUNITY_REASSIGNED), elog->community_reassigned, dyn_ls_event_name(DYN_LS_EVENT_SPHERE_GEOMETRY_DIRTY), elog->sphere_geometry_built, elog->sphere_geometry_overflow, elog->sphere_geometry_rescaled, dyn_ls_event_name(DYN_LS_EVENT_SPHERE_RESEEDED), elog->sphere_reseeded, dyn_ls_event_name(DYN_LS_EVENT_SPHERE_UNCHANGED), elog->sphere_unchanged, us_rank, us_geometry, us_dirty_mark, us_remap, us_seed_or_reflow, us_total);
}

struct DynLayeredSphere
{
	SphereGrid *grids;
	int grids_capacity;
	int grid_slot_count;
	igraph_integer_t last_seen_vcount;
	int *level_to_grid_slot;
	int level_to_grid_slot_cap;
	int *node_to_sphere; // Persisted for rotation ticks between recomputes.
	int node_to_sphere_cap;
	double *sphere_rotation;
	double *sphere_prev_omega;
	int *sphere_rotation_steps;
	int *sphere_settled_streak;
	int sphere_rotation_capacity;
};

static void dyn_ls_free_grid_contents(DynLayeredSphere *dls)
{
	for (int s = 0; s < dls->grid_slot_count; s++) {
		free(dls->grids[s].slots);
		dls->grids[s].slots = NULL;
		if (dls->grids[s].slot_occupant) {
			free(dls->grids[s].slot_occupant);
			dls->grids[s].slot_occupant = NULL;
			igraph_vector_int_destroy(&dls->grids[s].neis);
		}
	}
	dls->grid_slot_count = 0;
	for (int l = 0; l < dls->level_to_grid_slot_cap; l++)
		dls->level_to_grid_slot[l] = -1;
}

static bool dyn_ls_ensure_grids_capacity(DynLayeredSphere *dls, int needed)
{
	if (needed <= dls->grids_capacity)
		return true;
	int cap = dls->grids_capacity ? dls->grids_capacity : 4;
	while (cap < needed)
		cap *= 2;
	SphereGrid *grown = realloc(dls->grids, sizeof(SphereGrid) * (size_t)cap);
	if (!grown) {
		fprintf(stderr, "dyn_layered_sphere: realloc grids to capacity %d failed\n", cap);
		return false;
	}
	dls->grids = grown;
	dls->grids_capacity = cap;
	return true;
}

static bool dyn_ls_ensure_level_to_grid_slot(DynLayeredSphere *dls, int needed)
{
	if (needed <= dls->level_to_grid_slot_cap)
		return true;
	int old_cap = dls->level_to_grid_slot_cap;
	int cap = dls->level_to_grid_slot_cap ? dls->level_to_grid_slot_cap : 16;
	while (cap < needed)
		cap *= 2;
	int *grown = realloc(dls->level_to_grid_slot, sizeof(int) * (size_t)cap);
	if (!grown) {
		fprintf(stderr, "dyn_layered_sphere: realloc level_to_grid_slot to capacity %d failed\n", cap);
		return false;
	}
	for (int l = old_cap; l < cap; l++)
		grown[l] = -1;
	dls->level_to_grid_slot = grown;
	dls->level_to_grid_slot_cap = cap;
	return true;
}

static bool dyn_ls_ensure_node_to_sphere_capacity(DynLayeredSphere *dls, int needed)
{
	if (needed <= dls->node_to_sphere_cap)
		return true;
	int old_cap = dls->node_to_sphere_cap;
	int cap = dls->node_to_sphere_cap ? dls->node_to_sphere_cap : 16;
	while (cap < needed)
		cap *= 2;
	int *grown = realloc(dls->node_to_sphere, sizeof(int) * (size_t)cap);
	if (!grown) {
		fprintf(stderr, "dyn_layered_sphere: realloc node_to_sphere to capacity %d failed\n", cap);
		return false;
	}
	for (int v = old_cap; v < cap; v++)
		grown[v] = -1;
	dls->node_to_sphere = grown;
	dls->node_to_sphere_cap = cap;
	return true;
}

static bool dyn_ls_rebuild_one_slot(DynLayeredSphere *dls, int slot, int capacity_n, double radius, bool reset_rotation)
{
	SphereGrid *grid = &dls->grids[slot];
	free(grid->slots);
	if (grid->slot_occupant) {
		free(grid->slot_occupant);
		igraph_vector_int_destroy(&grid->neis);
	}
	memset(grid, 0, sizeof(SphereGrid));
	if (!build_sphere_grid(grid, capacity_n, radius, HILBERT_RES))
		return false;
	// Only a genuinely new sphere (its level never had one before) gets a fresh identity
	// orientation. An overflow rebuild is the same conceptual sphere growing its grid, not a new
	// one — resetting rotation there is what caused the visible "snap back to unrotated" jump on
	// every batch-triggered rebuild even though member placement barely changed; keeping the
	// already-converged quaternion/prev_omega/settled state means the newly-seeded occupants (see
	// dyn_ls_reseed_sphere) get placed and then immediately rotated to match, not shown unrotated.
	if (reset_rotation)
		dyn_ls_rotation_reset(dls->sphere_rotation, dls->sphere_prev_omega, dls->sphere_rotation_steps, dls->sphere_settled_streak, slot);
	return true;
}

static bool dyn_ls_handle_node_disjoint_added(DynLayeredSphere *dls, igraph_integer_t node_id, igraph_matrix_t *layout)
{
	if (dls->level_to_grid_slot_cap == 0)
		return false;
	int s = dls->level_to_grid_slot[0];
	if (s < 0 || s >= dls->grid_slot_count || !dls->grids[s].slot_occupant)
		return false;
	SphereGrid *grid = &dls->grids[s];
	const double *quat = dls->sphere_rotation ? &dls->sphere_rotation[4 * s] : NULL;
	for (int slot = grid->max_slots - 1; slot >= 0; slot--) {
		if (grid->slot_occupant[slot] == -1) {
			grid->slot_occupant[slot] = (int)node_id;
			write_slot_position(layout, node_id, &grid->slots[slot], quat);
			dls->node_to_sphere[node_id] = s;
			return true;
		}
	}
	return false;
}

static bool dyn_ls_handle_node_pair_connected(DynLayeredSphere *dls, const DynCoreTree *ct, igraph_integer_t node_id, const igraph_integer_t *community, igraph_integer_t vcount, igraph_matrix_t *layout)
{
	int level = dyn_core_tree_level(ct, dyn_core_tree_node_of(ct, node_id));
	int s = (level >= 0 && level < dls->level_to_grid_slot_cap) ? dls->level_to_grid_slot[level] : -1;
	if (s < 0 || s >= dls->grid_slot_count || !dls->grids[s].slot_occupant)
		return false;

	SphereGrid *grid = &dls->grids[s];
	igraph_integer_t comm_id = community[node_id];

	int anchor = -1;
	for (int k = 0; k < grid->max_slots; k++) {
		int occ = grid->slot_occupant[k];
		if (occ >= 0 && occ < vcount && community[occ] == comm_id) {
			anchor = k;
			break;
		}
	}
	int slot = -1;
	int start = (anchor >= 0) ? anchor : 0;
	for (int k = start; k < grid->max_slots; k++) {
		if (grid->slot_occupant[k] == -1) {
			slot = k;
			break;
		}
	}
	if (slot < 0 && start > 0) {
		for (int k = 0; k < start; k++) {
			if (grid->slot_occupant[k] == -1) {
				slot = k;
				break;
			}
		}
	}
	if (slot < 0)
		return false;
	grid->slot_occupant[slot] = (int)node_id;
	write_slot_position(layout, node_id, &grid->slots[slot], dls->sphere_rotation ? &dls->sphere_rotation[4 * s] : NULL);
	dls->node_to_sphere[node_id] = s;
	return true;
}

static void dyn_ls_refine_connected(const igraph_t *g, LayeredSphereContext *ctx, int s, const NodePlacement *grp, int n_in_group)
{
	for (int i = 0; i < n_in_group; i++) {
		igraph_integer_t v = grp[i].id;
		igraph_integer_t deg;
		if (igraph_degree_1(g, &deg, v, IGRAPH_ALL, IGRAPH_NO_LOOPS) != IGRAPH_SUCCESS || deg == 0)
			continue;
		int current_slot = ctx->node_to_slot_idx[v];
		int target_slot = node_hilbert_target(g, ctx->layout, ctx, (int)v, s, true, 1.0, HILBERT_RES);
		if (target_slot != current_slot) {
			int moves = 0;
			try_move_node(g, ctx->layout, ctx, (int)v, s, target_slot, current_slot, true, &moves);
		}
	}
}

static bool dyn_ls_seed_sphere(const igraph_t *g, LayeredSphereContext *ctx, const DynCoreTree *ct, const DynCoreTreeOrder *order, int s, int target_level, int n_in_group, const igraph_integer_t *community, bool has_timestamp)
{
	NodePlacement *grp = malloc(sizeof(NodePlacement) * (size_t)n_in_group);
	if (!grp) {
		fprintf(stderr, "dyn_layered_sphere: allocation failed\n");
		return false;
	}
	int m = 0;
	int node_count = dyn_core_tree_node_count(ct);
	for (int id = 0; id < node_count; id++) {
		if (dyn_core_tree_level(ct, id) != target_level)
			continue;
		for (igraph_integer_t v = dyn_core_tree_first_member(ct, id); v != -1; v = dyn_core_tree_next_member(ct, v)) {
			grp[m].id = (int)v;
			grp[m].community_id = (int)community[v];
			grp[m].density = order ? dyn_core_tree_order_rank(order, v) : (has_timestamp ? VAN(g, DYN_LS_TIMESTAMP_ATTR, v) : (double)v);
			grp[m].intra_degree = 0;
			m++;
		}
	}
	qsort(grp, (size_t)m, sizeof(NodePlacement), compare_nodes_placement);

	int seed_count = m;
	if (seed_count > ctx->grids[s].max_slots) {
		seed_count = ctx->grids[s].max_slots;
		static int ctr = 0;
		if (++ctr % 16 == 1)
			fprintf(stderr, "dyn_ls[%s]: sphere slot=%d level=%d population %d exceeds grid capacity %d, seeding only the first %d\n", dyn_ls_event_name(DYN_LS_EVENT_SPHERE_RESEEDED), s, target_level, m, ctx->grids[s].max_slots, seed_count);
	}
	seed_slots_for_sphere(ctx, s, grp, seed_count);

	dyn_ls_refine_connected(g, ctx, s, grp, seed_count);
	free(grp);
	return true;
}

static bool dyn_ls_assign_grid_slots_for_ranks(DynLayeredSphere *dls, const int *sphere_to_level, int num_spheres)
{
	for (int rank = 0; rank < num_spheres; rank++) {
		int l = sphere_to_level[rank];
		if (dls->level_to_grid_slot[l] >= 0)
			continue;
		if (!dyn_ls_ensure_grids_capacity(dls, dls->grid_slot_count + 1) || !dyn_ls_rotation_ensure_capacity(&dls->sphere_rotation, &dls->sphere_prev_omega, &dls->sphere_rotation_steps, &dls->sphere_settled_streak, &dls->sphere_rotation_capacity, dls->grid_slot_count + 1)) {
			fprintf(stderr, "dyn_layered_sphere: allocation failed\n");
			return false;
		}
		int slot = dls->grid_slot_count++;
		memset(&dls->grids[slot], 0, sizeof(SphereGrid));
		dls->level_to_grid_slot[l] = slot;
	}
	return true;
}

/* DYN_LS_EVENT_SPHERE_GEOMETRY_DIRTY: a sphere's grid is new/overflowed and needs rebuild/rescale. */
static bool dyn_ls_handle_sphere_geometry_dirty(DynLayeredSphere *dls, const int *sphere_to_level, const int *level_count, int num_spheres, bool *sphere_changed, int *n_built, int *n_overflow, int *n_rescaled)
{
	double prev_radius = 0.0;
	for (int rank = 0; rank < num_spheres; rank++) {
		int l = sphere_to_level[rank];
		int slot = dls->level_to_grid_slot[l];
		int occ = level_count[l];
		double new_radius = sphere_radius_for(rank, occ, prev_radius);
		prev_radius = new_radius;

		SphereGrid *grid = &dls->grids[slot];
		bool is_new = (grid->slots == NULL);
		bool overflow = !is_new && occ > grid->max_slots && grid->max_slots < DYN_LS_MAX_GRID_SLOTS;

		if (is_new || overflow) {
			int capacity_n = (int)((double)occ * (DYN_LS_SLOT_HEADROOM_BASE + rank * DYN_LS_SLOT_HEADROOM_PER_SPHERE));
			if (!dyn_ls_rebuild_one_slot(dls, slot, capacity_n, new_radius, is_new))
				return false;
			sphere_changed[slot] = true;
			if (is_new)
				(*n_built)++;
			else
				(*n_overflow)++;
		} else if (fabs(new_radius - grid->radius) > DYN_LS_RADIUS_EPS) {
			double scale = new_radius / grid->radius;
			for (int k = 0; k < grid->max_slots; k++) {
				grid->slots[k].x *= scale;
				grid->slots[k].y *= scale;
				grid->slots[k].z *= scale;
			}
			grid->radius = new_radius;
			(*n_rescaled)++;
		}
	}
	return true;
}

/* DYN_LS_EVENT_LEVEL_TOUCHED: a coreness level's tree membership changed. */
static void dyn_ls_handle_level_touched(DynLayeredSphere *dls, const igraph_vector_int_t *touched_levels, bool *sphere_changed)
{
	igraph_integer_t n = igraph_vector_int_size(touched_levels);
	for (igraph_integer_t i = 0; i < n; i++) {
		int lvl = (int)VECTOR(*touched_levels)[i];
		if (lvl >= 0 && lvl < dls->level_to_grid_slot_cap && dls->level_to_grid_slot[lvl] >= 0)
			sphere_changed[dls->level_to_grid_slot[lvl]] = true;
	}
}

/* DYN_LS_EVENT_COMMUNITY_REASSIGNED: a vertex's community changed. */
static void dyn_ls_handle_community_reassigned(DynLayeredSphere *dls, const DynCoreTree *ct, const igraph_vector_int_t *community_changed, bool *sphere_changed)
{
	igraph_integer_t n = igraph_vector_int_size(community_changed);
	for (igraph_integer_t i = 0; i < n; i++) {
		igraph_integer_t v = VECTOR(*community_changed)[i];
		int lvl = dyn_core_tree_level(ct, dyn_core_tree_node_of(ct, v));
		if (lvl >= 0 && lvl < dls->level_to_grid_slot_cap && dls->level_to_grid_slot[lvl] >= 0)
			sphere_changed[dls->level_to_grid_slot[lvl]] = true;
	}
}

/* Seed raw positions, then apply the sphere's persistent rotation once. */
static bool dyn_ls_reseed_sphere(const igraph_t *g, LayeredSphereContext *ctx, const DynCoreTree *ct, const DynCoreTreeOrder *order, DynLayeredSphere *dls, int slot, int l, int occ, const igraph_integer_t *community, bool has_timestamp)
{
	bool ok;
	{
		const double *saved_rotation = ctx->sphere_rotation;
		ctx->sphere_rotation = NULL;
		ok = dyn_ls_seed_sphere(g, ctx, ct, order, slot, l, occ, community, has_timestamp);
		ctx->sphere_rotation = saved_rotation;
	}
	if (ok)
		dyn_ls_apply_sphere_rotation(ctx, slot, dls->sphere_rotation);
	return ok;
}

/* DYN_LS_EVENT_SPHERE_UNCHANGED: no dirty event this batch; positions are re-emitted as-is. */
static void dyn_ls_handle_sphere_unchanged(igraph_matrix_t *layout, SphereGrid *grid, const double *quat)
{
	for (int k = 0; k < grid->max_slots; k++) {
		int occupant = grid->slot_occupant[k];
		if (occupant != -1)
			write_slot_position(layout, occupant, &grid->slots[k], quat);
	}
}

static bool dyn_ls_recompute(DynLayeredSphere *dls, const igraph_t *g, const DynCoreTree *ct, const DynCoreTreeOrder *order, const igraph_integer_t *community, igraph_matrix_t *layout, const igraph_vector_int_t *touched_levels, const igraph_vector_int_t *community_changed, bool *out_changed)
{
	igraph_integer_t vcount = igraph_vcount(g);
	if (vcount == 0 || !ct || !community) {
		dyn_ls_free_grid_contents(dls);
		*out_changed = true;
		return true;
	}

	struct timespec t0 = {0}, t1 = {0}, t2 = {0}, t3 = {0}, t3b = {0}, t4 = {0};
	clock_gettime(CLOCK_MONOTONIC_RAW, &t0);

	bool result = false;
	bool *populated = NULL;
	int *level_count = NULL;
	int *sphere_to_level = NULL;
	int *node_to_slot = NULL;
	bool *sphere_changed = NULL;
	int num_spheres = 0;

	int node_count = dyn_core_tree_node_count(ct);
	int max_level = -1;
	for (int id = 0; id < node_count; id++) {
		int lvl = dyn_core_tree_level(ct, id);
		if (lvl > max_level)
			max_level = lvl;
	}
	if (max_level < 0) {
		result = true;
		goto cleanup;
	}
	int level_cap = max_level + 1;

	populated = calloc((size_t)level_cap, sizeof(bool));
	level_count = calloc((size_t)level_cap, sizeof(int));
	if (!populated || !level_count) {
		fprintf(stderr, "dyn_layered_sphere: allocation failed\n");
		goto cleanup;
	}
	for (int id = 0; id < node_count; id++) {
		int lvl = dyn_core_tree_level(ct, id);
		if (lvl < 0)
			continue;
		populated[lvl] = true;
		level_count[lvl] += (int)dyn_core_tree_member_count(ct, id);
	}
	for (int l = 0; l < level_cap; l++)
		if (populated[l])
			num_spheres++;
	if (num_spheres == 0) {
		result = true;
		goto cleanup;
	}

	sphere_to_level = malloc(sizeof(int) * (size_t)num_spheres);
	if (!sphere_to_level) {
		fprintf(stderr, "dyn_layered_sphere: allocation failed\n");
		goto cleanup;
	}
	{
		int rank = 0;
		for (int l = level_cap - 1; l >= 0; l--)
			if (populated[l])
				sphere_to_level[rank++] = l;
	}
	free(populated);
	populated = NULL;
	clock_gettime(CLOCK_MONOTONIC_RAW, &t1);

	if (!dyn_ls_ensure_level_to_grid_slot(dls, level_cap)) {
		fprintf(stderr, "dyn_layered_sphere: allocation failed\n");
		goto cleanup;
	}
	if (!dyn_ls_assign_grid_slots_for_ranks(dls, sphere_to_level, num_spheres)) {
		fprintf(stderr, "dyn_layered_sphere: allocation failed\n");
		goto cleanup;
	}

	sphere_changed = calloc((size_t)dls->grid_slot_count, sizeof(bool));
	if (!sphere_changed) {
		fprintf(stderr, "dyn_layered_sphere: allocation failed\n");
		goto cleanup;
	}

	DynLsEventLog elog = {0};
	if (!dyn_ls_handle_sphere_geometry_dirty(dls, sphere_to_level, level_count, num_spheres, sphere_changed, &elog.sphere_geometry_built, &elog.sphere_geometry_overflow, &elog.sphere_geometry_rescaled)) {
		fprintf(stderr, "dyn_layered_sphere: grid (re)build failed\n");
		goto cleanup;
	}
	clock_gettime(CLOCK_MONOTONIC_RAW, &t2);

	if (touched_levels) {
		elog.level_touched = (int)igraph_vector_int_size(touched_levels);
		dyn_ls_handle_level_touched(dls, touched_levels, sphere_changed);
	}
	if (community_changed) {
		elog.community_reassigned = (int)igraph_vector_int_size(community_changed);
		dyn_ls_handle_community_reassigned(dls, ct, community_changed, sphere_changed);
	}
	for (int slot = 0; slot < dls->grid_slot_count; slot++)
		if (sphere_changed[slot])
			for (int k = 0; k < dls->grids[slot].max_slots; k++)
				dls->grids[slot].slot_occupant[k] = -1;
	clock_gettime(CLOCK_MONOTONIC_RAW, &t3);

	if (!dyn_ls_ensure_node_to_sphere_capacity(dls, (int)vcount)) {
		fprintf(stderr, "dyn_layered_sphere: allocation failed\n");
		goto cleanup;
	}
	node_to_slot = malloc((size_t)vcount * sizeof(int));
	if (!node_to_slot) {
		fprintf(stderr, "dyn_layered_sphere: allocation failed\n");
		goto cleanup;
	}
	for (igraph_integer_t v = 0; v < vcount; v++) {
		dls->node_to_sphere[v] = -1;
		node_to_slot[v] = -1;
	}
	for (int id = 0; id < node_count; id++) {
		int lvl = dyn_core_tree_level(ct, id);
		if (lvl < 0)
			continue;
		int s = dls->level_to_grid_slot[lvl];
		for (igraph_integer_t v = dyn_core_tree_first_member(ct, id); v != -1; v = dyn_core_tree_next_member(ct, v))
			if (v < vcount)
				dls->node_to_sphere[v] = s;
	}

	for (int slot = 0; slot < dls->grid_slot_count; slot++) {
		SphereGrid *grid = &dls->grids[slot];
		if (!grid->slot_occupant || sphere_changed[slot])
			continue;
		for (int k = 0; k < grid->max_slots; k++) {
			int occ = grid->slot_occupant[k];
			if (occ < 0)
				continue;
			if (occ >= vcount || dls->node_to_sphere[occ] != slot) {
				grid->slot_occupant[k] = -1;
				continue;
			}
			node_to_slot[occ] = k;
		}
	}
	int n_reflowed = 0;
	for (int rank = 0; rank < num_spheres; rank++) {
		int l = sphere_to_level[rank];
		int slot = dls->level_to_grid_slot[l];
		if (sphere_changed[slot])
			continue;
		SphereGrid *grid = &dls->grids[slot];
		for (int id = 0; id < node_count; id++) {
			if (dyn_core_tree_level(ct, id) != l)
				continue;
			for (igraph_integer_t v = dyn_core_tree_first_member(ct, id); v != -1; v = dyn_core_tree_next_member(ct, v)) {
				if (v >= vcount || node_to_slot[v] != -1)
					continue;
				int free_slot = -1;
				for (int k = 0; k < grid->max_slots; k++)
					if (grid->slot_occupant[k] == -1) {
						free_slot = k;
						break;
					}
				if (free_slot < 0)
					continue;
				grid->slot_occupant[free_slot] = (int)v;
				node_to_slot[v] = free_slot;
				n_reflowed++;
			}
		}
	}
	clock_gettime(CLOCK_MONOTONIC_RAW, &t3b);

	{
		bool ok = true;
		LayeredSphereContext ctx = {0};
		ctx.grids = dls->grids;
		ctx.node_to_sphere_id = dls->node_to_sphere;
		ctx.node_to_slot_idx = node_to_slot;
		ctx.layout = layout;
		ctx.vcount = (int)vcount;
		ctx.sphere_rotation = dls->sphere_rotation;

		bool has_timestamp = igraph_cattribute_has_attr(g, IGRAPH_ATTRIBUTE_VERTEX, DYN_LS_TIMESTAMP_ATTR);
		for (int rank = 0; rank < num_spheres && ok; rank++) {
			int l = sphere_to_level[rank];
			int slot = dls->level_to_grid_slot[l];
			int occ = level_count[l];
			if (occ > 0 && sphere_changed[slot]) {
				ok = dyn_ls_reseed_sphere(g, &ctx, ct, order, dls, slot, l, occ, community, has_timestamp);
				if (ok)
					elog.sphere_reseeded++;
			} else {
				SphereGrid *grid = &dls->grids[slot];
				const double *quat = dls->sphere_rotation ? &dls->sphere_rotation[4 * slot] : NULL;
				dyn_ls_handle_sphere_unchanged(layout, grid, quat);
				elog.sphere_unchanged++;
			}
		}
		if (!ok)
			goto cleanup;
	}

	result = true;
	if (elog.sphere_reseeded > 0 || elog.sphere_geometry_rescaled > 0 || n_reflowed > 0)
		*out_changed = true;
	clock_gettime(CLOCK_MONOTONIC_RAW, &t4);
	dyn_ls_report_event_log(&elog, num_spheres, dls->grid_slot_count, dyn_ls_timer_us(&t0, &t1), dyn_ls_timer_us(&t1, &t2), dyn_ls_timer_us(&t2, &t3), dyn_ls_timer_us(&t3, &t3b), dyn_ls_timer_us(&t3b, &t4), dyn_ls_timer_us(&t0, &t4));

cleanup:
	free(populated);
	free(level_count);
	free(sphere_to_level);
	free(node_to_slot);
	free(sphere_changed);
	return result;
}

/* Advance populated spheres between graph updates using persisted assignments. */
static bool dyn_ls_tick_rotation(DynLayeredSphere *dls, const igraph_t *g, igraph_matrix_t *layout, igraph_integer_t vcount)
{
	if (!dls->sphere_rotation || !dls->node_to_sphere)
		return false;
	LayeredSphereContext ctx = {0};
	ctx.grids = dls->grids;
	ctx.node_to_sphere_id = dls->node_to_sphere;
	ctx.layout = layout;
	ctx.vcount = (int)vcount;
	ctx.sphere_rotation = dls->sphere_rotation;

	bool any_rotated = false;
	for (int slot = 0; slot < dls->grid_slot_count; slot++) {
		if (!dls->grids[slot].slot_occupant)
			continue;
		if (dyn_ls_rotate_sphere_step(g, &ctx, slot, dls->sphere_rotation, dls->sphere_prev_omega, dls->sphere_rotation_steps, dls->sphere_settled_streak))
			any_rotated = true;
		dyn_ls_apply_sphere_rotation(&ctx, slot, dls->sphere_rotation);
	}
	return any_rotated;
}

/* Fast path for DYN_LS_EVENT_NODE_PAIR_CONNECTED / DYN_LS_EVENT_NODE_DISJOINT_ADDED: placing new
 * vertices locally without a full dyn_ls_recompute, valid only when nothing else in the batch is
 * dirty. Returns false (falling back to dyn_ls_recompute) if any vertex can't be placed locally.
 */
static bool dyn_ls_try_fast_append(DynLayeredSphere *dls, const igraph_t *g, const DynCoreTree *ct, const igraph_integer_t *community, igraph_integer_t old_vcount, igraph_integer_t vcount, igraph_matrix_t *layout)
{
	if (!dyn_ls_ensure_node_to_sphere_capacity(dls, (int)vcount))
		return false;
	int n_connected = 0, n_disjoint = 0;
	for (igraph_integer_t v = old_vcount; v < vcount; v++) {
		DynLsEventType kind = dyn_ls_classify_node_arrival(g, v);
		bool ok = (kind == DYN_LS_EVENT_NODE_PAIR_CONNECTED) ? dyn_ls_handle_node_pair_connected(dls, ct, v, community, vcount, layout) : dyn_ls_handle_node_disjoint_added(dls, v, layout);
		if (!ok)
			return false;
		if (kind == DYN_LS_EVENT_NODE_PAIR_CONNECTED)
			n_connected++;
		else
			n_disjoint++;
	}
	fprintf(stderr, "dyn_ls_on_update: fast-path %s=%d %s=%d\n", dyn_ls_event_name(DYN_LS_EVENT_NODE_PAIR_CONNECTED), n_connected, dyn_ls_event_name(DYN_LS_EVENT_NODE_DISJOINT_ADDED), n_disjoint);
	return true;
}

DynLayeredSphere *dyn_layered_sphere_init(const igraph_t *g, const DynCoreTree *ct, const DynCoreTreeOrder *order, const igraph_integer_t *community, igraph_matrix_t *layout)
{
	DynLayeredSphere *dls = calloc(1, sizeof(DynLayeredSphere));
	if (!dls) {
		fprintf(stderr, "dyn_layered_sphere_init: allocation failed\n");
		return NULL;
	}
	bool init_changed = false;
	if (!dyn_ls_recompute(dls, g, ct, order, community, layout, NULL, NULL, &init_changed) || igraph_step(layout, NULL) != IGRAPH_SUCCESS) {
		dyn_layered_sphere_destroy(dls);
		return NULL;
	}
	dls->last_seen_vcount = igraph_vcount(g);
	return dls;
}

bool dyn_layered_sphere_on_update(DynLayeredSphere *dls, const igraph_t *g, const DynCoreTree *ct, const igraph_vector_int_t *touched_levels, const DynCoreTreeOrder *order, const igraph_integer_t *community, const igraph_vector_int_t *community_changed, igraph_matrix_t *layout, bool *out_changed)
{
	if (!dls)
		return false;

	igraph_integer_t vcount = igraph_vcount(g);
	igraph_integer_t old_vcount = dls->last_seen_vcount;

	bool batch_has_other_dirt = (touched_levels && igraph_vector_int_size(touched_levels) > 0) || (community_changed && igraph_vector_int_size(community_changed) > 0);

	bool ok;
	bool changed = false;
	/* DYN_LS_EVENT_BATCH_NO_OP: no new vertices and no coreness/community dirt at all — layout maintenance is skipped, but the rotation tick below still runs. */
	if (vcount == old_vcount && !batch_has_other_dirt) {
		ok = true;
	} else if (vcount > old_vcount && !batch_has_other_dirt && dyn_ls_try_fast_append(dls, g, ct, community, old_vcount, vcount, layout)) {
		dls->last_seen_vcount = vcount;
		ok = true;
		changed = true;
	} else {
		dls->last_seen_vcount = vcount;
		ok = dyn_ls_recompute(dls, g, ct, order, community, layout, touched_levels, community_changed, &changed);
	}

	if (ok && dyn_ls_tick_rotation(dls, g, layout, vcount))
		changed = true;

	if (out_changed)
		*out_changed = changed;

	return ok && igraph_step(layout, NULL) == IGRAPH_SUCCESS;
}

void dyn_layered_sphere_destroy(DynLayeredSphere *dls)
{
	if (!dls)
		return;
	dyn_ls_free_grid_contents(dls);
	free(dls->grids);
	free(dls->level_to_grid_slot);
	free(dls->node_to_sphere);
	free(dls->sphere_rotation);
	free(dls->sphere_prev_omega);
	free(dls->sphere_rotation_steps);
	free(dls->sphere_settled_streak);
	free(dls);
}
