/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Dynamic (streaming) Layered Sphere layout maintenance, insertion-only.
 *
 * Shares its per-sphere placement primitives (sphere_radius_for,
 * build_sphere_grid, compare_nodes_placement, seed_slots_for_sphere) with
 * src/graph/layered_sphere.c via layered_sphere_common.c.
 *
 * Sphere assignment is read directly from a DynCoreTree (dyn_core_tree.h):
 * every currently-populated coreness LEVEL gets exactly one sphere (strict
 * 1:1), ranked DESCENDING — the highest populated level is sphere 0 (the
 * nucleus), level 0 (the tree root: coreness-0/isolated vertices) is always
 * the outermost sphere. Multiple disjoint same-level tree nodes (unconnected
 * components at the same coreness) still share one sphere, preserving
 * "shell = degree of centrality" rather than a connectivity-based grouping.
 * Community membership is read live from DynLeiden (O(1) per-vertex lookups)
 * and is the intra-shell grouping/ordering key, not the shell-assignment key.
 *
 * Storage is keyed by LEVEL, not by rank. dls->level_to_grid_slot[level] maps
 * a coreness level to a stable grid-array index ("grid slot"), assigned once
 * the first time that level is ever populated and never reassigned — not
 * even if the level later becomes unpopulated (see "Idle slots" below). Rank
 * (0 = nucleus, increasing outward) is recomputed fresh every call purely to
 * walk spheres nucleus-outward for sphere_radius_for's prev_radius
 * recurrence; it is never used as a storage index. This decoupling is what
 * lets dyn_ls_recompute react to a level's rank changing (because some OTHER
 * level became/stopped being populated, shifting everyone outward of it)
 * without touching any sphere whose own membership didn't change: a rank
 * shift only ever changes a sphere's RADIUS, and since a SphereGrid's
 * slots[k].{x,y,z} are simply radius * unit_direction(k) (compute_slot_point,
 * layered_sphere_common.c) with the unit direction — and therefore Hilbert
 * order and slot_occupant[] assignment — independent of radius, a pure rank
 * shift is handled as an in-place O(occupancy) coordinate rescale: no grid
 * teardown, no reseed, no Hilbert resort, no rotation reset (rotation and
 * uniform scaling commute, since write_slot_position rotates AFTER reading
 * slots[k]).
 *
 * Idle slots: vertices/edges are never deleted (insertion-only), but a
 * level's last tree node CAN still become empty and be removed — e.g. a
 * coreness lift reparents a vertex away, emptying its old tree node
 * (dyn_core_tree.c). When that happens the level simply drops out of the
 * "populated" set on the next recompute; nothing below ever visits an
 * unpopulated level's grid slot (the radius walk and dirty-detection both
 * iterate populated levels only), so its SphereGrid just sits allocated and
 * untouched. If that same level value is ever populated again later (by
 * different vertices), it is picked up by the ordinary "slot already exists"
 * path below — including the overflow check, which rebuilds it if the new
 * membership no longer fits, and the dirty-reseed path, which already clears
 * slot_occupant[] before reseeding regardless of what was left over. No
 * explicit free/reuse bookkeeping is needed. dls->grid_slot_count (the
 * number of distinct levels ever populated) is bounded by the graph's
 * maximum coreness ever reached, not by stream length.
 *
 * Change detection is exact: dyn_core_tree_on_edges' touched_levels output
 * says precisely which spheres' populations moved (radial change), and
 * DynLeiden's community_changed output says which vertices' community label
 * moved without necessarily changing coreness (angular-only change) — both
 * are unioned into a per-slot dirty flag, and only dirty slots are cleared
 * and re-seeded; an untouched, non-dirty slot is provably unchanged.
 *
 * Within a sphere, members are grouped by community and ordered by the
 * crossing-reduction rank maintained by DynCoreTreeOrder
 * (dyn_core_tree_order.h) when available, falling back to arrival timestamp
 * (or vertex id if the "timestamp" attribute is absent) otherwise. Each
 * connected node then gets a single one-shot refinement move toward its
 * neighbors (dyn_ls_refine_connected). The ordering key rides in
 * NodePlacement.density (with intra_degree pinned to 0) so the shared
 * compare_nodes_placement comparator and seed_slots_for_sphere seeder apply
 * unchanged — with all intra_degree equal, that comparator reduces exactly
 * to (community asc, density asc).
 *
 * Each sphere also carries a persistent rigid rotation (a unit quaternion),
 * updated once per recompute for every dirty sphere with inter-sphere edges,
 * via dyn_ls_rotate_sphere_step (graph/dyn_ls_sphere_rotation.h): the net
 * torque of the sphere's inter-sphere edges is turned into a damped,
 * clamped rotation increment composed onto the sphere's quaternion, then
 * dyn_ls_apply_sphere_rotation re-writes that sphere's occupants' layout
 * positions under the updated quaternion. Rotation is a pure coordinate
 * transform applied at write_slot_position time (layered_sphere_common.h)
 * — it never touches SphereGrid slot/occupancy geometry or Hilbert order,
 * so rotating a sphere never triggers a reseed or grid rebuild. Rotation
 * state is indexed by grid slot (like everything else here), so it survives
 * a pure rank shift undisturbed.
 *
 * The sphere GEOMETRY persists across recomputes: each newly-built grid is
 * sized with a large occupancy headroom (DYN_LS_SLOT_HEADROOM_BASE, applied
 * to both the radius and the slot count), and an ordinary recompute only
 * re-seeds occupants into the existing slots or rescales them in place. A
 * grid is torn down and rebuilt only when its own occupancy exceeds its
 * headroom (overflow) or the first time its level is populated. A batch of
 * arrivals that are ALL still disconnected (degree 0) is appended straight
 * onto the end of the outermost sphere's curve with no recompute at all
 * (dyn_layered_sphere_on_update), and last_seen_vcount tracks which vertices
 * are new arrivals.
 *
 * Simplified Local Buffers (connected-arrival fast path): the shared seeder
 * interleaves each sphere's headroom as gaps across its slots, so a newly
 * arrived vertex that is already connected can often be appended straight
 * into its community's sphere (dyn_ls_try_local_append) without any re-sort
 * or recompute — it just fills one of those gaps. The attempt only fails (and
 * falls through to the full recompute) when the vertex starts a brand-new
 * level, or its sphere has no free slot left. dls->level_to_grid_slot[] (kept
 * up to date by every recompute) makes the level->sphere lookup O(1) on the
 * fast path — it is always authoritative (the tree cannot go stale), so no
 * fallback scan is needed. Disconnected-only batches take the cheaper
 * dyn_ls_append_disconnected path instead. The fast path does not itself
 * consult touched_levels/community_changed — if every new vertex places
 * locally, the whole call returns early without a full recompute, an
 * accepted heuristic tradeoff favoring speed for the common case over
 * reacting to rarer coreness-ripple side effects on already-existing
 * vertices.
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
#define DYN_LS_SLOT_HEADROOM_BASE 4.0		// every sphere's radius and slot count are sized for this multiple of its occupancy at build time, so grids absorb growth without resizing
#define DYN_LS_SLOT_HEADROOM_PER_SPHERE 0.5 // extra headroom fraction added on top per rank further from the nucleus, so outer spheres end up progressively sparser
#define DYN_LS_RADIUS_EPS 1e-9				 // below this, a freshly-computed radius is treated as "unchanged" (sphere_radius_for is a deterministic function of unchanged inputs, so this only guards against incidental floating-point noise)

