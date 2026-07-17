/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Dynamic (streaming) maintenance of Leiden community membership.
 *
 * This is a single-threaded C port of the Dynamic Frontier (DF) approach to
 * the Leiden algorithm from Subhajit Sahu's parallel GVE-Leiden
 * implementation (leiden.hxx):
 *   https://github.com/puzzlef/leiden-communities-openmp-dynamic
 *   Sahu, S. "A Starting Point for Dynamic Community Detection with
 *     Leiden Algorithm." arXiv:2405.11658.
 *   Sahu, S. (2024). "Heuristic-Based Dynamic Leiden Algorithm for Efficient
 *     Tracking of Communities on Evolving Graphs." arXiv:2410.15451.
 *     https://doi.org/10.48550/arXiv.2410.15451
 * The static Leiden objective it maintains is from Traag, Waltman & van Eck
 * (2019), implemented here via igraph's igraph_community_leiden_simple.
 */

#include "graph/dyn_leiden.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DYN_LEIDEN_MAX_ITERATIONS 20 // local-move wave cap, matches GVE-Leiden's default (Sahu 2024, §5.1.2)
#define DYN_LEIDEN_MAX_PASSES 10	 // aggregation (community-merge) pass cap, matches GVE-Leiden's default (Sahu 2024, §5.1.2)

// Density-scaled CPM resolution, matching the static Leiden menu command
// (wrappers_community.c:compute_igraph_community_leiden) and
// layered_sphere.c: gamma = max(3 * graph density, 0.001), recomputed from
// the live graph on every dyn_leiden_init/dyn_leiden_on_edges call. The
// heuristic is deliberately duplicated here rather than shared, since it is
// small and consolidating it is a separate, unrelated concern.
static double dyn_leiden_cpm_resolution(const igraph_t *g)
{
	igraph_integer_t vcount = igraph_vcount(g);
	igraph_integer_t ecount = igraph_ecount(g);
	double graph_density = (vcount > 1) ? (2.0 * (double)ecount) / ((double)vcount * (double)(vcount - 1)) : 0.0;
	return fmax(graph_density * 3.0, 0.001);
}

// ============================================================================
// State
//
// Community ids are always a representative original vertex id (never a
// compacted 0..C-1 index): vertices start as their own singleton (vcom[v]=v)
// and community labels persist across merges even if their namesake vertex
// later moves elsewhere. Membership per community is a doubly-linked list
// (comm_head/comm_next/comm_prev) so aggregation can enumerate a touched
// community's exact members in O(members), not O(V).
//
// All per-vertex/per-community arrays share one capacity, grown by doubling
// like dyn_k-core.c. The frontier worklist is an explicit FIFO (queue/
// in_queue), not GVE-Leiden's O(V)-per-wave dense scan — a live stream
// calls dyn_leiden_on_edges every poll, so an O(V) scan per call would
// defeat the point of incremental maintenance.
// ============================================================================

struct DynLeiden
{
	igraph_integer_t *vcom; // community label per vertex
	igraph_integer_t *csiz; // community size (member count) per community label

	igraph_integer_t *comm_head; // first member of community c, or -1
	igraph_integer_t *comm_next; // next member after v in its community, or -1
	igraph_integer_t *comm_prev; // previous member before v in its community, or -1

	// Refinement snapshot, written only for touched vertices; an untouched
	// vertex's pre-refine community is simply its live vcom (never modified
	// outside the frontier).
	igraph_integer_t *vcob;
	char *touched_flag;
	igraph_integer_t *touched;
	igraph_integer_t touched_count;

	// "Community c changed this call" flag, tracked via an append-only list
	// (not a full-array scan) so end-of-call cleanup stays O(touched).
	char *cchg;
	igraph_integer_t *cchg_list;
	igraph_integer_t cchg_count;

	// Frontier worklist: ring buffer sized to capacity (at most `capacity`
	// distinct ids can be enqueued at once, deduped via in_queue).
	igraph_integer_t *queue;
	char *in_queue;
	igraph_integer_t queue_head;
	igraph_integer_t queue_count;

