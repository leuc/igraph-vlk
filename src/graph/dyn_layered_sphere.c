/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Dynamic (streaming) Layered Sphere layout maintenance, insertion-only.
 *
 * Mirrors src/graph/layered_sphere.c's PHASE_INIT bucketing exactly (see the
 * comments below referencing its line ranges), reusing
 * layered_sphere_common.c's CommData/compare_communities_kcore,
 * sphere_radius_for, and build_sphere_grid unmodified. Two things differ
 * from the batch algorithm:
 *
 * 1. Coreness and Leiden community membership are never recomputed here —
 *    they're read live from DynKCore/DynLeiden (O(1) per-vertex lookups),
 *    replacing PHASE_INIT's igraph_coreness/igraph_community_leiden_simple
 *    calls (layered_sphere.c:37-59). Community ids are therefore
 *    representative VERTEX ids (sparse, up to vcount), not the compact
 *    0..C-1 cluster indices igraph_community_leiden_simple produces — the
 *    community aggregation below accounts for that.
 * 2. There is no relaxation phase (PHASE_INTRA_SPHERE/PHASE_INTER_SPHERE,
 *    layered_sphere.c:170-198): placement within a sphere orders members by
 *    (community_id, own coreness) instead of the batch's
 *    (community_id, intra_degree, density), since intra_degree/transitivity
 *    both require an O(E)-ish full graph scan per call
 *    (layered_sphere.c:114-131) that would defeat the point of using
 *    already-incremental coreness/community values.
 *
 * Because deriving coreness/community is cheap, the entire bucketing+
 * placement pass reruns from scratch every call — an intentional full
 * O(V + C log C) recompute, not the O(touched) incremental style of
 * DynKCore/DynLeiden. Nothing needs to persist between calls except the
 * SphereGrid array itself (reused/regrown across calls purely to avoid
 * malloc/free churn, not because state carries over).
 */

#include "graph/dyn_layered_sphere.h"
#include "graph/layered_sphere_common.h"

#include <igraph_step.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct DynLayeredSphere
{
	SphereGrid *grids;		 // rebuilt from scratch every dyn_ls_recompute call; realloc-grown (doubling) across calls to reuse allocations
	int grids_capacity;		 // capacity of grids[]
	int num_spheres;		 // spheres populated by the most recent recompute (for cleanup bookkeeping)
	int last_logged_spheres; // num_spheres as of the last "sphere overflow" log — since the whole bucketing reruns every call, this is what keeps that log from firing on every single poll even when the sphere count hasn't actually changed
};

// Sphere-local member ordering: (community_id asc, own_coreness desc) — see
// the file header for why this replaces the batch algorithm's
// (community_id, intra_degree, density) ordering.
typedef struct
{
	igraph_integer_t id;
	int community_id;
	int own_coreness;
} DynLsMember;

static int dyn_ls_compare_member(const void *a, const void *b)
{
	const DynLsMember *ma = a;
	const DynLsMember *mb = b;
	if (ma->community_id != mb->community_id)
		return ma->community_id - mb->community_id;
	return mb->own_coreness - ma->own_coreness;
}

// Frees every currently-populated grid's contents (not the grids[] array
// itself, which is kept for reuse) — mirrors layered_sphere_cleanup's
// per-grid teardown, run per-call here instead of per-maintainer-lifetime.
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