// Simple high-resolution timer for performance breakdown logging, in
// microseconds (see the phase timestamps t0..t4 in dyn_ls_recompute).
static inline double dyn_ls_timer_us(const struct timespec *start, const struct timespec *end)
{
	return (double)(end->tv_sec - start->tv_sec) * 1000000.0 + (double)(end->tv_nsec - start->tv_nsec) / 1000.0;
}

struct DynLayeredSphere
{
	SphereGrid *grids;				   // persistent, indexed by GRID SLOT (stable per level, not rank — see file header): built with headroom, reused across recomputes, torn down and rebuilt only on that one slot's overflow or first population
	int grids_capacity;				   // capacity of grids[]
	int grid_slot_count;			   // number of distinct levels ever populated over this graph's lifetime (== live extent of grids[]/rotation arrays); only grows, never shrinks (see "Idle slots" in the file header)
	igraph_integer_t last_seen_vcount; // vcount as of the end of the previous on_update/init call (append or recompute) — used to find newly-arrived vertices
	int *level_to_grid_slot;		   // persistent: level_to_grid_slot[level] = grid slot index that coreness level maps to, -1 if that level has never been populated. Indexed by coreness level (grown as the graph's max coreness grows). Read by the fast-append path for O(1) sphere lookup. Once set for a level it is NEVER cleared, even if that level later becomes unpopulated (see file header).
	int level_to_grid_slot_cap;	   // capacity of level_to_grid_slot[] (levels 0..cap-1 representable)
	double *sphere_rotation;		   // persistent, indexed by GRID SLOT: 4 doubles/slot (w,x,y,z unit quaternion), identity when unrotated — see graph/dyn_ls_sphere_rotation.h
	double *sphere_prev_omega;		   // persistent, indexed by GRID SLOT: 3 doubles/slot (previous rotation-step direction), for oscillation damping
	int *sphere_rotation_steps;	   // persistent, indexed by GRID SLOT: per-slot count of successfully applied rotation steps, for the damping schedule
	int sphere_rotation_capacity;	   // capacity of the three rotation arrays above, in SLOTS (not doubles)
};

