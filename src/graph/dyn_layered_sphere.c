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
 * Change detection is exact: dyn_core_tree_on_edges' touched_levels output
 * says precisely which spheres' populations moved (radial change), and
 * DynLeiden's community_changed output says which vertices' community label
 * moved without necessarily changing coreness (angular-only change) — both
 * are unioned into a per-sphere dirty flag, and only dirty spheres are
 * cleared and re-seeded; an untouched sphere is provably unchanged as long as
 * no RENUMBERING occurred (see below).
 *
 * A previously-unpopulated level becoming populated (or vice versa) can shift
 * every OTHER already-populated level's sphere rank even though that level's
 * own membership never moved — e.g. populated levels {2,3,7} -> {2,7,8}
 * leaves the sphere COUNT unchanged (3) but shifts level 7 from rank 2 to
 * rank 1. dyn_ls_recompute detects this by comparing the freshly computed
 * level->sphere mapping against the persisted one from last time
 * (dls->level_to_sphere[]) and forces a full rebuild on any mismatch, since a
 * sphere index can then represent a completely different level's population
 * than it did last time.
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
 * The sphere GEOMETRY persists across recomputes: each grid is sized with a
 * large occupancy headroom (DYN_LS_SLOT_HEADROOM_BASE, applied to both the
 * radius and the slot count) when built, and a recompute only re-seeds
 * occupants into the existing slots. Grids are torn down and rebuilt ONLY on
 * sphere overflow or level-rank renumbering (see the `fits` check in
 * dyn_ls_recompute). A batch of arrivals that are ALL still disconnected
 * (degree 0) is appended straight onto the end of the outermost sphere's
 * curve with no recompute at all (dyn_layered_sphere_on_update), and
 * last_seen_vcount tracks which vertices are new arrivals.
 *
 * Simplified Local Buffers (connected-arrival fast path): the shared seeder
 * interleaves each sphere's headroom as gaps across its slots, so a newly
 * arrived vertex that is already connected can often be appended straight
 * into its community's sphere (dyn_ls_try_local_append) without any re-sort
 * or recompute — it just fills one of those gaps. The attempt only fails (and
 * falls through to the full recompute) when the vertex starts a brand-new
 * level, or its sphere has no free slot left. dls->level_to_sphere[] (kept up
 * to date by every recompute) makes the level->sphere lookup O(1) on the fast
 * path — it is always authoritative (the tree cannot go stale), so no
 * fallback scan is needed. Disconnected-only batches take the cheaper
 * dyn_ls_append_disconnected path instead. The fast path does not itself
 * consult touched_levels/community_changed — if every new vertex places
 * locally, the whole call returns early without a full recompute, an
 * accepted heuristic tradeoff favoring speed for the common case over
 * reacting to rarer coreness-ripple side effects on already-existing
 * vertices.
 */

#include "graph/dyn_layered_sphere.h"
#include "graph/layered_sphere_common.h"

#include <igraph_step.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DYN_LS_TIMESTAMP_ATTR "timestamp"
#define DYN_LS_SLOT_HEADROOM_BASE 4.0		// every sphere's radius and slot count are sized for this multiple of its occupancy at build time, so grids absorb growth without resizing
#define DYN_LS_SLOT_HEADROOM_PER_SPHERE 0.5 // extra headroom fraction added on top per sphere index further from the nucleus, so outer spheres end up progressively sparser

// Simple high-resolution timer for performance breakdown logging, in
// microseconds (see the phase timestamps t0..t4 in dyn_ls_recompute).
static inline double dyn_ls_timer_us(const struct timespec *start, const struct timespec *end)
{
	return (double)(end->tv_sec - start->tv_sec) * 1000000.0 + (double)(end->tv_nsec - start->tv_nsec) / 1000.0;
}

struct DynLayeredSphere
{
	SphereGrid *grids;				   // persistent: built with headroom, reused across recomputes, torn down and rebuilt ONLY on sphere overflow or renumbering (see dyn_ls_recompute's fits check)
	int grids_capacity;				   // capacity of grids[]
	int num_spheres;				   // grids currently built (for cleanup/reuse bookkeeping)
	igraph_integer_t last_seen_vcount; // vcount as of the end of the previous on_update/init call (append or recompute) — used to find newly-arrived vertices
	int *level_to_sphere;			   // persistent: level_to_sphere[level] = sphere index that coreness level currently maps to, -1 if that level is unpopulated. Indexed by coreness level (grown as the graph's max coreness grows). Read by the fast-append path for O(1) sphere lookup; compared against the freshly recomputed mapping every recompute to detect rank-shifting renumbering (point 3 in the file header).
	int level_to_sphere_cap;		   // capacity of level_to_sphere[] (levels 0..cap-1 representable)
};