	// Per-scan neighbor-community weight accumulator ("touched-list + dense
	// array", igraph has no sparse accumulator); vcs/vcout are capacity-sized
	// so no separate growth logic is needed for them.
	igraph_integer_t *vcs;
	igraph_integer_t vcs_count;
	double *vcout;

	igraph_vector_int_t neis; // reusable neighbor buffer (vertex ids)
	igraph_vector_int_t inc;  // reusable incident-edge-id buffer

	igraph_integer_t vcount;
	igraph_integer_t capacity;

	double resolution; // CPM gamma, recomputed from live graph density on every init/on_edges call (see dyn_leiden_cpm_resolution)

	int max_frontier_size; // lifetime max touched_count
	int last_frontier_size;
};

// ============================================================================
// Capacity / vertex-count sync
// ============================================================================

static bool dyn_leiden_sync_vcount(DynLeiden *dl, const igraph_t *g)
{
	igraph_integer_t n = igraph_vcount(g);
	if (n <= dl->vcount)
		return true;

	if (n > dl->capacity) {
		igraph_integer_t cap = dl->capacity ? dl->capacity : 64;
		while (cap < n)
			cap *= 2;

		igraph_integer_t *vcom = realloc(dl->vcom, sizeof(igraph_integer_t) * (size_t)cap);
		igraph_integer_t *csiz = realloc(dl->csiz, sizeof(igraph_integer_t) * (size_t)cap);
		igraph_integer_t *comm_head = realloc(dl->comm_head, sizeof(igraph_integer_t) * (size_t)cap);
		igraph_integer_t *comm_next = realloc(dl->comm_next, sizeof(igraph_integer_t) * (size_t)cap);
		igraph_integer_t *comm_prev = realloc(dl->comm_prev, sizeof(igraph_integer_t) * (size_t)cap);
		igraph_integer_t *vcob = realloc(dl->vcob, sizeof(igraph_integer_t) * (size_t)cap);
		char *touched_flag = realloc(dl->touched_flag, sizeof(char) * (size_t)cap);
		igraph_integer_t *touched = realloc(dl->touched, sizeof(igraph_integer_t) * (size_t)cap);
		char *cchg = realloc(dl->cchg, sizeof(char) * (size_t)cap);
		igraph_integer_t *cchg_list = realloc(dl->cchg_list, sizeof(igraph_integer_t) * (size_t)cap);
		igraph_integer_t *queue = realloc(dl->queue, sizeof(igraph_integer_t) * (size_t)cap);
		char *in_queue = realloc(dl->in_queue, sizeof(char) * (size_t)cap);
		igraph_integer_t *vcs = realloc(dl->vcs, sizeof(igraph_integer_t) * (size_t)cap);
		double *vcout = realloc(dl->vcout, sizeof(double) * (size_t)cap);

		if (vcom)
			dl->vcom = vcom;
		if (csiz)
			dl->csiz = csiz;
		if (comm_head)
			dl->comm_head = comm_head;
		if (comm_next)
			dl->comm_next = comm_next;
		if (comm_prev)
			dl->comm_prev = comm_prev;
		if (vcob)
			dl->vcob = vcob;
		if (touched_flag)
			dl->touched_flag = touched_flag;
		if (touched)
			dl->touched = touched;
		if (cchg)
			dl->cchg = cchg;
		if (cchg_list)
			dl->cchg_list = cchg_list;
		if (queue)
			dl->queue = queue;
		if (in_queue)
			dl->in_queue = in_queue;
		if (vcs)
			dl->vcs = vcs;
		if (vcout)
			dl->vcout = vcout;

		if (!vcom || !csiz || !comm_head || !comm_next || !comm_prev || !vcob || !touched_flag || !touched || !cchg || !cchg_list || !queue || !in_queue || !vcs || !vcout) {
			fprintf(stderr, "dyn_leiden: realloc to capacity %lld failed\n", (long long)cap);
			return false;
		}
		dl->capacity = cap;
	}

	// New vertices: singleton community of their own, zero weight.
	for (igraph_integer_t v = dl->vcount; v < n; v++) {
		dl->vcom[v] = v;
		dl->csiz[v] = 1;
		dl->comm_head[v] = v;
		dl->comm_next[v] = -1;
		dl->comm_prev[v] = -1;
		dl->vcob[v] = v;
		dl->touched_flag[v] = 0;
		dl->cchg[v] = 0;
		dl->in_queue[v] = 0;
		dl->vcout[v] = 0.0;
	}
	dl->vcount = n;
	return true;
}