// ============================================================================
// Grid lifecycle
// ============================================================================

// Frees the contents of every grid slot ever allocated (not the grids[]
// array itself, which is kept for reuse), resetting grid_slot_count to 0 —
// mirrors layered_sphere_cleanup's per-grid teardown. Called only on
// destroy and on "graph went empty" reset; never on an ordinary recompute
// (see file header — a normal recompute only ever touches ONE slot's
// contents at a time, via dyn_ls_rebuild_one_slot below).
static void dyn_ls_free_grid_contents(DynLayeredSphere *dls)
{
	for (int s = 0; s < dls->grid_slot_count; s++) {
		free(dls->grids[s].slots);
		if (dls->grids[s].slot_occupant) {
			free(dls->grids[s].slot_occupant);
			igraph_vector_int_destroy(&dls->grids[s].neis);
		}
	}
	dls->grid_slot_count = 0;
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

// Grows level_to_grid_slot[] to hold at least `needed` levels (0..needed-1),
// zeroing (as -1, "never populated") the newly grown region so a query for a
// level not yet seen by any recompute reads as absent rather than garbage.
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

// Frees slot `s`'s current grid contents (if any — a no-op for a slot that
// was only just allocated this call, i.e. is_new) and rebuilds it at
// `capacity_n`/`radius`, resetting the slot's rotation state. Used for both
// "level populated for the first time" and "existing slot overflowed" —
// the two cases where a slot's own geometry (not just its radius) must
// change.
static bool dyn_ls_rebuild_one_slot(DynLayeredSphere *dls, int slot, int capacity_n, double radius)
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
	dyn_ls_rotation_reset(dls->sphere_rotation, dls->sphere_prev_omega, dls->sphere_rotation_steps, slot);
	return true;
}

// ============================================================================
// Disconnected-arrival append (the only path that skips the full recompute)
// ============================================================================