// Full bucketing+placement recompute — the whole algorithm, run fresh every
// call. Mirrors layered_sphere.c's PHASE_INIT (lines 29-167) apart from the
// two differences noted in the file header.
static bool dyn_ls_recompute(DynLayeredSphere *dls, const igraph_t *g, const int *coreness, const igraph_integer_t *community, igraph_matrix_t *layout)
{
	dyn_ls_free_grid_contents(dls);

	igraph_integer_t vcount = igraph_vcount(g);
	if (vcount == 0 || !coreness || !community)
		return true;

	// --- Community aggregation (mirrors layered_sphere.c:61-73) ---
	// comm ids are representative vertex ids here, not compact 0..C-1
	// indices, so accumulate into vcount-sized flat arrays instead of a
	// num_communities-sized one.
	double *comm_sum_kcore = calloc((size_t)vcount, sizeof(double));
	int *comm_count = calloc((size_t)vcount, sizeof(int));
	if (!comm_sum_kcore || !comm_count) {
		fprintf(stderr, "dyn_layered_sphere: allocation failed\n");
		free(comm_sum_kcore);
		free(comm_count);
		return false;
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
		return false;
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

	// --- Greedy sphere bucketing (mirrors layered_sphere.c:76-103 exactly) ---
	int nucleus_capacity = comms[0].node_count;
	int remaining_nodes = (int)vcount - nucleus_capacity;
	int base_capacity = (int)fmax(15.0, remaining_nodes * 0.015);

	int current_sphere = 0;
	int current_load = 0;
	int *comm_to_sphere = malloc((size_t)vcount * sizeof(int)); // indexed by comm_id (a vertex id)
	if (!comm_to_sphere) {
		fprintf(stderr, "dyn_layered_sphere: allocation failed\n");
		free(comms);
		return false;
	}

	for (int i = 0; i < num_communities; i++) {
		int c_size = comms[i].node_count;

		int sphere_capacity = (current_sphere == 0) ? nucleus_capacity : base_capacity * current_sphere * current_sphere;

		if (current_load > 0 && current_load + c_size > sphere_capacity) {
			current_sphere++;
			current_load = 0;
		}

		comm_to_sphere[comms[i].comm_id] = current_sphere;
		current_load += c_size;
	}
	int num_spheres = current_sphere + 1;
	free(comms);

	// The whole bucketing above reruns from scratch every call (see the file
	// header), so log only when the sphere COUNT actually changes across
	// calls — otherwise this would fire every single poll even when nothing
	// structurally changed.
	if (num_spheres != dls->last_logged_spheres) {
		fprintf(stderr, "dyn_layered_sphere: sphere overflow — now using %d sphere(s) (was %d)\n", num_spheres, dls->last_logged_spheres);
		dls->last_logged_spheres = num_spheres;
	}

	int *node_to_sphere = malloc((size_t)vcount * sizeof(int));
	if (!node_to_sphere) {
		fprintf(stderr, "dyn_layered_sphere: allocation failed\n");
		free(comm_to_sphere);
		return false;
	}
	for (igraph_integer_t i = 0; i < vcount; i++)
		node_to_sphere[i] = comm_to_sphere[community[i]];
	free(comm_to_sphere);

	// --- Per-sphere grid + placement (mirrors layered_sphere.c:135-159) ---
	if (!dyn_ls_ensure_grids_capacity(dls, num_spheres)) {
		free(node_to_sphere);
		return false;
	}
	memset(dls->grids, 0, sizeof(SphereGrid) * (size_t)num_spheres);
	dls->num_spheres = num_spheres; // set now so a mid-loop failure still lets the next call's dyn_ls_free_grid_contents clean up safely (build_sphere_grid nulls out a failed grid's own pointers)

	bool ok = true;
	double current_radius = 0.0;
	for (int s = 0; s < num_spheres && ok; s++) {
		int n_in_group = 0;
		for (igraph_integer_t i = 0; i < vcount; i++)
			if (node_to_sphere[i] == s)
				n_in_group++;
		if (n_in_group == 0)
			continue;

		current_radius = sphere_radius_for(s, n_in_group, current_radius);
		if (!build_sphere_grid(&dls->grids[s], n_in_group, current_radius, HILBERT_RES)) {
			ok = false;
			break;
		}

		DynLsMember *members = malloc(sizeof(DynLsMember) * (size_t)n_in_group);
		if (!members) {
			fprintf(stderr, "dyn_layered_sphere: allocation failed\n");
			ok = false;
			break;
		}
		int m = 0;
		for (igraph_integer_t i = 0; i < vcount; i++) {
			if (node_to_sphere[i] == s) {
				members[m].id = i;
				members[m].community_id = (int)community[i];
				members[m].own_coreness = coreness[i];
				m++;
			}
		}
		qsort(members, (size_t)m, sizeof(DynLsMember), dyn_ls_compare_member);

		int M_s = dls->grids[s].max_slots;
		int step = M_s / m;
		for (int i = 0; i < m; i++) {
			igraph_integer_t nid = members[i].id;
			int sid = (int)fmin(M_s - 1, (double)(i * step));
			dls->grids[s].slot_occupant[sid] = (int)nid;
			MATRIX(*layout, nid, 0) = dls->grids[s].slots[sid].x;
			MATRIX(*layout, nid, 1) = dls->grids[s].slots[sid].y;
			MATRIX(*layout, nid, 2) = dls->grids[s].slots[sid].z;
		}
		free(members);
	}

	free(node_to_sphere);
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
	if (!dyn_ls_recompute(dls, g, coreness, community, layout)) {
		dyn_layered_sphere_destroy(dls);
		return NULL;
	}
	return dls;
}

bool dyn_layered_sphere_on_update(DynLayeredSphere *dls, const igraph_t *g, const int *coreness, const igraph_integer_t *community, igraph_matrix_t *layout)
{
	if (!dls)
		return false;
	return dyn_ls_recompute(dls, g, coreness, community, layout);
}

void dyn_layered_sphere_destroy(DynLayeredSphere *dls)
{
	if (!dls)
		return;
	dyn_ls_free_grid_contents(dls);
	free(dls->grids);
	free(dls);
}