// ============================================================================
// Neighbor fetch (mirrors dyn_k-core.c's fetch_neighbors)
// ============================================================================

static bool dyn_leiden_fetch_neighbors(DynLeiden *dl, const igraph_t *g, igraph_integer_t w)
{
	if (igraph_incident(g, &dl->inc, w, IGRAPH_ALL, IGRAPH_LOOPS_TWICE) != IGRAPH_SUCCESS) {
		fprintf(stderr, "dyn_leiden: igraph_incident failed for vertex %lld\n", (long long)w);
		return false;
	}
	igraph_vector_int_clear(&dl->neis);
	igraph_integer_t m = igraph_vector_int_size(&dl->inc);
	for (igraph_integer_t i = 0; i < m; i++) {
		igraph_integer_t eid = VECTOR(dl->inc)[i];
		igraph_integer_t from, to;
		if (igraph_edge(g, eid, &from, &to) != IGRAPH_SUCCESS) {
			fprintf(stderr, "dyn_leiden: igraph_edge failed for edge %lld\n", (long long)eid);
			return false;
		}
		if (igraph_vector_int_push_back(&dl->neis, (from == w) ? to : from) != IGRAPH_SUCCESS) {
			fprintf(stderr, "dyn_leiden: neighbor buffer push_back failed\n");
			return false;
		}
	}
	return true;
}

// ============================================================================
// Community membership list (doubly-linked, O(1) insert/remove)
// ============================================================================

static void dyn_leiden_list_remove(DynLeiden *dl, igraph_integer_t v)
{
	igraph_integer_t c = dl->vcom[v];
	igraph_integer_t prev = dl->comm_prev[v], next = dl->comm_next[v];
	if (prev >= 0)
		dl->comm_next[prev] = next;
	else
		dl->comm_head[c] = next;
	if (next >= 0)
		dl->comm_prev[next] = prev;
	dl->comm_prev[v] = -1;
	dl->comm_next[v] = -1;
}

static void dyn_leiden_list_insert(DynLeiden *dl, igraph_integer_t c, igraph_integer_t v)
{
	igraph_integer_t head = dl->comm_head[c];
	dl->comm_next[v] = head;
	dl->comm_prev[v] = -1;
	if (head >= 0)
		dl->comm_prev[head] = v;
	dl->comm_head[c] = v;
}

// Central mutator: every community change (local-move, refine reset,
// aggregation merge) goes through here so vcom/csiz/membership-list stay
// consistent. changed is best-effort (push_back failure is silently
// skipped, matching dyn_k-core.c's convention — the maintained state is
// already correct, only the caller's optional changelist is incomplete).
static void dyn_leiden_move(DynLeiden *dl, igraph_integer_t v, igraph_integer_t new_c, igraph_vector_int_t *changed)
{
	igraph_integer_t old_c = dl->vcom[v];
	if (old_c == new_c)
		return;
	dyn_leiden_list_remove(dl, v);
	dl->csiz[old_c]--;
	dl->vcom[v] = new_c;
	dyn_leiden_list_insert(dl, new_c, v);
	dl->csiz[new_c]++;
	if (changed)
		igraph_vector_int_push_back(changed, v);
}

static igraph_integer_t dyn_leiden_vcob_of(const DynLeiden *dl, igraph_integer_t v)
{
	return dl->touched_flag[v] ? dl->vcob[v] : dl->vcom[v];
}

// ============================================================================
// Community-change bookkeeping (append-only list, cleared in O(touched))
// ============================================================================

static void dyn_leiden_flag_cchg(DynLeiden *dl, igraph_integer_t c)
{
	if (!dl->cchg[c]) {
		dl->cchg[c] = 1;
		dl->cchg_list[dl->cchg_count++] = c;
	}
}

// ============================================================================
// Frontier worklist
// ============================================================================

