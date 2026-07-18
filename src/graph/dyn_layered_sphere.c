/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Dynamic (streaming) Layered Sphere layout maintenance, insertion-only.
 *
 * Mirrors src/graph/layered_sphere.c's PHASE_INIT and shares its actual code
 * where the inputs allow: bucket_communities_into_spheres (the nucleus /
 * base_capacity*s^2 formula), compare_communities_kcore, sphere_radius_for,
 * build_sphere_grid, compare_nodes_placement, and seed_slots_for_sphere are
 * all called from layered_sphere_common.c unmodified. Two things differ from
 * the batch algorithm:
 *
 * 1. Coreness and Leiden community membership are never recomputed here —
 *    they're read live from DynKCore/DynLeiden (O(1) per-vertex lookups),
 *    replacing PHASE_INIT's igraph_coreness/igraph_community_leiden_simple
 *    calls. Community ids are therefore representative VERTEX ids (sparse,
 *    up to vcount), not the compact 0..C-1 cluster indices the batch path
 *    sees — dyn_ls_aggregate_communities and the vcount-sized comm_to_sphere
 *    array account for that.
 * 2. There is no annealed relaxation phase (PHASE_INTRA_SPHERE/
 *    PHASE_INTER_SPHERE): within a sphere, members are grouped by community
 *    and ordered purely by arrival timestamp, then each connected node gets
 *    a single one-shot refinement move toward its neighbors
 *    (dyn_ls_refine_connected). The batch's intra_degree/transitivity
 *    ordering keys need O(E)-ish full graph scans per call, which would
 *    defeat the point of using already-incremental coreness/community
 *    values; the timestamp rides in NodePlacement.density (with
 *    intra_degree pinned to 0) so the shared compare_nodes_placement
 *    comparator and seed_slots_for_sphere seeder apply unchanged — with all
 *    intra_degree equal, that comparator reduces exactly to (community asc,
 *    timestamp asc). The timestamp itself is the "timestamp" igraph vertex
 *    attribute (set in graph/stream.c's ensure_vertex from wall-clock
 *    arrival time; a genuine data-source timestamp can occupy the same
 *    attribute later with no change here), falling back to the vertex id —
 *    which also tracks insertion order — when the attribute is absent.
 *
 * Because deriving coreness/community is cheap, the bucketing+seeding pass
 * reruns on every connected arrival — an intentional full O(V + C log C)
 * recompute, not the O(touched) incremental style of DynKCore/DynLeiden.
 * The sphere GEOMETRY, however, persists: each grid is sized with a large
 * occupancy headroom (DYN_LS_SLOT_HEADROOM_BASE, applied to both the radius
 * and the slot count) when built, and a recompute only re-seeds occupants
 * into the existing slots. Grids are torn down and rebuilt ONLY on sphere
 * overflow — when a sphere's member count exceeds its slot capacity, or
 * more spheres are needed than exist (see the fits check in
 * dyn_ls_recompute). Two more shortcuts: a batch of arrivals that are ALL
 * still disconnected (degree 0) is appended straight onto the end of the
 * outermost sphere's curve with no recompute at all
 * (dyn_layered_sphere_on_update), and last_seen_vcount tracks which
 * vertices are new arrivals.
 *
 * Topological blast radius (stable core): an insertion (a new edge, or a new
 * vertex) only alters the coreness/community of vertices whose coreness is
 * <= k_max, the highest coreness among the newly arrived vertices. comms is
 * sorted by avg coreness descending and bucketed into contiguous,
 * non-decreasing sphere indices, so finding the first community at or below
 * k_max yields the innermost sphere that must be rebuilt (min_sphere_to_rebuild);
 * every sphere inside that radius is topologically insulated and keeps its
 * slot mappings entirely — only the outer spheres are cleared and re-seeded.
 * A grid overflow rebuild (fits == false) wipes all slot data, so it forces
 * min_sphere_to_rebuild back to 0 (full re-seed) regardless of k_max.
 *
 * Simplified Local Buffers (connected-arrival fast path): the shared seeder
 * interleaves each sphere's headroom as gaps across its slots, so a newly
 * arrived vertex that is already connected can often be appended straight
 * into its community's sphere (dyn_ls_try_local_append) without any re-sort
 * or recompute — it just fills one of those gaps. The attempt only fails (and
 * falls through to the full recompute) when the vertex starts a brand-new
 * community, or its community's sphere has no free slot left. A persistent
 * comm_sphere[] map (community id -> sphere, rebuilt every recompute) makes
 * the community->sphere lookup O(1) on the fast path. Disconnected-only
 * batches take the cheaper dyn_ls_append_disconnected path instead.
 */