// Places a newly-arrived, disconnected vertex directly into the outermost
// sphere's next free slot — "the end of the curve" — without touching any
// other vertex's position: no re-sort, no recompute. Correct only because
// level 0 (coreness-0/isolated vertices) always ranks last (see the file
// header's descending-rank point): a fresh disconnected vertex is always
// coreness 0, so the outermost sphere is always the right target — found by
// looking up level 0's grid slot directly (level 0 is always populated once
// vcount > 0), NOT by assuming it's the highest array index (grids are no
// longer stored in rank order — see file header). Returns false if level 0
// has no slot built yet, or that sphere is full; either way the caller
// falls back to a full recompute.
static bool dyn_ls_append_disconnected(DynLayeredSphere *dls, igraph_integer_t node_id, igraph_matrix_t *layout)
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
			return true;
		}
	}
	return false;
}

// ============================================================================
// Connected-arrival fast path (local-buffer append)
// ============================================================================

// Attempts to place a newly-arrived, connected vertex into the persistent
// grid that already holds its coreness level, in a free (-1) slot near that
// level's same-community members — no re-sort, no recompute. This is the
// "Simplified Local Buffers" fast path: headroom is already interleaved
// across every sphere's slots by the shared seeder, so an in-level arrival
// just fills one of those gaps.
//
// Returns false (and the caller falls through to a full recompute) when:
//   - the vertex's level has no sphere yet (a brand-new level), or
//   - that sphere has no free slot left (local/overall overflow).
// Only ever writes a free slot, so placement stays collision-free.
static bool dyn_ls_try_local_append(DynLayeredSphere *dls, const DynCoreTree *ct, igraph_integer_t node_id, const igraph_integer_t *community, igraph_integer_t vcount, igraph_matrix_t *layout)
{
	int level = dyn_core_tree_level(ct, dyn_core_tree_node_of(ct, node_id));
	int s = (level >= 0 && level < dls->level_to_grid_slot_cap) ? dls->level_to_grid_slot[level] : -1;
	if (s < 0 || s >= dls->grid_slot_count || !dls->grids[s].slot_occupant)
		return false; // brand-new level with no sphere yet -> fall through to recompute

	SphereGrid *grid = &dls->grids[s];
	igraph_integer_t comm_id = community[node_id];

	// Find a free slot near the vertex's community on this sphere. First,
	// locate any occupant of the same community to use as an anchor, then
	// scan forward from its slot (wrapping around) for the nearest gap. This
	// preserves visual grouping: the new vertex lands in its community's own
	// headroom band rather than stealing the first community's gap.
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
		return false; // sphere full — local overflow
	grid->slot_occupant[slot] = (int)node_id;
	write_slot_position(layout, node_id, &grid->slots[slot], dls->sphere_rotation ? &dls->sphere_rotation[4 * s] : NULL);
	return true;
}

// ============================================================================
// Recompute stage 1: per-sphere placement
// ============================================================================

// One-shot neighbor refinement for sphere (grid slot) s: each connected node
// in grp gets a single direct move toward its neighbors via the shared
// node_hilbert_target/try_move_node pair (no damping schedule, no iteration).
// Disconnected nodes are skipped outright. Takes the already-gathered
// membership list from dyn_ls_seed_sphere rather than re-scanning for it.
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

// Seeds sphere (grid slot) s, which holds exactly the tree's target_level
// (the level<->slot mapping is strictly 1:1), into its (already-built,
// persistent) grid: members are gathered by walking the tree directly
// (O(this level's occupancy), not O(vcount)), ordered via the shared
// compare_nodes_placement (density carries the crossing-reduction rank from
// DynCoreTreeOrder when available, else falls back to timestamp/vertex-id;
// intra_degree is 0 — see the file header), seeded via the shared
// seed_slots_for_sphere, then refined via dyn_ls_refine_connected. Never
// touches the grid's geometry (radius/slot count).
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
	seed_slots_for_sphere(ctx, s, grp, m);

	dyn_ls_refine_connected(g, ctx, s, grp, m);
	free(grp);
	return true;
}

// ============================================================================
// Full recompute: orchestration only — enumerate populated levels from the
// tree, assign/reuse each one's stable grid slot, walk them nucleus-outward
// to (re)compute radii, and re-seed only the slots that actually need it.
// ============================================================================