static bool dyn_leiden_enqueue(DynLeiden *dl, igraph_integer_t v)
{
	if (dl->in_queue[v])
		return true;
	if (dl->queue_count >= dl->capacity) {
		fprintf(stderr, "dyn_leiden: queue overflow (capacity %lld)\n", (long long)dl->capacity);
		return false; // unreachable: at most `capacity` distinct ids exist
	}
	igraph_integer_t tail = (dl->queue_head + dl->queue_count) % dl->capacity;
	dl->queue[tail] = v;
	dl->queue_count++;
	dl->in_queue[v] = 1;
	if (!dl->touched_flag[v]) {
		dl->touched_flag[v] = 1;
		dl->touched[dl->touched_count++] = v;
	}
	return true;
}

static igraph_integer_t dyn_leiden_dequeue(DynLeiden *dl)
{
	igraph_integer_t v = dl->queue[dl->queue_head];
	dl->queue_head = (dl->queue_head + 1) % dl->capacity;
	dl->queue_count--;
	dl->in_queue[v] = 0;
	return v;
}

// Forces the empty-queue invariant sync_vcount's growth relies on: if a wave
// cap cut a loop short with entries still queued, drop them rather than
// leave a ring buffer straddling a future realloc.
static void dyn_leiden_queue_drain(DynLeiden *dl)
{
	while (dl->queue_count > 0) {
		igraph_integer_t v = dl->queue[dl->queue_head];
		dl->queue_head = (dl->queue_head + 1) % dl->capacity;
		dl->queue_count--;
		dl->in_queue[v] = 0;
	}
	dl->queue_head = 0;
}

// ============================================================================
// Neighbor-community weight scan (port of GVE-Leiden leidenScanCommunitiesW,
// leiden.hxx)
//
// igraph has no sparse accumulator, so this is a "touched-list + dense
// array" pair: vcs/vcout are capacity-sized (at most one entry per distinct
// community label), cleared by walking vcs after each scan rather than
// memset over the whole array.
// ============================================================================

static void dyn_leiden_scan_clear(DynLeiden *dl)
{
	for (igraph_integer_t i = 0; i < dl->vcs_count; i++)
		dl->vcout[dl->vcs[i]] = 0.0;
	dl->vcs_count = 0;
}

static void dyn_leiden_scan_add(DynLeiden *dl, igraph_integer_t c)
{
	if (dl->vcout[c] == 0.0)
		dl->vcs[dl->vcs_count++] = c;
	dl->vcout[c] += 1.0; // unweighted: every edge contributes 1.0
}

// Per-vertex scan (local-move/refine): candidate communities are u's live
// neighbor communities, weight = 1.0 per edge, self-loops excluded (a
// self-loop never proposes moving u "elsewhere" — it's internal wherever u
// ends up, so it cancels out of every delta-CPM comparison).
// In REFINE mode, candidates are further restricted to neighbors sharing u's
// pre-refine (vcob) community, matching the reference's REFINE gate.
static bool dyn_leiden_scan_vertex(DynLeiden *dl, const igraph_t *g, igraph_integer_t u, bool refine)
{
	dyn_leiden_scan_clear(dl);
	if (!dyn_leiden_fetch_neighbors(dl, g, u))
		return false;
	igraph_integer_t bound = refine ? dyn_leiden_vcob_of(dl, u) : -1;
	igraph_integer_t deg = igraph_vector_int_size(&dl->neis);
	for (igraph_integer_t i = 0; i < deg; i++) {
		igraph_integer_t v = VECTOR(dl->neis)[i];
		if (v == u)
			continue;
		if (refine && dyn_leiden_vcob_of(dl, v) != bound)
			continue;
		dyn_leiden_scan_add(dl, dl->vcom[v]);
	}
	return true;
}

// Per-community scan (aggregation): candidate communities are every
// community adjacent to ANY current member of c, found by enumerating c's
// exact membership via the linked list — exact (not approximated from only
// the touched subset), bounded by the size of the touched community, not V.
static bool dyn_leiden_scan_community(DynLeiden *dl, const igraph_t *g, igraph_integer_t c)
{
	dyn_leiden_scan_clear(dl);
	for (igraph_integer_t m = dl->comm_head[c]; m >= 0; m = dl->comm_next[m]) {
		if (!dyn_leiden_fetch_neighbors(dl, g, m))
			return false;
		igraph_integer_t deg = igraph_vector_int_size(&dl->neis);
		for (igraph_integer_t i = 0; i < deg; i++) {
			igraph_integer_t v = VECTOR(dl->neis)[i];
			if (v == m)
				continue;
			igraph_integer_t nc = dl->vcom[v];
			if (nc == c)
				continue; // internal edge
			dyn_leiden_scan_add(dl, nc);
		}
	}
	return true;
}