#include "graph/dyn_layered_sphere.h"
#include "graph/community_simhash.h"
#include "graph/layered_sphere_common.h"

#include <igraph_step.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DYN_LS_TIMESTAMP_ATTR "timestamp"
#define DYN_LS_SLOT_HEADROOM_BASE 4.0		// every sphere's radius and slot count are sized for this multiple of its occupancy at build time, so grids absorb growth without resizing
#define DYN_LS_SLOT_HEADROOM_PER_SPHERE 0.5 // extra headroom fraction added on top per sphere index further from the nucleus, so outer spheres end up progressively sparser

// Simple high-resolution timer for performance breakdown logging, in
// microseconds (see the phase timestamps t0..t6 in dyn_ls_recompute).
static inline double dyn_ls_timer_us(const struct timespec *start, const struct timespec *end)
{
	return (double)(end->tv_sec - start->tv_sec) * 1000000.0 + (double)(end->tv_nsec - start->tv_nsec) / 1000.0;
}

struct DynLayeredSphere
{
	SphereGrid *grids;				   // persistent: built with headroom, reused across recomputes, torn down and rebuilt ONLY on sphere overflow (see dyn_ls_recompute's fits check)
	int grids_capacity;				   // capacity of grids[]
	int num_spheres;				   // grids currently built (for cleanup/reuse bookkeeping)
	igraph_integer_t last_seen_vcount; // vcount as of the end of the previous on_update/init call (append or recompute) — used to find newly-arrived vertices
	int *comm_sphere;				   // persistent: comm_sphere[comm_id] = sphere index of that community's members (-1 if the community is absent/new). Rebuilt from the live community map on every recompute; lets the fast-path local append locate a vertex's sphere without rescanning. comm_id is a vertex id (sparse, up to vcount).
	igraph_integer_t comm_sphere_cap;  // capacity of comm_sphere[] (== current vcount after the latest recompute)
	uint64_t *comm_simhash;			   // persistent: comm_simhash[comm_id] = SimHash of that community's member set at the last recompute. Lets a recompute skip re-seeding communities (and whole spheres) whose member set is unchanged — identical member set => identical avg_kcore => identical sphere, so no repositioning is needed. Keyed by comm_id (a vertex id), like comm_sphere.
	igraph_integer_t comm_simhash_cap; // capacity of comm_simhash[] (== current vcount after the latest recompute)
};

// ============================================================================
// Grid lifecycle
// ============================================================================

// Frees every currently-built grid's contents (not the grids[] array
// itself, which is kept for reuse) — mirrors layered_sphere_cleanup's
// per-grid teardown. Called only on sphere overflow (before a rebuild) and
// on destroy, never on an ordinary recompute.
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

// Grows comm_sphere[] to hold at least vcount entries (indexed by community
// ids, which are vertex ids up to vcount). Returns true on success.
static bool dyn_ls_ensure_comm_sphere(DynLayeredSphere *dls, igraph_integer_t vcount)
{
	if (vcount <= dls->comm_sphere_cap)
		return true;
	igraph_integer_t cap = dls->comm_sphere_cap ? dls->comm_sphere_cap : 16;
	while (cap < vcount)
		cap *= 2;
	int *grown = realloc(dls->comm_sphere, sizeof(int) * (size_t)cap);
	if (!grown) {
		fprintf(stderr, "dyn_layered_sphere: realloc comm_sphere to capacity %lld failed\n", (long long)cap);
		return false;
	}
	dls->comm_sphere = grown;
	dls->comm_sphere_cap = cap;
	return true;
}