static bool dyn_ls_recompute(DynLayeredSphere *dls, const igraph_t *g, const DynCoreTree *ct, const DynCoreTreeOrder *order, const igraph_integer_t *community, igraph_matrix_t *layout, const igraph_vector_int_t *touched_levels, const igraph_vector_int_t *community_changed)
{
	igraph_integer_t vcount = igraph_vcount(g);
	if (vcount == 0 || !ct || !community) {
		dyn_ls_free_grid_contents(dls); // empty graph / maintainers gone = reset
		return true;
	}

	struct timespec t0 = {0}, t1 = {0}, t2 = {0}, t3 = {0}, t4 = {0};
	clock_gettime(CLOCK_MONOTONIC_RAW, &t0);

	// Every heap allocation below is freed exactly once at `cleanup`, whether
	// this call succeeds or bails out early — declare and zero-init them all
	// up front so `goto cleanup` from any failure point is safe (free(NULL)
	// is a no-op) instead of each failure branch hand-listing its own subset.
	bool result = false;
	bool *populated = NULL;
	int *level_count = NULL;	// occupancy per LEVEL (0..level_cap-1), unlike sphere_to_level this is stable regardless of rank
	int *sphere_to_level = NULL; // rank -> level, transient, used only to walk the radius recurrence in nucleus-outward order
	int *node_to_sphere = NULL;
	int *node_to_slot = NULL;
	bool *sphere_changed = NULL; // indexed by GRID SLOT, sized to dls->grid_slot_count AFTER slot assignment below
	int num_spheres = 0;		  // count of currently populated levels (== count of ranks this call)

	// 1. Enumerate populated coreness levels directly from the tree (O(tree
	// node count), not O(V)): every alive node's level is a populated level.
	int node_count = dyn_core_tree_node_count(ct);
	int max_level = -1;
	for (int id = 0; id < node_count; id++) {
		int lvl = dyn_core_tree_level(ct, id);
		if (lvl > max_level)
			max_level = lvl;
	}
	if (max_level < 0) {
		// No alive nodes at all — shouldn't happen once vcount > 0 (the root
		// is always alive), but nothing to place either way.
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

	// Descending rank, nucleus-outward walk order (transient — see file
	// header: rank is never stored, only used to thread sphere_radius_for's
	// prev_radius recurrence below).
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

	// 2. Slot assignment: ensure every currently-populated level has a grid
	// slot, allocating a fresh one only for a level populated for the first
	// time ever (see file header — an existing slot, even an idle one from a
	// since-depopulated level, is always reused, never reassigned).
	if (!dyn_ls_ensure_level_to_grid_slot(dls, level_cap)) {
		fprintf(stderr, "dyn_layered_sphere: allocation failed\n");
		goto cleanup;
	}
	for (int rank = 0; rank < num_spheres; rank++) {
		int l = sphere_to_level[rank];
		if (dls->level_to_grid_slot[l] >= 0)
			continue; // already has a slot (whether built this call or reused from before)
		if (!dyn_ls_ensure_grids_capacity(dls, dls->grid_slot_count + 1) ||
			!dyn_ls_rotation_ensure_capacity(&dls->sphere_rotation, &dls->sphere_prev_omega, &dls->sphere_rotation_steps, &dls->sphere_rotation_capacity, dls->grid_slot_count + 1)) {
			fprintf(stderr, "dyn_layered_sphere: allocation failed\n");
			goto cleanup;
		}
		int slot = dls->grid_slot_count++;
		memset(&dls->grids[slot], 0, sizeof(SphereGrid));
		dls->level_to_grid_slot[l] = slot;
	}

	sphere_changed = calloc((size_t)dls->grid_slot_count, sizeof(bool));
	if (!sphere_changed) {
		fprintf(stderr, "dyn_layered_sphere: allocation failed\n");
		goto cleanup;
	}

	// 3. Radius walk, nucleus outward: reproduces the exact same radii as
	// before (same formula, same inputs), but now DECIDES per slot whether
	// that requires a full rebuild (new slot / overflow) or just an in-place
	// rescale (pure or partial rank shift) — see file header for why a
	// rescale is safe to do without touching slot_occupant[]/rotation.
	{
		bool ok = true;
		double prev_radius = 0.0;
		int n_built = 0, n_overflow = 0, n_rescaled = 0;
		for (int rank = 0; rank < num_spheres && ok; rank++) {
			int l = sphere_to_level[rank];
			int slot = dls->level_to_grid_slot[l];
			int occ = level_count[l];
			double new_radius = sphere_radius_for(rank, occ, prev_radius);
			prev_radius = new_radius;

			SphereGrid *grid = &dls->grids[slot];
			bool is_new = (grid->slots == NULL);
			bool overflow = !is_new && occ > grid->max_slots;

			if (is_new || overflow) {
				int capacity_n = (int)((double)occ * (DYN_LS_SLOT_HEADROOM_BASE + rank * DYN_LS_SLOT_HEADROOM_PER_SPHERE));
				ok = dyn_ls_rebuild_one_slot(dls, slot, capacity_n, new_radius);
				sphere_changed[slot] = true;
				if (is_new)
					n_built++;
				else
					n_overflow++;
			} else if (fabs(new_radius - grid->radius) > DYN_LS_RADIUS_EPS) {
				double scale = new_radius / grid->radius;
				for (int k = 0; k < grid->max_slots; k++) {
					grid->slots[k].x *= scale;
					grid->slots[k].y *= scale;
					grid->slots[k].z *= scale;
				}
				grid->radius = new_radius;
				n_rescaled++;
			}
		}
		if (!ok) {
			fprintf(stderr, "dyn_layered_sphere: grid (re)build failed\n");
			goto cleanup;
		}
		clock_gettime(CLOCK_MONOTONIC_RAW, &t2);

		// 4. Exact, precise dirty-slot detection: because the tree is exact,
		// a slot untouched by touched_levels/community_changed AND whose
		// radius didn't change above is PROVABLY unchanged — no defensive
		// "mark everything dirty" needed.
		if (touched_levels) {
			igraph_integer_t n = igraph_vector_int_size(touched_levels);
			for (igraph_integer_t i = 0; i < n; i++) {
				int lvl = (int)VECTOR(*touched_levels)[i];
				if (lvl >= 0 && lvl < dls->level_to_grid_slot_cap && dls->level_to_grid_slot[lvl] >= 0)
					sphere_changed[dls->level_to_grid_slot[lvl]] = true;
			}
		}
		if (community_changed) {
			igraph_integer_t n = igraph_vector_int_size(community_changed);
			for (igraph_integer_t i = 0; i < n; i++) {
				igraph_integer_t v = VECTOR(*community_changed)[i];
				int lvl = dyn_core_tree_level(ct, dyn_core_tree_node_of(ct, v));
				if (lvl >= 0 && lvl < dls->level_to_grid_slot_cap && dls->level_to_grid_slot[lvl] >= 0)
					sphere_changed[dls->level_to_grid_slot[lvl]] = true;
			}
		}
		for (int slot = 0; slot < dls->grid_slot_count; slot++)
			if (sphere_changed[slot])
				for (int k = 0; k < dls->grids[slot].max_slots; k++)
					dls->grids[slot].slot_occupant[k] = -1;

		// Debug: one-line summary of optimization decisions (throttled to
		// avoid flooding stderr on every poll).
		{
			static int log_counter = 0;
			if (++log_counter % 32 == 1)
				fprintf(stderr, "dyn_ls: recompute spheres=%d slots=%d built=%d overflow=%d rescaled=%d\n", num_spheres, dls->grid_slot_count, n_built, n_overflow, n_rescaled);
		}
	}
	clock_gettime(CLOCK_MONOTONIC_RAW, &t3);

	// Node -> sphere map plus per-slot map, one O(V)-sized array populated by
	// walking the (much smaller) tree. Still touches every vertex once —
	// required by the shared LayeredSphereContext API, which
	// node_hilbert_target/try_move_node use to look up ANY neighbor's sphere
	// during refinement, not just same-sphere ones.
	node_to_sphere = malloc((size_t)vcount * sizeof(int));
	node_to_slot = malloc((size_t)vcount * sizeof(int));
	if (!node_to_sphere || !node_to_slot) {
		fprintf(stderr, "dyn_layered_sphere: allocation failed\n");
		goto cleanup;
	}
	for (igraph_integer_t v = 0; v < vcount; v++)
		node_to_sphere[v] = -1;
	for (int id = 0; id < node_count; id++) {
		int lvl = dyn_core_tree_level(ct, id);
		if (lvl < 0)
			continue;
		int s = dls->level_to_grid_slot[lvl];
		for (igraph_integer_t v = dyn_core_tree_first_member(ct, id); v != -1; v = dyn_core_tree_next_member(ct, v))
			if (v < vcount)
				node_to_sphere[v] = s;
	}

	{
		bool ok = true;
		LayeredSphereContext ctx = {0};
		ctx.grids = dls->grids;
		ctx.node_to_sphere_id = node_to_sphere;
		ctx.node_to_slot_idx = node_to_slot;
		ctx.layout = layout;
		ctx.vcount = (int)vcount;
		ctx.sphere_rotation = dls->sphere_rotation; // seeds vertices already under each sphere's persisted rotation, avoiding a pop-then-correct jitter

		bool has_timestamp = igraph_cattribute_has_attr(g, IGRAPH_ATTRIBUTE_VERTEX, DYN_LS_TIMESTAMP_ATTR);
		for (int rank = 0; rank < num_spheres && ok; rank++) {
			int l = sphere_to_level[rank];
			int slot = dls->level_to_grid_slot[l];
			int occ = level_count[l];
			if (occ == 0)
				continue;
			if (sphere_changed[slot]) {
				ok = dyn_ls_seed_sphere(g, &ctx, ct, order, slot, l, occ, community, has_timestamp);
				if (ok) {
					// TopoLayout-style torque rotation, adapted to 3D: rotate
					// sphere `slot` as a rigid body to reduce its
					// inter-sphere edge stress, then re-apply the (possibly
					// just-updated) quaternion to its freshly-seeded
					// occupants.
					dyn_ls_rotate_sphere_step(g, &ctx, slot, dls->sphere_rotation, dls->sphere_prev_omega, dls->sphere_rotation_steps);
					dyn_ls_apply_sphere_rotation(&ctx, slot, dls->sphere_rotation);
				}
			} else {
				// Not dirty, but its radius may have changed in the walk
				// above (a pure rank shift) — rewrite occupants' positions
				// under the SAME (unreset) rotation quaternion, no reseed.
				SphereGrid *grid = &dls->grids[slot];
				const double *quat = dls->sphere_rotation ? &dls->sphere_rotation[4 * slot] : NULL;
				for (int k = 0; k < grid->max_slots; k++) {
					int occupant = grid->slot_occupant[k];
					if (occupant != -1)
						write_slot_position(layout, occupant, &grid->slots[k], quat);
				}
			}
		}
		if (!ok)
			goto cleanup; // result stays false
	}

	result = igraph_step(layout, NULL) == IGRAPH_SUCCESS;
	clock_gettime(CLOCK_MONOTONIC_RAW, &t4);
	{
		static int log_counter = 0;
		if (++log_counter % 32 == 1) {
			double us_levels = dyn_ls_timer_us(&t0, &t1);
			double us_occ = dyn_ls_timer_us(&t1, &t2);
			double us_geom = dyn_ls_timer_us(&t2, &t3);
			double us_seed = dyn_ls_timer_us(&t3, &t4);
			double us_tot = dyn_ls_timer_us(&t0, &t4);
			fprintf(stderr, "dyn_ls_t: levels=%.0f occ=%.0f geom=%.0f seed=%.0f tot=%.0f us\n", us_levels, us_occ, us_geom, us_seed, us_tot);
		}
	}

cleanup:
	free(populated);
	free(level_count);
	free(sphere_to_level);
	free(node_to_sphere);
	free(node_to_slot);
	free(sphere_changed);
	return result;
}

// ============================================================================
// Public API
// ============================================================================

DynLayeredSphere *dyn_layered_sphere_init(const igraph_t *g, const DynCoreTree *ct, const DynCoreTreeOrder *order, const igraph_integer_t *community, igraph_matrix_t *layout)
{
	DynLayeredSphere *dls = calloc(1, sizeof(DynLayeredSphere));
	if (!dls) {
		fprintf(stderr, "dyn_layered_sphere_init: allocation failed\n");
		return NULL;
	}
	if (!dyn_ls_recompute(dls, g, ct, order, community, layout, NULL, NULL)) {
		dyn_layered_sphere_destroy(dls);
		return NULL;
	}
	dls->last_seen_vcount = igraph_vcount(g);
	return dls;
}

bool dyn_layered_sphere_on_update(DynLayeredSphere *dls, const igraph_t *g, const DynCoreTree *ct, const igraph_vector_int_t *touched_levels, const DynCoreTreeOrder *order, const igraph_integer_t *community, const igraph_vector_int_t *community_changed, igraph_matrix_t *layout)
{
	if (!dls)
		return false;

	igraph_integer_t vcount = igraph_vcount(g);
	igraph_integer_t old_vcount = dls->last_seen_vcount;

	// Connected-arrival fast path (Simplified Local Buffers): each newly
	// arrived vertex is appended into its level's sphere (connected) or the
	// outermost sphere's tail (disconnected) with no re-sort. If any
	// vertex's append fails (new level, full sphere, no sphere built yet),
	// we fall through to the full recompute. Sphere rotation is deliberately
	// NOT recomputed here — dyn_ls_rotate_sphere_step's cost scales with a
	// whole sphere's inter-sphere edge count, not with the arrival batch
	// size, so running it on every fast append would regress this path's
	// near-O(1) guarantee; each append still writes under its sphere's
	// CURRENT rotation (see dyn_ls_append_disconnected/dyn_ls_try_local_append)
	// so a fast-appended node never visibly pops relative to its sphere-mates.
	if (vcount > old_vcount) {
		bool all_local = true;
		for (igraph_integer_t v = old_vcount; v < vcount && all_local; v++) {
			igraph_integer_t deg;
			if (igraph_degree_1(g, &deg, v, IGRAPH_ALL, IGRAPH_NO_LOOPS) != IGRAPH_SUCCESS)
				deg = 0;
			if (deg > 0) {
				if (!dyn_ls_try_local_append(dls, ct, v, community, vcount, layout))
					all_local = false;
			} else {
				if (!dyn_ls_append_disconnected(dls, v, layout))
					all_local = false;
			}
		}
		if (all_local) {
			fprintf(stderr, "dyn_ls: fast local-append %lld new vertices\n", (long long)(vcount - old_vcount));
			dls->last_seen_vcount = vcount;
			return igraph_step(layout, NULL) == IGRAPH_SUCCESS;
		}
		// Some vertex's append failed (new level, full sphere, no sphere
		// built yet) — some vertices in this batch may already be placed, so
		// fall through to a full recompute rather than retrying the
		// disconnected-only path, which would double-place them.
	}

	dls->last_seen_vcount = vcount;
	return dyn_ls_recompute(dls, g, ct, order, community, layout, touched_levels, community_changed);
}

void dyn_layered_sphere_destroy(DynLayeredSphere *dls)
{
	if (!dls)
		return;
	dyn_ls_free_grid_contents(dls);
	free(dls->grids);
	free(dls->level_to_grid_slot);
	free(dls->sphere_rotation);
	free(dls->sphere_prev_omega);
	free(dls->sphere_rotation_steps);
	free(dls);
}