// ============================================================================
// Delta-CPM argmax (port of GVE-Leiden leidenChooseCommunity, leiden.hxx,
// adapted from modularity to CPM)
//
// igraph's CPM convention (node weight n_i=1): Q*2m = sum_c (2*w_int_c -
// gamma*N_c^2). The 1/2m factor and partition-independent constants drop out
// of the argmax/sign, so a vertex move u: d->c gives
//   dQ ~ (k_u->c - k_u->d) - gamma*(1 + n_c - n_d)
// (n_d counted with u still in d), and a community merge c->t gives
//   dQ ~ e_ct - gamma*N_c*N_t.
// Both reduce to the same formula below once "own_size" and "current label"
// are parameterized: own_size=1, d=vcom[u] for a vertex move; own_size=csiz[c],
// d=c for a merge (there vcout[d]=0 and csiz[c]-csiz[d]=0, recovering
// e_ct - gamma*N_c*N_t exactly).
// ============================================================================

static void dyn_leiden_choose(const DynLeiden *dl, igraph_integer_t own_size, igraph_integer_t d, igraph_integer_t *out_c, double *out_gain)
{
	igraph_integer_t cmax = d;
	double emax = 0.0;
	double vcout_d = dl->vcout[d]; // 0 if d was never pushed to vcs this scan
	for (igraph_integer_t i = 0; i < dl->vcs_count; i++) {
		igraph_integer_t c = dl->vcs[i];
		if (c == d)
			continue;
		double e = (dl->vcout[c] - vcout_d) - dl->resolution * (double)own_size * ((double)own_size + (double)(dl->csiz[c] - dl->csiz[d]));
		if (e > emax) {
			emax = e;
			cmax = c;
		}
	}
	*out_c = cmax;
	*out_gain = emax;
}

// ============================================================================
// Frontier marking (port of GVE-Leiden leidenAffectedVerticesFrontierW,
// leiden.hxx)
//
// A cross-community edge flags BOTH endpoints' communities for aggregation,
// not just the vertex-move frontier: local-moving only ever considers a
// single vertex switching communities wholesale, and correctly rejects that
// when the vertex has more to lose (its existing same-community edges) than
// to gain (the one new cross edge) — but the two communities AS WHOLES may
// still be worth merging (their full mutual connectivity, not just this one
// vertex's share of it). Without flagging both here, that merge would never
// even be considered when no individual vertex move happens to accept it,
// defeating the point of running aggregation at all.
// ============================================================================

static bool dyn_leiden_mark_frontier(DynLeiden *dl, const igraph_vector_int_t *new_edges)
{
	igraph_integer_t n = igraph_vector_int_size(new_edges) / 2;
	for (igraph_integer_t i = 0; i < n; i++) {
		igraph_integer_t u = VECTOR(*new_edges)[2 * i];
		igraph_integer_t v = VECTOR(*new_edges)[2 * i + 1];
		if (dl->vcom[u] == dl->vcom[v]) {
			dyn_leiden_flag_cchg(dl, dl->vcom[u]); // intra-community: bookkeeping only
		} else {
			dyn_leiden_flag_cchg(dl, dl->vcom[u]);
			dyn_leiden_flag_cchg(dl, dl->vcom[v]);
			if (!dyn_leiden_enqueue(dl, u)) // source endpoint only (frontier
				return false;				// expansion reaches v if it moves)
		}
	}
	return true;
}

// ============================================================================
// Local-moving (wave-based port of GVE-Leiden leidenMoveW, leiden.hxx;
// queue-driven, not a dense scan)
// ============================================================================