static bool dyn_ls_ensure_comm_simhash(DynLayeredSphere *dls, igraph_integer_t vcount)
{
	if (vcount <= dls->comm_simhash_cap)
		return true;
	igraph_integer_t old_cap = dls->comm_simhash_cap;
	igraph_integer_t cap = dls->comm_simhash_cap ? dls->comm_simhash_cap : 16;
	while (cap < vcount)
		cap *= 2;
	uint64_t *grown = realloc(dls->comm_simhash, sizeof(uint64_t) * (size_t)cap);
	if (!grown) {
		fprintf(stderr, "dyn_layered_sphere: realloc comm_simhash to capacity %lld failed\n", (long long)cap);
		return false;
	}
	// Zero the newly grown portion so stale comparisons never spuriously
	// match a real hash on the first recompute after growth.
	memset(grown + old_cap, 0, sizeof(uint64_t) * (size_t)(cap - old_cap));
	dls->comm_simhash = grown;
	dls->comm_simhash_cap = cap;
	return true;
}

// Builds all num_spheres grids from the current per-sphere occupancy, each
// sized — radius AND slot count — for DYN_LS_SLOT_HEADROOM_BASE (+ the
// per-sphere extra) times its actual occupancy, so subsequent recomputes and
// appends fit into the existing slots without any resizing. Only called on
// sphere overflow; the previous grids must already be freed.
static bool dyn_ls_build_grids(DynLayeredSphere *dls, const int *sphere_count, int num_spheres)
{
	if (!dyn_ls_ensure_grids_capacity(dls, num_spheres))
		return false;
	memset(dls->grids, 0, sizeof(SphereGrid) * (size_t)num_spheres);
	dls->num_spheres = num_spheres; // set now so a mid-loop failure still lets dyn_ls_free_grid_contents clean up safely (build_sphere_grid nulls out a failed grid's own pointers)

	double current_radius = 0.0;
	for (int s = 0; s < num_spheres; s++) {
		if (sphere_count[s] == 0)
			continue; // defensive: bucketing never opens an empty sphere
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
// other vertex's position: no re-sort, no recompute. Returns false if
// there's no sphere to append into yet, or the outermost sphere is full;
// either way the caller falls back to a full recompute.
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
// grid that already holds its community, in a free (-1) slot near that
// community's existing members — no re-sort, no recompute. This is the
// "Simplified Local Buffers" fast path: headroom is already interleaved
// across every sphere's slots by the shared seeder, so an in-community
// arrival just fills one of those gaps.
//
// Returns false (and the caller falls through to a full recompute) when:
//   - comm_id has no sphere yet (a brand-new community), or
//   - the community's sphere has no free slot left (local/overall overflow).
// Only ever writes a free slot, so placement stays collision-free.
static bool dyn_ls_try_local_append(DynLayeredSphere *dls, igraph_integer_t node_id, igraph_integer_t comm_id, const igraph_integer_t *community, igraph_integer_t vcount, igraph_matrix_t *layout)
{
	// Fast path: cached comm_sphere mapping.
	int s = (comm_id >= 0 && comm_id < dls->comm_sphere_cap) ? dls->comm_sphere[comm_id] : -1;

	// When the cached sphere index is stale (e.g. Leiden renumbered the
	// community's representative id, or grids were rebuilt with fewer
	// spheres), scan all built grids' occupant slots for any vertex whose
	// community matches comm_id. In insertion-only streaming the community
	// always has at least one pre-existing member, so this succeeds for any
	// non-new community. The membership[] array is snapshot from the last
	// recompute's community data.
	if (s < 0 || s >= dls->num_spheres || !dls->grids[s].slot_occupant) {
		s = -1;
		for (int si = 0; si < dls->num_spheres; si++) {
			SphereGrid *g = &dls->grids[si];
			if (!g->slot_occupant)
				continue;
			for (int k = 0; k < g->max_slots; k++) {
				int occ = g->slot_occupant[k];
				if (occ >= 0 && occ < vcount && community[occ] == comm_id) {
					s = si;
					break;
				}
			}
			if (s >= 0)
				break;
		}
		// If the scan also found nothing, this is a truly new community
		// with no members yet — fall through to recompute.
		if (s < 0)
			return false;
		// Cache the discovered sphere so subsequent appends in the same
		// batch hit the fast path directly.
		if (comm_id >= 0 && comm_id < dls->comm_sphere_cap)
			dls->comm_sphere[comm_id] = s;
	}

	SphereGrid *grid = &dls->grids[s];

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
// Recompute stage 1: community aggregation
// (mirrors layered_sphere.c PHASE_INIT's CommData accumulation, adapted to
// sparse representative-vertex-id community labels)
// ============================================================================

// Builds the CommData array — one entry per non-empty community, avg
// coreness computed from the live per-vertex values — sorted by avg coreness
// descending via the shared compare_communities_kcore. *out_num_communities
// is always set (0 on any failure or if there are genuinely no communities);
// the caller must check it rather than just the returned pointer, since a
// legitimately empty result also returns NULL (calloc(0, ...) is allowed to).
static CommData *dyn_ls_aggregate_communities(const int *coreness, const igraph_integer_t *community, igraph_integer_t vcount, int *out_num_communities)
{
	*out_num_communities = 0;

	double *comm_sum_kcore = calloc((size_t)vcount, sizeof(double));
	int *comm_count = calloc((size_t)vcount, sizeof(int));
	if (!comm_sum_kcore || !comm_count) {
		fprintf(stderr, "dyn_layered_sphere: allocation failed\n");
		free(comm_sum_kcore);
		free(comm_count);
		return NULL;
	}
	for (igraph_integer_t i = 0; i < vcount; i++) {
		igraph_integer_t c = community[i];
		if (c < 0 || c >= vcount)
			continue; // defensive: community ids are always valid vertex ids in practice
		comm_sum_kcore[c] += coreness[i];
		comm_count[c]++;
	}

	int num_communities = 0;
	for (igraph_integer_t c = 0; c < vcount; c++)
		if (comm_count[c] > 0)
			num_communities++;

	CommData *comms = NULL;
	if (num_communities > 0) {
		comms = calloc((size_t)num_communities, sizeof(CommData));
		if (!comms) {
			fprintf(stderr, "dyn_layered_sphere: allocation failed\n");
			free(comm_sum_kcore);
			free(comm_count);
			return NULL;
		}
		int ci = 0;
		for (igraph_integer_t c = 0; c < vcount; c++) {
			if (comm_count[c] > 0) {
				comms[ci].comm_id = (int)c;
				comms[ci].avg_kcore = comm_sum_kcore[c] / comm_count[c];
				comms[ci].node_count = comm_count[c];
				ci++;
			}
		}
		qsort(comms, (size_t)num_communities, sizeof(CommData), compare_communities_kcore);
	}
	free(comm_sum_kcore);
	free(comm_count);

	*out_num_communities = num_communities;
	return comms;
}

// ============================================================================
// Recompute stage 2: per-sphere placement
// ============================================================================

// One-shot neighbor refinement for sphere s: each connected node gets a
// single direct move toward its neighbors via the shared
// node_hilbert_target/try_move_node pair (no damping schedule, no iteration
// — not the batch algorithm's annealed relaxation loop). Disconnected nodes
// are skipped outright.
static void dyn_ls_refine_connected(const igraph_t *g, LayeredSphereContext *ctx, int s)
{
	for (igraph_integer_t i = 0; i < ctx->vcount; i++) {
		if (ctx->node_to_sphere_id[i] != s)
			continue;
		igraph_integer_t deg;
		if (igraph_degree_1(g, &deg, i, IGRAPH_ALL, IGRAPH_NO_LOOPS) != IGRAPH_SUCCESS || deg == 0)
			continue;
		int current_slot = ctx->node_to_slot_idx[i];
		int target_slot = node_hilbert_target(g, ctx->layout, ctx, (int)i, s, true, 1.0, HILBERT_RES);
		if (target_slot != current_slot) {
			int moves = 0;
			try_move_node(g, ctx->layout, ctx, (int)i, s, target_slot, current_slot, true, &moves);
		}
	}
}

// Seeds sphere s's n_in_group members into its (already-built, persistent)
// grid: ordered via the shared compare_nodes_placement (density carries the
// timestamp, intra_degree is 0 — see the file header), seeded via the shared
// seed_slots_for_sphere, then refined via dyn_ls_refine_connected. Never
// touches the grid's geometry.
static bool dyn_ls_seed_sphere(const igraph_t *g, LayeredSphereContext *ctx, int s, int n_in_group, const igraph_integer_t *community, bool has_timestamp)
{
	NodePlacement *grp = malloc(sizeof(NodePlacement) * (size_t)n_in_group);
	if (!grp) {
		fprintf(stderr, "dyn_layered_sphere: allocation failed\n");
		return false;
	}
	int m = 0;
	for (igraph_integer_t i = 0; i < ctx->vcount; i++) {
		if (ctx->node_to_sphere_id[i] == s) {
			grp[m].id = (int)i;
			grp[m].community_id = (int)community[i];
			grp[m].density = has_timestamp ? VAN(g, DYN_LS_TIMESTAMP_ATTR, i) : (double)i;
			grp[m].intra_degree = 0;
			m++;
		}
	}
	qsort(grp, (size_t)m, sizeof(NodePlacement), compare_nodes_placement);
	seed_slots_for_sphere(ctx, s, grp, m);
	free(grp);

	dyn_ls_refine_connected(g, ctx, s);
	return true;
}

// ============================================================================
// Full recompute: orchestration only — aggregate, bucket (shared), map,
// re-seed the persistent grids (rebuilding them ONLY on sphere overflow).
// Rerun on every connected arrival.
// ============================================================================

static bool dyn_ls_recompute(DynLayeredSphere *dls, const igraph_t *g, const int *coreness, const igraph_integer_t *community, igraph_matrix_t *layout, int k_max)
{
	igraph_integer_t vcount = igraph_vcount(g);
	if (vcount == 0 || !coreness || !community) {
		dyn_ls_free_grid_contents(dls); // empty graph / maintainers gone = reset
		return true;
	}

	struct timespec t0 = {0}, t1 = {0}, t2 = {0}, t3 = {0}, t4 = {0}, t5 = {0}, t6 = {0};
	clock_gettime(CLOCK_MONOTONIC_RAW, &t0);

	// Every heap allocation below is freed exactly once at `cleanup`, whether
	// this call succeeds or bails out early — declare and zero-init them all
	// up front so `goto cleanup` from any failure point is safe (free(NULL)
	// is a no-op) instead of each failure branch hand-listing its own subset.
	bool result = false;
	CommData *comms = NULL;
	int *comm_to_sphere = NULL;
	uint64_t *simhash_now = NULL;
	igraph_integer_t *unstable_ids = NULL;
	int *node_to_sphere = NULL;
	int *node_to_slot = NULL;
	int *sphere_count = NULL;
	bool *sphere_changed = NULL;
	int num_communities = 0, num_spheres = 0, num_unstable = 0, min_sphere_to_rebuild = 0;

	comms = dyn_ls_aggregate_communities(coreness, community, vcount, &num_communities);
	if (num_communities == 0) {
		// Either aggregation hit an allocation failure (already logged inside
		// dyn_ls_aggregate_communities) or every community[] entry was
		// out-of-range (defensive-only case). Either way there is nothing to
		// place this round; keep the existing grids/layout untouched and let
		// the next poll retry rather than tearing down the maintainer.
		result = true;
		goto cleanup;
	}

	comm_to_sphere = malloc((size_t)vcount * sizeof(int)); // indexed by comm_id (a vertex id), unlike the batch path's compact-id array
	if (!comm_to_sphere) {
		fprintf(stderr, "dyn_layered_sphere: allocation failed\n");
		goto cleanup;
	}
	num_spheres = bucket_communities_into_spheres(comms, num_communities, (int)vcount, comm_to_sphere);

	// Ensure the per-community SimHash cache is sized for the current graph.
	if (!dyn_ls_ensure_comm_simhash(dls, vcount)) {
		fprintf(stderr, "dyn_layered_sphere: allocation failed\n");
		goto cleanup;
	}
	clock_gettime(CLOCK_MONOTONIC_RAW, &t1);

	// Compute SimHash for every live, unstable community using the shared
	// batch API: one O(vcount) pass over the membership array accumulates
	// every requested community's hash bits simultaneously (vs. calling
	// community_simhash_from_membership once per community, which would scan
	// membership from scratch each time — O(V*C) for C communities). Reuses
	// the exact same per-bit projection, so hash values are identical either
	// way; only the loop shape changes.
	simhash_now = calloc((size_t)vcount, sizeof(uint64_t));
	unstable_ids = malloc((size_t)num_communities * sizeof(igraph_integer_t));
	if (!simhash_now || !unstable_ids) {
		fprintf(stderr, "dyn_layered_sphere: allocation failed\n");
		goto cleanup;
	}
	for (int i = 0; i < num_communities; i++) {
		if (comms[i].avg_kcore > k_max)
			continue; // topologically insulated — member set unchanged by theorem
		unstable_ids[num_unstable++] = comms[i].comm_id;
	}
	if (num_unstable > 0)
		community_simhash_batch(community, vcount, unstable_ids, num_unstable, simhash_now);
	free(unstable_ids);
	unstable_ids = NULL;
	clock_gettime(CLOCK_MONOTONIC_RAW, &t2);

	// Node -> sphere map plus per-sphere group sizes, one O(V) pass.
	node_to_sphere = malloc((size_t)vcount * sizeof(int));
	node_to_slot = malloc((size_t)vcount * sizeof(int));
	sphere_count = calloc((size_t)num_spheres, sizeof(int));
	if (!node_to_sphere || !node_to_slot || !sphere_count) {
		fprintf(stderr, "dyn_layered_sphere: allocation failed\n");
		goto cleanup;
	}
	if (!dyn_ls_ensure_comm_sphere(dls, vcount)) {
		fprintf(stderr, "dyn_layered_sphere: allocation failed\n");
		goto cleanup;
	}
	// Reset the community->sphere map; the O(V) pass below repopulates it for
	// every community that currently has members. A brand-new community (not
	// yet present) keeps -1, which the fast-path append reads as "new".
	for (igraph_integer_t c = 0; c < vcount; c++)
		dls->comm_sphere[c] = -1;
	for (igraph_integer_t i = 0; i < vcount; i++) {
		igraph_integer_t cid = community[i];
		if (cid < 0 || cid >= vcount) {
			node_to_sphere[i] = -1; // defensive: mirrors dyn_ls_aggregate_communities' guard — leave unmapped rather than reading/writing out of bounds below
			continue;
		}
		node_to_sphere[i] = comm_to_sphere[cid];
		sphere_count[node_to_sphere[i]]++;
		dls->comm_sphere[cid] = node_to_sphere[i];
	}

	// Topological blast radius (stable core) tightened by SimHash: an
	// insertion only alters the coreness/community of vertices whose coreness
	// is <= k_max, but within that band a community whose member set is
	// unchanged (SimHash identical to the last recompute's cache) cannot have
	// changed sphere — so it must not force a rebuild. We scan comms (sorted
	// by avg_kcore descending) for the first community whose avg_kcore <=
	// k_max AND whose SimHash differs from cache; that community's sphere
	// becomes the outermost sphere that must be rebuilt. If no community
	// satisfies both conditions, the entire core is frozen.
	min_sphere_to_rebuild = num_spheres;
	for (int i = 0; i < num_communities; i++) {
		if (comms[i].avg_kcore <= k_max && simhash_now[comms[i].comm_id] != dls->comm_simhash[comms[i].comm_id]) {
			min_sphere_to_rebuild = comm_to_sphere[comms[i].comm_id];
			break;
		}
	}

	// Precompute per-sphere "changed" flags for the per-sphere seed skip (B):
	// a sphere whose every community has an unchanged SimHash keeps its
	// occupants and does not need re-seeding.
	sphere_changed = calloc((size_t)num_spheres, sizeof(bool));
	if (!sphere_changed) {
		fprintf(stderr, "dyn_layered_sphere: allocation failed\n");
		goto cleanup;
	}
	for (int i = 0; i < num_communities; i++) {
		int s = comm_to_sphere[comms[i].comm_id];
		if (comms[i].avg_kcore > k_max)
			continue; // topologically insulated — cache entry unchanged
		if (simhash_now[comms[i].comm_id] != dls->comm_simhash[comms[i].comm_id])
			sphere_changed[s] = true;
	}

	// Update the SimHash cache for every live community so the next poll
	// can detect changes. Stale comm_ids (beyond current communities)
	// keep their previous cache entry; they are never read because no vertex
	// maps to them.
	for (int ci = 0; ci < num_communities; ci++) {
		if (comms[ci].avg_kcore > k_max)
			continue; // topologically insulated — cache stays valid
		dls->comm_simhash[comms[ci].comm_id] = simhash_now[comms[ci].comm_id];
	}

	free(comms);
	comms = NULL;
	free(comm_to_sphere);
	comm_to_sphere = NULL;
	clock_gettime(CLOCK_MONOTONIC_RAW, &t3);

	// Sphere overflow check: the persistent grids are reused as long as
	// every sphere's members still fit in its (headroom-sized) slots and no
	// new sphere is needed; only a genuine overflow triggers a rebuild.
	bool fits = num_spheres <= dls->num_spheres;
	for (int s = 0; fits && s < num_spheres; s++)
		if (sphere_count[s] > dls->grids[s].max_slots)
			fits = false;

	bool ok = true;
	if (!fits) {
		fprintf(stderr, "dyn_layered_sphere: sphere overflow — rebuilding grids for %d sphere(s) (had %d)\n", num_spheres, dls->num_spheres);
		dyn_ls_free_grid_contents(dls);
		ok = dyn_ls_build_grids(dls, sphere_count, num_spheres);
		// Grids were just torn down and rebuilt: all persistent slot data was
		// wiped, so the entire graph must be re-seeded regardless of k_max.
		min_sphere_to_rebuild = 0;
		// Every sphere must be re-seeded after a grid teardown.
		for (int s = 0; s < num_spheres; s++)
			sphere_changed[s] = true;
	} else {
		// Reuse: clear occupants only; geometry (radius, slots) is untouched.
		// Only spheres at or outside the blast radius are cleared — members
		// may have moved between spheres, and spheres beyond num_spheres may
		// hold stale occupants. Frozen inner spheres (strictly inside the
		// radius) keep their slot mappings entirely.
		for (int s = min_sphere_to_rebuild; s < dls->num_spheres; s++)
			for (int k = 0; k < dls->grids[s].max_slots; k++)
				dls->grids[s].slot_occupant[k] = -1;
	}
	clock_gettime(CLOCK_MONOTONIC_RAW, &t4);

	// Debug: one-line summary of optimization decisions (throttled to avoid
	// flooding stderr on every poll). k_max is the blast-radius threshold;
	// communities with avg_kcore > k_max are topologically insulated and never
	// recomputed. min_sphere_to_rebuild is the first sphere that gets cleared
	// and re-seeded (0 means everything, num_spheres means nothing). fits
	// tells whether grids were reused (true) or rebuilt after overflow (false).
	// sphere_changed[s] is set per-sphere; any false sphere in the rebuild
	// band keeps its occupants without re-seeding.
	{
		static int log_counter = 0;
		if (++log_counter % 32 == 1) {
			int to_seed = 0;
			for (int s = min_sphere_to_rebuild; s < num_spheres; s++)
				if (sphere_count[s] > 0 && sphere_changed[s])
					to_seed++;
			fprintf(stderr, "dyn_ls: recompute k_max=%d comms=%d/%d frozen->s%d fits=%d spheres=%d/%d seed=%d\n", k_max, num_unstable, num_communities, min_sphere_to_rebuild, fits, num_spheres, dls->num_spheres, to_seed);
		}
	}

	if (ok) {
		LayeredSphereContext ctx = {0};
		ctx.grids = dls->grids;
		ctx.node_to_sphere_id = node_to_sphere;
		ctx.node_to_slot_idx = node_to_slot;
		ctx.layout = layout;
		ctx.vcount = (int)vcount;

		bool has_timestamp = igraph_cattribute_has_attr(g, IGRAPH_ATTRIBUTE_VERTEX, DYN_LS_TIMESTAMP_ATTR);
		for (int s = min_sphere_to_rebuild; s < num_spheres && ok; s++) {
			if (sphere_count[s] == 0)
				continue;
			if (!sphere_changed[s])
				continue; // B: per-sphere seed skip — all communities unchanged
			ok = dyn_ls_seed_sphere(g, &ctx, s, sphere_count[s], community, has_timestamp);
		}
	}
	clock_gettime(CLOCK_MONOTONIC_RAW, &t5);

	if (!ok)
		goto cleanup; // result stays false

	result = igraph_step(layout, NULL) == IGRAPH_SUCCESS;
	clock_gettime(CLOCK_MONOTONIC_RAW, &t6);
	{
		static int log_counter = 0;
		if (++log_counter % 32 == 1) {
			double us_agg = dyn_ls_timer_us(&t0, &t1);
			double us_sim = dyn_ls_timer_us(&t1, &t2);
			double us_ov = dyn_ls_timer_us(&t2, &t3);
			double us_chg = dyn_ls_timer_us(&t3, &t4);
			double us_seed = dyn_ls_timer_us(&t4, &t5);
			double us_step = dyn_ls_timer_us(&t5, &t6);
			double us_tot = dyn_ls_timer_us(&t0, &t6);
			fprintf(stderr, "dyn_ls_t: agg=%.0f bkt_sim=%.0f ovpass=%.0f chg=%.0f seed=%.0f step=%.0f tot=%.0f us\n", us_agg, us_sim, us_ov, us_chg, us_seed, us_step, us_tot);
		}
	}

cleanup:
	free(comms);
	free(comm_to_sphere);
	free(simhash_now);
	free(unstable_ids);
	free(node_to_sphere);
	free(node_to_slot);
	free(sphere_count);
	free(sphere_changed);
	return result;
}

// ============================================================================
// Public API
// ============================================================================

DynLayeredSphere *dyn_layered_sphere_init(const igraph_t *g, const int *coreness, const igraph_integer_t *community, igraph_matrix_t *layout)
{
	DynLayeredSphere *dls = calloc(1, sizeof(DynLayeredSphere));
	if (!dls) {
		fprintf(stderr, "dyn_layered_sphere_init: allocation failed\n");
		return NULL;
	}
	if (!dyn_ls_recompute(dls, g, coreness, community, layout, INT_MAX)) {
		dyn_layered_sphere_destroy(dls);
		return NULL;
	}
	dls->last_seen_vcount = igraph_vcount(g);
	return dls;
}

bool dyn_layered_sphere_on_update(DynLayeredSphere *dls, const igraph_t *g, const int *coreness, const igraph_integer_t *community, igraph_matrix_t *layout)
{
	if (!dls)
		return false;

	igraph_integer_t vcount = igraph_vcount(g);
	igraph_integer_t old_vcount = dls->last_seen_vcount;

	// Topological blast radius: k_max is the highest coreness among the newly
	// arrived vertices (those added since the previous call). An insertion can
	// only alter coreness/community for vertices with coreness <= k_max, so a
	// sphere whose minimum community coreness strictly exceeds k_max is
	// insulated and needs no rebuild. Defaults to INT_MAX (full recompute)
	// both when coreness is unavailable AND when there are no new vertices —
	// an edge-only batch between two already-known vertices is exactly the
	// "a new edge" case the blast-radius theorem also covers, but only a new
	// vertex's own coreness is tracked here, so an edge-only batch can't
	// derive a tighter bound and must not be silently treated as "nothing
	// changed" (that would leave coreness/community-driven moves stuck).
	int k_max = INT_MAX;
	if (vcount > old_vcount) {
		k_max = -1;
		for (igraph_integer_t v = old_vcount; v < vcount; v++)
			if (coreness && coreness[v] > k_max)
				k_max = coreness[v];
		// coreness was entirely NULL — can't determine blast radius,
		// default to full rebuild.
		if (k_max < 0)
			k_max = INT_MAX;

		// Connected-arrival fast path (Simplified Local Buffers): each newly
		// arrived vertex is appended into its community's sphere (connected)
		// or the outermost sphere's tail (disconnected) with no re-sort. If
		// any vertex's append fails (new community, full sphere, no sphere
		// built yet), we fall through to the full recompute.
		bool all_local = true;
		for (igraph_integer_t v = old_vcount; v < vcount && all_local; v++) {
			igraph_integer_t deg;
			if (igraph_degree_1(g, &deg, v, IGRAPH_ALL, IGRAPH_NO_LOOPS) != IGRAPH_SUCCESS)
				deg = 0;
			if (deg > 0) {
				if (!dyn_ls_try_local_append(dls, v, community[v], community, vcount, layout))
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
		// Some vertex's append failed (new community, full sphere, no sphere
		// built yet) — some vertices in this batch may already be placed, so
		// fall through to a full recompute rather than retrying the
		// disconnected-only path, which would double-place them.
	}

	dls->last_seen_vcount = vcount;
	return dyn_ls_recompute(dls, g, coreness, community, layout, k_max);
}

void dyn_layered_sphere_destroy(DynLayeredSphere *dls)
{
	if (!dls)
		return;
	dyn_ls_free_grid_contents(dls);
	free(dls->grids);
	free(dls->comm_sphere);
	free(dls->comm_simhash);
	free(dls);
}
