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
 */

#include "graph/dyn_layered_sphere.h"
#include "graph/layered_sphere_common.h"

#include <igraph_step.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DYN_LS_TIMESTAMP_ATTR "timestamp"
#define DYN_LS_SLOT_HEADROOM_BASE 4.0		// every sphere's radius and slot count are sized for this multiple of its occupancy at build time, so grids absorb growth without resizing
#define DYN_LS_SLOT_HEADROOM_PER_SPHERE 0.5 // extra headroom fraction added on top per sphere index further from the nucleus, so outer spheres end up progressively sparser

struct DynLayeredSphere
{
	SphereGrid *grids;				   // persistent: built with headroom, reused across recomputes, torn down and rebuilt ONLY on sphere overflow (see dyn_ls_recompute's fits check)
	int grids_capacity;				   // capacity of grids[]
	int num_spheres;				   // grids currently built (for cleanup/reuse bookkeeping)
	igraph_integer_t last_seen_vcount; // vcount as of the end of the previous on_update/init call (append or recompute) — used to find newly-arrived vertices
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
			MATRIX(*layout, node_id, 0) = grid->slots[slot].x;
			MATRIX(*layout, node_id, 1) = grid->slots[slot].y;
			MATRIX(*layout, node_id, 2) = grid->slots[slot].z;
			return true;
		}
	}
	return false;
}

// ============================================================================
// Recompute stage 1: community aggregation
// (mirrors layered_sphere.c PHASE_INIT's CommData accumulation, adapted to
// sparse representative-vertex-id community labels)
// ============================================================================

// Builds the CommData array — one entry per non-empty community, avg
// coreness computed from the live per-vertex values — sorted by avg coreness
// descending via the shared compare_communities_kcore. Returns NULL on
// allocation failure.
static CommData *dyn_ls_aggregate_communities(const int *coreness, const igraph_integer_t *community, igraph_integer_t vcount, int *out_num_communities)
{
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

	CommData *comms = calloc((size_t)num_communities, sizeof(CommData));
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
	free(comm_sum_kcore);
	free(comm_count);

	qsort(comms, (size_t)num_communities, sizeof(CommData), compare_communities_kcore);
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

	int num_communities;
	CommData *comms = dyn_ls_aggregate_communities(coreness, community, vcount, &num_communities);
	if (!comms)
		return false;

	int *comm_to_sphere = malloc((size_t)vcount * sizeof(int)); // indexed by comm_id (a vertex id), unlike the batch path's compact-id array
	if (!comm_to_sphere) {
		fprintf(stderr, "dyn_layered_sphere: allocation failed\n");
		free(comms);
		return false;
	}
	int num_spheres = bucket_communities_into_spheres(comms, num_communities, (int)vcount, comm_to_sphere);

	// Topological blast radius (stable core): an insertion only alters the
	// coreness/community of vertices whose coreness is <= k_max (the highest
	// coreness among the newly arrived vertices), so any community whose avg
	// coreness strictly exceeds k_max is topologically insulated. comms is
	// sorted by avg_kcore descending and bucketed into contiguous,
	// non-decreasing sphere indices, so the first community at or below k_max
	// marks the innermost sphere that must be rebuilt; every sphere inside
	// that radius keeps its slot mappings entirely. No such community means
	// the whole core is frozen. comms is read here, so it must not be freed
	// until after this scan.
	int min_sphere_to_rebuild = num_spheres;
	for (int i = 0; i < num_communities; i++) {
		if (comms[i].avg_kcore <= k_max) {
			min_sphere_to_rebuild = comm_to_sphere[comms[i].comm_id];
			break;
		}
	}

	free(comms);

	// Node -> sphere map plus per-sphere group sizes, one O(V) pass.
	int *node_to_sphere = malloc((size_t)vcount * sizeof(int));
	int *node_to_slot = malloc((size_t)vcount * sizeof(int));
	int *sphere_count = calloc((size_t)num_spheres, sizeof(int));
	if (!node_to_sphere || !node_to_slot || !sphere_count) {
		fprintf(stderr, "dyn_layered_sphere: allocation failed\n");
		free(comm_to_sphere);
		free(node_to_sphere);
		free(node_to_slot);
		free(sphere_count);
		return false;
	}
	for (igraph_integer_t i = 0; i < vcount; i++) {
		node_to_sphere[i] = comm_to_sphere[community[i]];
		sphere_count[node_to_sphere[i]]++;
	}
	free(comm_to_sphere);

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
			ok = dyn_ls_seed_sphere(g, &ctx, s, sphere_count[s], community, has_timestamp);
		}
	}

	free(node_to_sphere);
	free(node_to_slot);
	free(sphere_count);
	if (!ok)
		return false;

	return igraph_step(layout, NULL) == IGRAPH_SUCCESS;
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
	// insulated and needs no rebuild. No new vertices (shouldn't happen on an
	// insertion) defaults to INT_MAX so everything recomputes.
	int k_max = INT_MAX;
	if (vcount > old_vcount) {
		k_max = -1;
		for (igraph_integer_t v = old_vcount; v < vcount; v++)
			if (coreness && coreness[v] > k_max)
				k_max = coreness[v];
	}

	// Simple condition: if every vertex that arrived since the last call is
	// still disconnected (degree 0), just append each one to the end of the
	// outermost sphere's curve directly — no re-sort. A single connected
	// arrival (or a failed append) falls through to the full recompute.
	bool all_new_disconnected = (vcount > old_vcount);
	for (igraph_integer_t v = old_vcount; v < vcount && all_new_disconnected; v++) {
		igraph_integer_t deg;
		if (igraph_degree_1(g, &deg, v, IGRAPH_ALL, IGRAPH_NO_LOOPS) != IGRAPH_SUCCESS || deg > 0)
			all_new_disconnected = false;
	}
	if (all_new_disconnected) {
		bool appended_all = true;
		for (igraph_integer_t v = old_vcount; v < vcount && appended_all; v++)
			appended_all = dyn_ls_append_disconnected(dls, v, layout);
		if (appended_all) {
			dls->last_seen_vcount = vcount;
			return igraph_step(layout, NULL) == IGRAPH_SUCCESS;
		}
		// couldn't append (no sphere yet, or outermost sphere full) — fall through to the recompute below
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
	free(dls);
}