static bool dyn_leiden_local_move(DynLeiden *dl, const igraph_t *g, bool refine, igraph_vector_int_t *changed)
{
	int wave_count = 0;
	while (dl->queue_count > 0 && wave_count < DYN_LEIDEN_MAX_ITERATIONS) {
		igraph_integer_t wave_size = dl->queue_count;
		for (igraph_integer_t k = 0; k < wave_size; k++) {
			igraph_integer_t u = dyn_leiden_dequeue(dl);
			igraph_integer_t d = dl->vcom[u];
			if (refine && dl->csiz[d] > 1)
				continue; // refine only ever grows a still-singleton community
			if (!dyn_leiden_scan_vertex(dl, g, u, refine))
				return false;
			igraph_integer_t best_c;
			double best_gain;
			dyn_leiden_choose(dl, 1, d, &best_c, &best_gain);
			if (best_gain <= 0.0 || best_c == d)
				continue;
			dyn_leiden_move(dl, u, best_c, changed);
			dyn_leiden_flag_cchg(dl, d);
			dyn_leiden_flag_cchg(dl, best_c);
			if (!refine) { // frontier expansion: reference gates this on !REFINE too
				if (!dyn_leiden_fetch_neighbors(dl, g, u))
					return false;
				igraph_integer_t deg = igraph_vector_int_size(&dl->neis);
				for (igraph_integer_t i = 0; i < deg; i++) {
					igraph_integer_t nb = VECTOR(dl->neis)[i];
					if (nb != u && !dyn_leiden_enqueue(dl, nb))
						return false;
				}
			}
		}
		wave_count++;
	}
	dyn_leiden_queue_drain(dl);
	if (dl->touched_count > dl->max_frontier_size)
		dl->max_frontier_size = (int)dl->touched_count;
	return true;
}

// ============================================================================
// Refinement
//
// Reset touched vertices whose pre-move community changed back to a
// singleton, then re-run local-moving restricted to same-vcob candidates
// (dyn_leiden_scan_vertex's REFINE gate) that are still singletons
// (the csiz[d]>1 check in dyn_leiden_local_move) — finds
// well-connected sub-communities without the reference's CSR-based
// ID-renaming machinery (unneeded here: labels are already stable
// representative vertex ids, not compacted integers).
// ============================================================================

static bool dyn_leiden_refine(DynLeiden *dl, const igraph_t *g, igraph_vector_int_t *changed)
{
	for (igraph_integer_t i = 0; i < dl->touched_count; i++) {
		igraph_integer_t u = dl->touched[i];
		dl->vcob[u] = dl->vcom[u];
	}
	for (igraph_integer_t i = 0; i < dl->touched_count; i++) {
		igraph_integer_t u = dl->touched[i];
		igraph_integer_t d = dl->vcob[u];
		if (!dl->cchg[d])
			continue;
		// A community label is only safe to reclaim as u's fresh singleton
		// if nobody else currently holds it — rare id-collision guard: an
		// earlier aggregation merge could have left an unrelated vertex
		// sitting under label u (community labels persist independently of
		// their namesake vertex, see the file header). Skip refining u this
		// round rather than silently merging it with that unrelated
		// community; this only forgoes a splitting opportunity, it never
		// produces an incorrect merge.
		bool label_free = dl->comm_head[u] < 0 || (dl->comm_head[u] == u && dl->comm_next[u] < 0);
		if (!label_free)
			continue;
		dyn_leiden_move(dl, u, u, changed);
		if (!dyn_leiden_enqueue(dl, u))
			return false;
	}
	return dyn_leiden_local_move(dl, g, /*refine=*/true, changed);
}

// ============================================================================
// Aggregation — scoped to the affected region (see dyn_leiden.h header and
// the design plan for why this differs from GVE-Leiden's full-graph
// coarsen-and-recurse loop: rebuilding a supergraph from the entire current
// community structure every call would cost O(current graph size) per poll
// regardless of batch size, reintroducing the O(V)-per-frame cost this
// maintainer exists to avoid).
//
// Communities flagged cchg during local-moving/refinement are the seed
// worklist; merging one community into a neighbor is mathematically the
// same delta-CPM argmax as a vertex move, with the "vertex" being
// the whole community (own_size=csiz[c], d=c — see dyn_leiden_choose).
// A merge folds every member of the losing community into the winner via
// the same dyn_leiden_move mutator, and flags the winner for another round,
// bounded by DYN_LEIDEN_MAX_PASSES (mirrors the static Leiden
// continue_clustering loop in igraph's src/community/leiden.c, but applied
// only to the cchg-flagged candidate set instead of the whole graph — i.e.
// the scoped aggregation of Sahu's DF-Leiden rather than a full
// coarsen-and-recurse pass).
// ============================================================================