// ============================================================================
// Grid lifecycle
// ============================================================================

// Frees every currently-built grid's contents (not the grids[] array
// itself, which is kept for reuse) — mirrors layered_sphere_cleanup's
// per-grid teardown. Called only on sphere overflow/renumbering (before a
// rebuild) and on destroy, never on an ordinary recompute.
static void dyn_ls_free_grid_contents(DynLayeredSphere *dls)
{
	for (int s = 0; s < dls->num_spheres; s++) {
		free(dls->grids[s].slots);
		if (dls->grids[s].slot_occupant) {
			free(dls->grids[s].slot_occupant);
			igraph_vector_int_destroy(&dls->grids[s].neis);
		}
	}
	dls->num_spheres = 0;
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

// Grows level_to_sphere[] to hold at least `needed` levels (0..needed-1),
// zeroing (as -1, "unpopulated") the newly grown region so a query for a
// level not yet seen by any recompute reads as absent rather than garbage.
static bool dyn_ls_ensure_level_to_sphere(DynLayeredSphere *dls, int needed)
{
	if (needed <= dls->level_to_sphere_cap)
		return true;
	int old_cap = dls->level_to_sphere_cap;
	int cap = dls->level_to_sphere_cap ? dls->level_to_sphere_cap : 16;
	while (cap < needed)
		cap *= 2;
	int *grown = realloc(dls->level_to_sphere, sizeof(int) * (size_t)cap);
	if (!grown) {
		fprintf(stderr, "dyn_layered_sphere: realloc level_to_sphere to capacity %d failed\n", cap);
		return false;
	}
	for (int l = old_cap; l < cap; l++)
		grown[l] = -1;
	dls->level_to_sphere = grown;
	dls->level_to_sphere_cap = cap;
	return true;
}

// Builds all num_spheres grids from the current per-sphere occupancy, each
// sized — radius AND slot count — for DYN_LS_SLOT_HEADROOM_BASE (+ the
// per-sphere extra) times its actual occupancy, so subsequent recomputes and
// appends fit into the existing slots without any resizing. Only called on
// sphere overflow/renumbering; the previous grids must already be freed.
static bool dyn_ls_build_grids(DynLayeredSphere *dls, const int *sphere_count, int num_spheres)
{
	if (!dyn_ls_ensure_grids_capacity(dls, num_spheres))
		return false;
	memset(dls->grids, 0, sizeof(SphereGrid) * (size_t)num_spheres);
	dls->num_spheres = num_spheres; // set now so a mid-loop failure still lets dyn_ls_free_grid_contents clean up safely (build_sphere_grid nulls out a failed grid's own pointers)

	double current_radius = 0.0;
	for (int s = 0; s < num_spheres; s++) {
		if (sphere_count[s] == 0)
			continue; // defensive: a rank with zero occupancy should never occur (every populated level has >=1 member)
		int capacity_n = (int)((double)sphere_count[s] * (DYN_LS_SLOT_HEADROOM_BASE + s * DYN_LS_SLOT_HEADROOM_PER_SPHERE));
		current_radius = sphere_radius_for(s, capacity_n, current_radius);
		if (!build_sphere_grid(&dls->grids[s], capacity_n, current_radius, HILBERT_RES))
			return false;
	}
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
// coreness 0, so the outermost sphere is always the right target. Returns
// false if there's no sphere to append into yet, or the outermost sphere is
// full; either way the caller falls back to a full recompute.
static bool dyn_ls_append_disconnected(DynLayeredSphere *dls, igraph_integer_t node_id, igraph_matrix_t *layout)
{
	if (dls->num_spheres == 0)
		return false;
	SphereGrid *grid = &dls->grids[dls->num_spheres - 1];
	for (int slot = grid->max_slots - 1; slot >= 0; slot--) {
		if (grid->slot_occupant[slot] == -1) {
			grid->slot_occupant[slot] = (int)node_id;
			write_slot_position(layout, node_id, &grid->slots[slot]);
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
	int s = (level >= 0 && level < dls->level_to_sphere_cap) ? dls->level_to_sphere[level] : -1;
	if (s < 0 || s >= dls->num_spheres || !dls->grids[s].slot_occupant)
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
	write_slot_position(layout, node_id, &grid->slots[slot]);
	return true;
}

// ============================================================================
// Recompute stage 1: per-sphere placement
// ============================================================================

// One-shot neighbor refinement for sphere s: each connected node in grp gets
// a single direct move toward its neighbors via the shared
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

// Seeds sphere s (which holds exactly the tree's target_level, since the
// level<->sphere mapping is strictly 1:1) into its (already-built,
// persistent) grid: members are gathered by walking the tree directly
// (O(this level's occupancy), not O(vcount)), ordered via the shared
// compare_nodes_placement (density carries the crossing-reduction rank from
// DynCoreTreeOrder when available, else falls back to timestamp/vertex-id;
// intra_degree is 0 — see the file header), seeded via the shared
// seed_slots_for_sphere, then refined via dyn_ls_refine_connected. Never
// touches the grid's geometry.
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
// tree, map them to spheres, and re-seed the persistent grids (rebuilding
// them ONLY on sphere overflow or renumbering).
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
	int *fresh_level_to_sphere = NULL;
	int *sphere_to_level = NULL;
	int *sphere_count = NULL;
	int *node_to_sphere = NULL;
	int *node_to_slot = NULL;
	bool *sphere_changed = NULL;
	int num_spheres = 0;

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
	if (!populated) {
		fprintf(stderr, "dyn_layered_sphere: allocation failed\n");
		goto cleanup;
	}
	for (int id = 0; id < node_count; id++) {
		int lvl = dyn_core_tree_level(ct, id);
		if (lvl >= 0)
			populated[lvl] = true;
	}
	for (int l = 0; l < level_cap; l++)
		if (populated[l])
			num_spheres++;
	if (num_spheres == 0) {
		result = true;
		goto cleanup;
	}

	// Descending rank: the highest populated level is sphere 0 (the
	// nucleus), level 0 (isolated/coreness-0 vertices, always populated once
	// vcount > 0) is always the last/outermost sphere index.
	fresh_level_to_sphere = malloc(sizeof(int) * (size_t)level_cap);
	sphere_to_level = malloc(sizeof(int) * (size_t)num_spheres);
	if (!fresh_level_to_sphere || !sphere_to_level) {
		fprintf(stderr, "dyn_layered_sphere: allocation failed\n");
		goto cleanup;
	}
	{
		int rank = 0;
		for (int l = level_cap - 1; l >= 0; l--) {
			if (populated[l]) {
				fresh_level_to_sphere[l] = rank;
				sphere_to_level[rank] = l;
				rank++;
			} else {
				fresh_level_to_sphere[l] = -1;
			}
		}
	}
	free(populated);
	populated = NULL;
	clock_gettime(CLOCK_MONOTONIC_RAW, &t1);

	// 2. Per-sphere occupancy: sum of member_count over every tree node at
	// that sphere's level (another O(tree node count) pass).
	sphere_count = calloc((size_t)num_spheres, sizeof(int));
	if (!sphere_count) {
		fprintf(stderr, "dyn_layered_sphere: allocation failed\n");
		goto cleanup;
	}
	for (int id = 0; id < node_count; id++) {
		int lvl = dyn_core_tree_level(ct, id);
		if (lvl < 0)
			continue;
		sphere_count[fresh_level_to_sphere[lvl]] += (int)dyn_core_tree_member_count(ct, id);
	}

	// 3. Renumbering check: compare the freshly computed level->sphere
	// mapping against the persisted one from the last recompute. Coreness is
	// monotonically non-decreasing (insertion-only), so max_level here is
	// always >= any previously seen max_level — a mismatch can only mean a
	// level's rank shifted, never that it vanished from the representable
	// range.
	bool renumbered = false;
	for (int l = 0; l < level_cap && !renumbered; l++) {
		int old_s = (l < dls->level_to_sphere_cap) ? dls->level_to_sphere[l] : -1;
		if (old_s != fresh_level_to_sphere[l])
			renumbered = true;
	}

	// 4. Sphere overflow check: only meaningful when nothing renumbered — the
	// persistent grids are reused as long as every sphere's members still
	// fit in its (headroom-sized) slots and no new sphere is needed.
	bool fits = !renumbered && num_spheres <= dls->num_spheres;
	for (int s = 0; fits && s < num_spheres; s++)
		if (sphere_count[s] > dls->grids[s].max_slots)
			fits = false;
	clock_gettime(CLOCK_MONOTONIC_RAW, &t2);

	sphere_changed = calloc((size_t)num_spheres, sizeof(bool));
	if (!sphere_changed) {
		fprintf(stderr, "dyn_layered_sphere: allocation failed\n");
		goto cleanup;
	}

	bool ok = true;
	if (!fits) {
		fprintf(stderr, "dyn_layered_sphere: %s — rebuilding grids for %d sphere(s) (had %d)\n", renumbered ? "level ranks shifted" : "sphere overflow", num_spheres, dls->num_spheres);
		dyn_ls_free_grid_contents(dls);
		ok = dyn_ls_build_grids(dls, sphere_count, num_spheres);
		for (int s = 0; s < num_spheres; s++)
			sphere_changed[s] = true;
	} else {
		// Exact, precise invalidation: because the tree is exact and no
		// renumbering occurred, an untouched sphere is PROVABLY unchanged —
		// no defensive "clear everything outward" needed.
		if (touched_levels) {
			igraph_integer_t n = igraph_vector_int_size(touched_levels);
			for (igraph_integer_t i = 0; i < n; i++) {
				int lvl = (int)VECTOR(*touched_levels)[i];
				if (lvl >= 0 && lvl < level_cap && fresh_level_to_sphere[lvl] >= 0)
					sphere_changed[fresh_level_to_sphere[lvl]] = true;
			}
		}
		if (community_changed) {
			igraph_integer_t n = igraph_vector_int_size(community_changed);
			for (igraph_integer_t i = 0; i < n; i++) {
				igraph_integer_t v = VECTOR(*community_changed)[i];
				int lvl = dyn_core_tree_level(ct, dyn_core_tree_node_of(ct, v));
				if (lvl >= 0 && lvl < level_cap && fresh_level_to_sphere[lvl] >= 0)
					sphere_changed[fresh_level_to_sphere[lvl]] = true;
			}
		}
		for (int s = 0; s < num_spheres; s++)
			if (sphere_changed[s])
				for (int k = 0; k < dls->grids[s].max_slots; k++)
					dls->grids[s].slot_occupant[k] = -1;
	}

	// Persist the fresh mapping for the fast-append path and next
	// recompute's renumbering check.
	if (!dyn_ls_ensure_level_to_sphere(dls, level_cap)) {
		fprintf(stderr, "dyn_layered_sphere: allocation failed\n");
		goto cleanup;
	}
	for (int l = 0; l < level_cap; l++)
		dls->level_to_sphere[l] = fresh_level_to_sphere[l];
	clock_gettime(CLOCK_MONOTONIC_RAW, &t3);

	// Debug: one-line summary of optimization decisions (throttled to avoid
	// flooding stderr on every poll).
	{
		static int log_counter = 0;
		if (++log_counter % 32 == 1) {
			int to_seed = 0;
			for (int s = 0; s < num_spheres; s++)
				if (sphere_count[s] > 0 && sphere_changed[s])
					to_seed++;
			fprintf(stderr, "dyn_ls: recompute spheres=%d/%d fits=%d renumbered=%d seed=%d\n", num_spheres, dls->num_spheres, fits, renumbered, to_seed);
		}
	}

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
		int s = fresh_level_to_sphere[lvl];
		for (igraph_integer_t v = dyn_core_tree_first_member(ct, id); v != -1; v = dyn_core_tree_next_member(ct, v))
			if (v < vcount)
				node_to_sphere[v] = s;
	}

	if (ok) {
		LayeredSphereContext ctx = {0};
		ctx.grids = dls->grids;
		ctx.node_to_sphere_id = node_to_sphere;
		ctx.node_to_slot_idx = node_to_slot;
		ctx.layout = layout;
		ctx.vcount = (int)vcount;

		bool has_timestamp = igraph_cattribute_has_attr(g, IGRAPH_ATTRIBUTE_VERTEX, DYN_LS_TIMESTAMP_ATTR);
		for (int s = 0; s < num_spheres && ok; s++) {
			if (sphere_count[s] == 0 || !sphere_changed[s])
				continue;
			ok = dyn_ls_seed_sphere(g, &ctx, ct, order, s, sphere_to_level[s], sphere_count[s], community, has_timestamp);
		}
	}

	if (!ok)
		goto cleanup; // result stays false

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
	free(fresh_level_to_sphere);
	free(sphere_to_level);
	free(sphere_count);
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
	// we fall through to the full recompute.
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
	free(dls->level_to_sphere);
	free(dls);
}