static void dyn_leiden_merge_community(DynLeiden *dl, igraph_integer_t c, igraph_integer_t target, igraph_vector_int_t *changed)
{
	igraph_integer_t m = dl->comm_head[c];
	while (m >= 0) {
		igraph_integer_t next = dl->comm_next[m];
		dyn_leiden_move(dl, m, target, changed);
		m = next;
	}
}

static bool dyn_leiden_aggregate(DynLeiden *dl, const igraph_t *g, igraph_vector_int_t *changed)
{
	for (igraph_integer_t i = 0; i < dl->cchg_count; i++) {
		igraph_integer_t c = dl->cchg_list[i];
		if (dl->comm_head[c] >= 0 && !dyn_leiden_enqueue(dl, c))
			return false;
	}

	int pass = 0;
	while (dl->queue_count > 0 && pass < DYN_LEIDEN_MAX_PASSES) {
		igraph_integer_t wave = dl->queue_count;
		for (igraph_integer_t k = 0; k < wave; k++) {
			igraph_integer_t c = dyn_leiden_dequeue(dl);
			if (dl->comm_head[c] < 0)
				continue; // emptied by an earlier merge this wave
			if (!dyn_leiden_scan_community(dl, g, c))
				return false;
			igraph_integer_t best_c;
			double best_gain;
			dyn_leiden_choose(dl, dl->csiz[c], c, &best_c, &best_gain);
			if (best_gain <= 0.0 || best_c == c)
				continue;
			dyn_leiden_merge_community(dl, c, best_c, changed);
			dyn_leiden_flag_cchg(dl, best_c);
			if (!dyn_leiden_enqueue(dl, best_c))
				return false;
		}
		pass++;
	}
	dyn_leiden_queue_drain(dl);
	if (dl->touched_count > dl->max_frontier_size)
		dl->max_frontier_size = (int)dl->touched_count;
	return true;
}

// ============================================================================
// Public API
// ============================================================================

DynLeiden *dyn_leiden_init(const igraph_t *g)
{
	DynLeiden *dl = calloc(1, sizeof(DynLeiden));
	if (!dl) {
		fprintf(stderr, "dyn_leiden_init: allocation failed\n");
		return NULL;
	}
	if (igraph_vector_int_init(&dl->neis, 0) != IGRAPH_SUCCESS) {
		free(dl);
		return NULL;
	}
	if (igraph_vector_int_init(&dl->inc, 0) != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(&dl->neis);
		free(dl);
		return NULL;
	}

	if (!dyn_leiden_sync_vcount(dl, g)) {
		dyn_leiden_destroy(dl);
		return NULL;
	}

	dl->resolution = dyn_leiden_cpm_resolution(g);

	if (dl->vcount > 0) {
		igraph_vector_int_t membership;
		if (igraph_vector_int_init(&membership, dl->vcount) != IGRAPH_SUCCESS) {
			dyn_leiden_destroy(dl);
			return NULL;
		}
		// Bootstrap under the SAME objective (CPM, density-scaled gamma)
		// that dyn_leiden_choose() maintains afterward — otherwise the
		// initial partition wouldn't even be a local optimum under the
		// formula that immediately starts adjusting it.
		igraph_int_t nb_clusters;
		igraph_error_t code = igraph_community_leiden_simple(g, NULL, IGRAPH_LEIDEN_OBJECTIVE_CPM, dl->resolution, 0.01, 0, -1, &membership, &nb_clusters, NULL);
		if (code != IGRAPH_SUCCESS) {
			fprintf(stderr, "dyn_leiden_init: igraph_community_leiden_simple failed\n");
			igraph_vector_int_destroy(&membership);
			dyn_leiden_destroy(dl);
			return NULL;
		}

		// Translate igraph's compact 0..C-1 cluster indices into our
		// representative-vertex-id labels (lowest member id per cluster),
		// then rebuild vcom/csiz/membership-list from that assignment —
		// a one-time O(V) pass, bootstrap only.
		igraph_integer_t *rep = malloc(sizeof(igraph_integer_t) * (size_t)nb_clusters);
		if (!rep) {
			fprintf(stderr, "dyn_leiden_init: allocation failed\n");
			igraph_vector_int_destroy(&membership);
			dyn_leiden_destroy(dl);
			return NULL;
		}
		for (igraph_int_t i = 0; i < nb_clusters; i++)
			rep[i] = -1;
		for (igraph_integer_t v = 0; v < dl->vcount; v++) {
			igraph_integer_t cl = VECTOR(membership)[v];
			if (rep[cl] < 0)
				rep[cl] = v; // v ascends, so the first hit is the lowest id
		}
		for (igraph_integer_t v = 0; v < dl->vcount; v++) {
			dl->comm_head[v] = -1;
			dl->comm_next[v] = -1;
			dl->comm_prev[v] = -1;
			dl->csiz[v] = 0;
		}
		for (igraph_integer_t v = 0; v < dl->vcount; v++) {
			igraph_integer_t c = rep[(igraph_integer_t)VECTOR(membership)[v]];
			dl->vcom[v] = c;
			dyn_leiden_list_insert(dl, c, v);
			dl->csiz[c]++;
		}
		free(rep);
		igraph_vector_int_destroy(&membership);
	}

	return dl;
}

bool dyn_leiden_on_edges(DynLeiden *dl, const igraph_t *g, const igraph_vector_int_t *new_edges, igraph_vector_int_t *changed)
{
	if (!dl)
		return false;
	if (!dyn_leiden_sync_vcount(dl, g))
		return false;

	dl->resolution = dyn_leiden_cpm_resolution(g); // gamma tracks live density every call (graph already contains the new edges)

	if (!new_edges || dl->vcount == 0)
		return true;

	if (!dyn_leiden_mark_frontier(dl, new_edges))
		return false;
	if (!dyn_leiden_local_move(dl, g, /*refine=*/false, changed))
		return false;
	if (!dyn_leiden_refine(dl, g, changed))
		return false;
	if (!dyn_leiden_aggregate(dl, g, changed))
		return false;

	dl->last_frontier_size = (int)dl->touched_count;
	for (igraph_integer_t i = 0; i < dl->touched_count; i++)
		dl->touched_flag[dl->touched[i]] = 0;
	dl->touched_count = 0;
	for (igraph_integer_t i = 0; i < dl->cchg_count; i++)
		dl->cchg[dl->cchg_list[i]] = 0;
	dl->cchg_count = 0;
	return true;
}

const igraph_integer_t *dyn_leiden_membership(const DynLeiden *dl)
{
	return dl ? dl->vcom : NULL;
}

igraph_integer_t dyn_leiden_get(const DynLeiden *dl, igraph_integer_t v)
{
	if (!dl || v < 0 || v >= dl->vcount)
		return v;
	return dl->vcom[v];
}

double dyn_leiden_resolution(const DynLeiden *dl)
{
	return dl ? dl->resolution : 0.0;
}

int dyn_leiden_community_count(const DynLeiden *dl)
{
	if (!dl)
		return 0;
	int count = 0;
	for (igraph_integer_t c = 0; c < dl->vcount; c++)
		if (dl->comm_head[c] >= 0)
			count++;
	return count;
}

int dyn_leiden_max_frontier_size(const DynLeiden *dl)
{
	return dl ? dl->max_frontier_size : 0;
}

int dyn_leiden_last_frontier_size(const DynLeiden *dl)
{
	return dl ? dl->last_frontier_size : 0;
}

void dyn_leiden_destroy(DynLeiden *dl)
{
	if (!dl)
		return;
	igraph_vector_int_destroy(&dl->neis);
	igraph_vector_int_destroy(&dl->inc);
	free(dl->vcom);
	free(dl->csiz);
	free(dl->comm_head);
	free(dl->comm_next);
	free(dl->comm_prev);
	free(dl->vcob);
	free(dl->touched_flag);
	free(dl->touched);
	free(dl->cchg);
	free(dl->cchg_list);
	free(dl->queue);
	free(dl->in_queue);
	free(dl->vcs);
	free(dl->vcout);
	free(dl);
}
