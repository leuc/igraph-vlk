/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "graph/dyn_core_tree.h"
#include "graph/dyn_k-core.h"

#include <stdio.h>
#include <stdlib.h>

// ============================================================================
// State
//
// Nodes live in a flat, freelist-managed pool (grown by doubling, like
// dyn_k-core.c's per-vertex arrays). A dead node's `next_sibling` field is
// repurposed as the freelist link (it has no other use once removed from its
// parent's child list), avoiding a separate field. Node 0 is always the root
// and is never freed. Vertex membership is an intrusive singly-linked list
// per node, threaded through `next_in_node[vcount]`; `node_of[vcount]` maps a
// vertex to its current node.
// ============================================================================

typedef struct
{
	int level;
	int parent;
	int first_child;
	int next_sibling; // freelist link when dead
	igraph_integer_t first_member;
	igraph_integer_t member_count;
	bool alive;
} TreeNode;

struct DynCoreTree
{
	DynKCore *kc;

	TreeNode *nodes;
	int node_count; // high-water mark of the pool (alive + dead)
	int node_capacity;
	int free_head; // -1 if none

	int *node_of;					// per-vertex: current node id
	igraph_integer_t *next_in_node; // per-vertex: next vertex in the same node's member list, -1 at the end
	igraph_integer_t vcount;
	igraph_integer_t vertex_capacity;
};

// ============================================================================
// Node pool
// ============================================================================

static bool ensure_node_capacity(DynCoreTree *ct, int needed)
{
	if (needed <= ct->node_capacity)
		return true;
	int cap = ct->node_capacity ? ct->node_capacity : 16;
	while (cap < needed)
		cap *= 2;
	TreeNode *nodes = realloc(ct->nodes, sizeof(TreeNode) * (size_t)cap);
	if (!nodes) {
		fprintf(stderr, "dyn_core_tree: realloc nodes to capacity %d failed\n", cap);
		return false;
	}
	ct->nodes = nodes;
	ct->node_capacity = cap;
	return true;
}

// Allocates a node at `level` and links it as a child of `parent` (must
// already be alive; pass -1 only for the root's own creation). Returns -1 on
// allocation failure.
static int alloc_node(DynCoreTree *ct, int level, int parent)
{
	int id;
	if (ct->free_head != -1) {
		id = ct->free_head;
		ct->free_head = ct->nodes[id].next_sibling;
	} else {
		if (!ensure_node_capacity(ct, ct->node_count + 1))
			return -1;
		id = ct->node_count++;
	}
	ct->nodes[id].level = level;
	ct->nodes[id].parent = parent;
	ct->nodes[id].first_child = -1;
	ct->nodes[id].first_member = -1;
	ct->nodes[id].member_count = 0;
	ct->nodes[id].alive = true;
	if (parent >= 0) {
		ct->nodes[id].next_sibling = ct->nodes[parent].first_child;
		ct->nodes[parent].first_child = id;
	} else {
		ct->nodes[id].next_sibling = -1;
	}
	return id;
}

static void free_node(DynCoreTree *ct, int node)
{
	ct->nodes[node].alive = false;
	ct->nodes[node].next_sibling = ct->free_head;
	ct->free_head = node;
}

// Unlinks `node` from its parent's child list (O(siblings); singly-linked, no
// faster option without a second pointer per node). Leaves node's own level
// and members untouched.
static void detach_from_parent(DynCoreTree *ct, int node)
{
	int p = ct->nodes[node].parent;
	if (p < 0)
		return;
	if (ct->nodes[p].first_child == node) {
		ct->nodes[p].first_child = ct->nodes[node].next_sibling;
	} else {
		int s = ct->nodes[p].first_child;
		while (s != -1 && ct->nodes[s].next_sibling != node)
			s = ct->nodes[s].next_sibling;
		if (s != -1)
			ct->nodes[s].next_sibling = ct->nodes[node].next_sibling;
	}
	ct->nodes[node].next_sibling = -1;
	ct->nodes[node].parent = -1;
}

static void reparent_node(DynCoreTree *ct, int node, int new_parent)
{
	detach_from_parent(ct, node);
	ct->nodes[node].next_sibling = ct->nodes[new_parent].first_child;
	ct->nodes[new_parent].first_child = node;
	ct->nodes[node].parent = new_parent;
}

// Absorbs b's members and children into a, then frees b. a and b must be
// distinct alive nodes; a's level is left unchanged (callers merge only
// same-level pairs, per the hierarchy's disjointness property).
static void merge_nodes(DynCoreTree *ct, int a, int b, igraph_vector_int_t *touched)
{
	if (a == b)
		return;
	if (ct->nodes[b].member_count > 0) {
		igraph_integer_t v = ct->nodes[b].first_member;
		igraph_integer_t last = -1;
		while (v != -1) {
			ct->node_of[v] = a;
			last = v;
			v = ct->next_in_node[v];
		}
		ct->next_in_node[last] = ct->nodes[a].first_member;
		ct->nodes[a].first_member = ct->nodes[b].first_member;
		ct->nodes[a].member_count += ct->nodes[b].member_count;
	}
	if (ct->nodes[b].first_child != -1) {
		int c = ct->nodes[b].first_child;
		int last_child = c;
		ct->nodes[last_child].parent = a;
		while (ct->nodes[last_child].next_sibling != -1) {
			last_child = ct->nodes[last_child].next_sibling;
			ct->nodes[last_child].parent = a;
		}
		ct->nodes[last_child].next_sibling = ct->nodes[a].first_child;
		ct->nodes[a].first_child = c;
	}
	// b may currently be a's own sibling (both children of the same parent,
	// e.g. when a fresh bucket node and an existing same-level node both
	// hang directly off the node the ancestor-merge converged on) — it must
	// be unlinked from wherever it sits before its next_sibling field is
	// repurposed as the freelist link, or the parent's child chain corrupts.
	detach_from_parent(ct, b);
	free_node(ct, b);
	if (touched)
		igraph_vector_int_push_back(touched, b);
}

// Walks from `node` up through parent pointers until reaching `ancestor`,
// returning the last node visited before ancestor itself (i.e. the child of
// `ancestor` that is `node`'s, or `node` itself if it already equals
// ancestor). Mirrors the paper's FindSubroot/cn(n0,v0), without the
// jump-pointer cache (a single call's walk is bounded by the touched
// subtree's depth, which stays small in practice — see dyn_k-core.c's own
// max_subcore_touched metric for the equivalent bound on subcore BFS size).
static int child_of_ancestor(const DynCoreTree *ct, int node, int ancestor)
{
	if (node == ancestor)
		return ancestor;
	int child = node;
	int parent = ct->nodes[child].parent;
	while (parent != ancestor) {
		if (parent < 0)
			return -1; // defensive: ancestor was not actually an ancestor
		child = parent;
		parent = ct->nodes[child].parent;
	}
	return child;
}

// ============================================================================
// Vertex capacity
// ============================================================================

static bool ensure_vertex_capacity(DynCoreTree *ct, igraph_integer_t n)
{
	if (n <= ct->vcount)
		return true;
	if (n > ct->vertex_capacity) {
		igraph_integer_t cap = ct->vertex_capacity ? ct->vertex_capacity : 64;
		while (cap < n)
			cap *= 2;
		int *node_of = realloc(ct->node_of, sizeof(int) * (size_t)cap);
		igraph_integer_t *next_in_node = realloc(ct->next_in_node, sizeof(igraph_integer_t) * (size_t)cap);
		if (node_of)
			ct->node_of = node_of;
		if (next_in_node)
			ct->next_in_node = next_in_node;
		if (!node_of || !next_in_node) {
			fprintf(stderr, "dyn_core_tree: realloc vertex arrays to capacity %lld failed\n", (long long)cap);
			return false;
		}
		ct->vertex_capacity = cap;
	}
	// New vertices start at coreness 0: direct members of the root.
	for (igraph_integer_t v = ct->vcount; v < n; v++) {
		ct->node_of[v] = DYN_CORE_TREE_ROOT;
		ct->next_in_node[v] = ct->nodes[DYN_CORE_TREE_ROOT].first_member;
		ct->nodes[DYN_CORE_TREE_ROOT].first_member = v;
	}
	ct->nodes[DYN_CORE_TREE_ROOT].member_count += (n - ct->vcount);
	ct->vcount = n;
	return true;
}

// ============================================================================
// Ancestor merge (Algorithm 1, lines 1-13 of Lin et al.)
// ============================================================================

// Merges the ancestor chains of node1 and node2 bottom-up until they
// coincide, returning the merge point. Purely structural: independent of how
// many layers this insertion will ultimately lift V* by.
static int merge_ancestors(DynCoreTree *ct, int node1, int node2, igraph_vector_int_t *touched)
{
	int n1 = node1, n2 = node2;
	while (n1 != n2) {
		if (ct->nodes[n1].level > ct->nodes[n2].level) {
			int tmp = n1;
			n1 = n2;
			n2 = tmp;
		}
		int p1 = ct->nodes[n1].parent;
		int p2 = ct->nodes[n2].parent;
		if (ct->nodes[n1].level == ct->nodes[n2].level) {
			merge_nodes(ct, n1, n2, touched);
			n1 = p1;
			n2 = p2;
		} else {
			if (p2 >= 0 && ct->nodes[n1].level > ct->nodes[p2].level)
				reparent_node(ct, n2, n1);
			n2 = p2;
		}
	}
	return n1;
}

// ============================================================================
// Single-edge insertion (Algorithm 1, lines 14-25 of Lin et al., generalized
// to a possibly multi-layer V* — see dyn_core_tree.h's file header)
// ============================================================================

// Runs Lines 14-25 of Algorithm 1 (create bucket(s), split n_prime's member
// list, NC reparenting, remove n_prime if emptied) for ONE group of V*
// vertices that all currently reside in the same node n_prime. See
// insert_edge for why V* can require more than one such group and more than
// one call to this function.
static bool apply_group(DynCoreTree *ct, const igraph_t *g, int n_prime, const igraph_vector_int_t *gstar, const igraph_vector_int_t *gstar_level, igraph_vector_int_t *touched)
{
	igraph_integer_t n = igraph_vector_int_size(gstar);

	// 2a. Create one empty bucket node per distinct final level present in
	// this group, ascending, chained onto each other (a multi-layer jump —
	// only possible from a self-loop's double lift — becomes a chain of
	// skip-layer tree edges, which Def. 6 explicitly allows). No member is
	// placed yet: that happens in a single pass below, together with
	// splitting n_prime's own list, so next_in_node is never overwritten out
	// from under an in-progress walk of it.
	igraph_vector_int_t bucket_level, bucket_node;
	if (igraph_vector_int_init(&bucket_level, 0) != IGRAPH_SUCCESS)
		return false;
	if (igraph_vector_int_init(&bucket_node, 0) != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(&bucket_level);
		return false;
	}
	{
		int current_parent = n_prime;
		igraph_integer_t placed = 0;
		bool *done = calloc((size_t)n, sizeof(bool));
		if (!done) {
			fprintf(stderr, "dyn_core_tree: allocation failed\n");
			igraph_vector_int_destroy(&bucket_node);
			igraph_vector_int_destroy(&bucket_level);
			return false;
		}
		while (placed < n) {
			int level = -1;
			for (igraph_integer_t i = 0; i < n; i++)
				if (!done[i] && (level < 0 || (int)VECTOR(*gstar_level)[i] < level))
					level = (int)VECTOR(*gstar_level)[i];
			int new_node = alloc_node(ct, level, current_parent);
			if (new_node < 0) {
				free(done);
				igraph_vector_int_destroy(&bucket_node);
				igraph_vector_int_destroy(&bucket_level);
				return false;
			}
			igraph_vector_int_push_back(&bucket_level, level);
			igraph_vector_int_push_back(&bucket_node, new_node);
			if (touched)
				igraph_vector_int_push_back(touched, new_node);
			for (igraph_integer_t i = 0; i < n; i++)
				if (!done[i] && (int)VECTOR(*gstar_level)[i] == level) {
					done[i] = true;
					placed++;
				}
			current_parent = new_node;
		}
		free(done);
	}
	int highest_node = (int)VECTOR(bucket_node)[igraph_vector_int_size(&bucket_node) - 1];
	int highest_level = (int)VECTOR(bucket_level)[igraph_vector_int_size(&bucket_level) - 1];

	// 2b. Single pass over n_prime's ORIGINAL member list (captured before
	// any mutation below touches next_in_node), splitting it into survivors
	// (rebuilt in place) and this group's movers (placed into their bucket).
	{
		igraph_integer_t survivors_head = -1, survivors_count = 0;
		igraph_integer_t v = ct->nodes[n_prime].first_member;
		while (v != -1) {
			igraph_integer_t next = ct->next_in_node[v];

			int vi = -1;
			for (igraph_integer_t i = 0; i < n; i++)
				if (VECTOR(*gstar)[i] == v) {
					vi = (int)i;
					break;
				}
			if (vi >= 0) {
				int level = (int)VECTOR(*gstar_level)[vi];
				int target = -1;
				for (igraph_integer_t i = 0; i < igraph_vector_int_size(&bucket_level); i++)
					if ((int)VECTOR(bucket_level)[i] == level) {
						target = (int)VECTOR(bucket_node)[i];
						break;
					}
				ct->next_in_node[v] = ct->nodes[target].first_member;
				ct->nodes[target].first_member = v;
				ct->nodes[target].member_count++;
				ct->node_of[v] = target;
			} else {
				ct->next_in_node[v] = survivors_head;
				survivors_head = v;
				survivors_count++;
			}
			v = next;
		}
		ct->nodes[n_prime].first_member = survivors_head;
		ct->nodes[n_prime].member_count = survivors_count;
	}

	// 3. NC: neighbor subtrees of this group get merged into or reparented
	// under whichever new bucket node they actually nest under.
	//
	// In the paper's single-layer restriction, every child of n' trivially
	// has level >= K+1 (the tree invariant alone guarantees it, since n'
	// itself is level K), so "reparent everything under n+" is always valid.
	// That guarantee breaks once a group can span multiple levels (a
	// self-loop's double lift): n' can have OTHER pre-existing children
	// sitting at levels below our topmost new bucket — e.g. a level-3 child
	// of a level-2 n' when this call's only new bucket lands at level 4,
	// skipping level 3 entirely. Blindly reparenting such a node under the
	// level-4 bucket would put a lower-level node under a higher-level
	// parent, corrupting the tree. So each nc is routed by an exact level
	// match (merge into that bucket), left alone if it's shallower than
	// every bucket we created (it simply isn't part of this group's lift),
	// and only reparented under the topmost bucket if it's deeper than all
	// of them (mirroring the single-layer case's "push existing deeper
	// structure one level further down").
	igraph_vector_int_t nc_seen;
	if (igraph_vector_int_init(&nc_seen, 0) != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(&bucket_node);
		igraph_vector_int_destroy(&bucket_level);
		return false;
	}
	igraph_vector_int_t neis;
	if (igraph_vector_int_init(&neis, 0) != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(&nc_seen);
		igraph_vector_int_destroy(&bucket_node);
		igraph_vector_int_destroy(&bucket_level);
		return false;
	}
	for (igraph_integer_t i = 0; i < n; i++) {
		igraph_integer_t v = VECTOR(*gstar)[i];
		if (igraph_neighbors(g, &neis, v, IGRAPH_ALL, IGRAPH_LOOPS_TWICE, IGRAPH_MULTIPLE) != IGRAPH_SUCCESS) {
			igraph_vector_int_destroy(&neis);
			igraph_vector_int_destroy(&nc_seen);
			igraph_vector_int_destroy(&bucket_node);
			igraph_vector_int_destroy(&bucket_level);
			return false;
		}
		igraph_integer_t deg = igraph_vector_int_size(&neis);
		for (igraph_integer_t j = 0; j < deg; j++) {
			igraph_integer_t u = VECTOR(neis)[j];
			bool in_group = false;
			for (igraph_integer_t k = 0; k < n; k++)
				if (VECTOR(*gstar)[k] == u) {
					in_group = true;
					break;
				}
			if (in_group)
				continue;

			int nc = child_of_ancestor(ct, ct->node_of[u], n_prime);
			if (nc < 0 || nc == n_prime)
				continue; // u already resides directly in n_prime, or unrelated

			bool already = false;
			for (igraph_integer_t k = 0; k < igraph_vector_int_size(&nc_seen); k++)
				if (VECTOR(nc_seen)[k] == nc) {
					already = true;
					break;
				}
			if (already)
				continue;
			igraph_vector_int_push_back(&nc_seen, nc);

			int nc_level = ct->nodes[nc].level;
			int exact_bucket = -1;
			for (igraph_integer_t k = 0; k < igraph_vector_int_size(&bucket_level); k++)
				if ((int)VECTOR(bucket_level)[k] == nc_level) {
					exact_bucket = (int)VECTOR(bucket_node)[k];
					break;
				}
			if (exact_bucket >= 0) {
				merge_nodes(ct, exact_bucket, nc, touched);
			} else if (nc_level > highest_level) {
				reparent_node(ct, nc, highest_node);
				if (touched)
					igraph_vector_int_push_back(touched, nc);
			}
			// else: nc sits below every bucket this call created (it isn't
			// part of this group's lift at all) — leave it exactly where it
			// already validly was, as a child of n_prime.
		}
	}
	igraph_vector_int_destroy(&neis);
	igraph_vector_int_destroy(&nc_seen);
	igraph_vector_int_destroy(&bucket_node);
	igraph_vector_int_destroy(&bucket_level);

	// 4. If n_prime lost all its direct members, remove it: its remaining
	// children (whatever the NC step above didn't already move) reparent to
	// its own parent. The root is exempt — it always exists.
	if (n_prime != DYN_CORE_TREE_ROOT && ct->nodes[n_prime].member_count == 0) {
		int grandparent = ct->nodes[n_prime].parent;
		int c = ct->nodes[n_prime].first_child;
		while (c != -1) {
			int next = ct->nodes[c].next_sibling;
			reparent_node(ct, c, grandparent);
			c = next;
		}
		detach_from_parent(ct, n_prime);
		free_node(ct, n_prime);
		if (touched)
			igraph_vector_int_push_back(touched, n_prime);
	}

	return true;
}

static bool insert_edge(DynCoreTree *ct, const igraph_t *g, igraph_integer_t x1, igraph_integer_t x2, igraph_vector_int_t *touched)
{
	igraph_integer_t hi = x1 > x2 ? x1 : x2;
	if (!ensure_vertex_capacity(ct, hi + 1))
		return false;

	int node1 = ct->node_of[x1];
	int node2 = ct->node_of[x2];

	igraph_vector_int_t edge_vec, changed;
	if (igraph_vector_int_init(&edge_vec, 2) != IGRAPH_SUCCESS)
		return false;
	VECTOR(edge_vec)[0] = x1;
	VECTOR(edge_vec)[1] = x2;
	if (igraph_vector_int_init(&changed, 0) != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(&edge_vec);
		return false;
	}
	bool ok = dyn_kcore_on_edges(ct->kc, g, &edge_vec, &changed);
	igraph_vector_int_destroy(&edge_vec);
	if (!ok) {
		igraph_vector_int_destroy(&changed);
		return false;
	}

	igraph_integer_t nchanged = igraph_vector_int_size(&changed);
	if (nchanged == 0) {
		igraph_vector_int_destroy(&changed);
		return true; // coreness unaffected -> hierarchy unaffected
	}

	// Dedupe `changed` (a vertex can appear once per internal lift pass, e.g.
	// a self-loop's two passes) into a unique vertex/final-level pair list.
	// Touched sets are small (bounded by the subcore BFS size dyn_k-core.c
	// itself tracks via dyn_kcore_max_subcore_size), so an O(n^2) dedupe scan
	// is the right tradeoff over a hash set here.
	igraph_vector_int_t vstar, vstar_level;
	if (igraph_vector_int_init(&vstar, 0) != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(&changed);
		return false;
	}
	if (igraph_vector_int_init(&vstar_level, 0) != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(&vstar);
		igraph_vector_int_destroy(&changed);
		return false;
	}
	for (igraph_integer_t i = 0; i < nchanged; i++) {
		igraph_integer_t v = VECTOR(changed)[i];
		bool seen = false;
		for (igraph_integer_t j = 0; j < igraph_vector_int_size(&vstar); j++) {
			if (VECTOR(vstar)[j] == v) {
				seen = true;
				break;
			}
		}
		if (!seen) {
			igraph_vector_int_push_back(&vstar, v);
			igraph_vector_int_push_back(&vstar_level, dyn_kcore_get(ct->kc, v));
		}
	}
	igraph_vector_int_destroy(&changed);
	igraph_integer_t vstar_n = igraph_vector_int_size(&vstar);

	// 1. Merge ancestors of node(x1) and node(x2). This only needs to run for
	// its side effects (it may merge/reparent nodes strictly above V*'s own
	// layer) and is a no-op when x1==x2 (a self-loop) since there is only one
	// starting node to begin with.
	merge_ancestors(ct, node1, node2, touched);

	// Partition V* by the node each vertex CURRENTLY sits in (evaluated now,
	// after the ancestor merge above, which may have relocated some of them
	// via merge_nodes — e.g. a self-loop's second internal lift pass can pull
	// in vertices that started in a node entirely unrelated to node1/node2,
	// discovered because the first pass's survivors happened to already be
	// adjacent to that other node). The paper's own single-layer V* always
	// forms exactly one such group (Theorem 3); a multi-layer V* from a
	// self-loop's two internal passes can span more than one, each requiring
	// its own independent application of lines 14-25 against its own n' —
	// reusing a single n' for all of V* (as if Theorem 3 still held) would
	// silently drop or corrupt whichever subset didn't actually start in
	// that one node.
	igraph_vector_int_t group_source;
	if (igraph_vector_int_init(&group_source, vstar_n) != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(&vstar);
		igraph_vector_int_destroy(&vstar_level);
		return false;
	}
	for (igraph_integer_t i = 0; i < vstar_n; i++)
		VECTOR(group_source)[i] = ct->node_of[VECTOR(vstar)[i]];

	bool ok2 = true;
	igraph_vector_int_t handled;
	if (igraph_vector_int_init(&handled, 0) != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(&group_source);
		igraph_vector_int_destroy(&vstar);
		igraph_vector_int_destroy(&vstar_level);
		return false;
	}
	igraph_vector_int_t gstar, gstar_level;
	if (igraph_vector_int_init(&gstar, 0) != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(&handled);
		igraph_vector_int_destroy(&group_source);
		igraph_vector_int_destroy(&vstar);
		igraph_vector_int_destroy(&vstar_level);
		return false;
	}
	if (igraph_vector_int_init(&gstar_level, 0) != IGRAPH_SUCCESS) {
		igraph_vector_int_destroy(&gstar);
		igraph_vector_int_destroy(&handled);
		igraph_vector_int_destroy(&group_source);
		igraph_vector_int_destroy(&vstar);
		igraph_vector_int_destroy(&vstar_level);
		return false;
	}
	for (igraph_integer_t i = 0; i < vstar_n && ok2; i++) {
		int src = (int)VECTOR(group_source)[i];
		bool already = false;
		for (igraph_integer_t k = 0; k < igraph_vector_int_size(&handled); k++)
			if (VECTOR(handled)[k] == src) {
				already = true;
				break;
			}
		if (already)
			continue;
		igraph_vector_int_push_back(&handled, src);

		igraph_vector_int_clear(&gstar);
		igraph_vector_int_clear(&gstar_level);
		for (igraph_integer_t j = i; j < vstar_n; j++)
			if ((int)VECTOR(group_source)[j] == src) {
				igraph_vector_int_push_back(&gstar, VECTOR(vstar)[j]);
				igraph_vector_int_push_back(&gstar_level, VECTOR(vstar_level)[j]);
			}
		ok2 = apply_group(ct, g, src, &gstar, &gstar_level, touched);
	}
	igraph_vector_int_destroy(&gstar_level);
	igraph_vector_int_destroy(&gstar);
	igraph_vector_int_destroy(&handled);
	igraph_vector_int_destroy(&group_source);
	igraph_vector_int_destroy(&vstar);
	igraph_vector_int_destroy(&vstar_level);
	return ok2;
}

// ============================================================================
// Public API
// ============================================================================

DynCoreTree *dyn_core_tree_init(const igraph_t *g)
{
	DynCoreTree *ct = calloc(1, sizeof(DynCoreTree));
	if (!ct) {
		fprintf(stderr, "dyn_core_tree_init: allocation failed\n");
		return NULL;
	}
	ct->free_head = -1;

	igraph_t seed;
	if (igraph_empty(&seed, 0, IGRAPH_UNDIRECTED) != IGRAPH_SUCCESS) {
		free(ct);
		return NULL;
	}
	ct->kc = dyn_kcore_init(&seed);
	igraph_destroy(&seed);
	if (!ct->kc) {
		free(ct);
		return NULL;
	}

	if (!ensure_node_capacity(ct, 1)) {
		dyn_kcore_destroy(ct->kc);
		free(ct);
		return NULL;
	}
	ct->nodes[0].level = 0;
	ct->nodes[0].parent = -1;
	ct->nodes[0].first_child = -1;
	ct->nodes[0].next_sibling = -1;
	ct->nodes[0].first_member = -1;
	ct->nodes[0].member_count = 0;
	ct->nodes[0].alive = true;
	ct->node_count = 1;

	igraph_integer_t vcount = igraph_vcount(g);
	if (vcount == 0)
		return ct;
	if (!ensure_vertex_capacity(ct, vcount)) {
		dyn_core_tree_destroy(ct);
		return NULL;
	}

	igraph_integer_t ecount = igraph_ecount(g);
	if (ecount == 0)
		return ct;

	// Replay g's own edges into a scratch graph grown in lockstep, driving
	// the exact same incremental path dyn_core_tree_on_edges uses (see the
	// header comment for why there is no dedicated static-construction
	// algorithm here).
	igraph_t scratch;
	if (igraph_empty(&scratch, 0, IGRAPH_UNDIRECTED) != IGRAPH_SUCCESS) {
		dyn_core_tree_destroy(ct);
		return NULL;
	}
	if (igraph_add_vertices(&scratch, vcount, NULL) != IGRAPH_SUCCESS) {
		igraph_destroy(&scratch);
		dyn_core_tree_destroy(ct);
		return NULL;
	}
	for (igraph_integer_t eid = 0; eid < ecount; eid++) {
		igraph_integer_t from, to;
		if (igraph_edge(g, eid, &from, &to) != IGRAPH_SUCCESS) {
			igraph_destroy(&scratch);
			dyn_core_tree_destroy(ct);
			return NULL;
		}
		igraph_vector_int_t one;
		if (igraph_vector_int_init(&one, 2) != IGRAPH_SUCCESS) {
			igraph_destroy(&scratch);
			dyn_core_tree_destroy(ct);
			return NULL;
		}
		VECTOR(one)[0] = from;
		VECTOR(one)[1] = to;
		bool ok = igraph_add_edges(&scratch, &one, NULL) == IGRAPH_SUCCESS;
		igraph_vector_int_destroy(&one);
		if (!ok || !insert_edge(ct, &scratch, from, to, NULL)) {
			igraph_destroy(&scratch);
			dyn_core_tree_destroy(ct);
			return NULL;
		}
	}
	igraph_destroy(&scratch);
	return ct;
}

bool dyn_core_tree_on_edges(DynCoreTree *ct, const igraph_t *g, const igraph_vector_int_t *new_edges, igraph_vector_int_t *touched_nodes)
{
	if (!ct)
		return false;
	if (!ensure_vertex_capacity(ct, igraph_vcount(g)))
		return false;
	if (!new_edges)
		return true;

	igraph_integer_t n = igraph_vector_int_size(new_edges) / 2;
	igraph_integer_t vcount = igraph_vcount(g);
	for (igraph_integer_t i = 0; i < n; i++) {
		igraph_integer_t u = VECTOR(*new_edges)[2 * i];
		igraph_integer_t v = VECTOR(*new_edges)[2 * i + 1];
		if (u < 0 || u >= vcount || v < 0 || v >= vcount) {
			fprintf(stderr, "dyn_core_tree: edge (%lld,%lld) out of range, skipping\n", (long long)u, (long long)v);
			continue;
		}
		if (!insert_edge(ct, g, u, v, touched_nodes))
			return false;
	}
	return true;
}

int dyn_core_tree_node_of(const DynCoreTree *ct, igraph_integer_t v)
{
	if (!ct || v < 0 || v >= ct->vcount)
		return -1;
	return ct->node_of[v];
}

int dyn_core_tree_level(const DynCoreTree *ct, int node)
{
	if (!ct || node < 0 || node >= ct->node_count || !ct->nodes[node].alive)
		return -1;
	return ct->nodes[node].level;
}

int dyn_core_tree_parent(const DynCoreTree *ct, int node)
{
	if (!ct || node < 0 || node >= ct->node_count || !ct->nodes[node].alive)
		return -1;
	return ct->nodes[node].parent;
}

int dyn_core_tree_first_child(const DynCoreTree *ct, int node)
{
	if (!ct || node < 0 || node >= ct->node_count || !ct->nodes[node].alive)
		return -1;
	return ct->nodes[node].first_child;
}

int dyn_core_tree_next_sibling(const DynCoreTree *ct, int node)
{
	if (!ct || node < 0 || node >= ct->node_count || !ct->nodes[node].alive)
		return -1;
	return ct->nodes[node].next_sibling;
}

igraph_integer_t dyn_core_tree_member_count(const DynCoreTree *ct, int node)
{
	if (!ct || node < 0 || node >= ct->node_count || !ct->nodes[node].alive)
		return 0;
	return ct->nodes[node].member_count;
}

igraph_integer_t dyn_core_tree_first_member(const DynCoreTree *ct, int node)
{
	if (!ct || node < 0 || node >= ct->node_count || !ct->nodes[node].alive)
		return -1;
	return ct->nodes[node].first_member;
}

igraph_integer_t dyn_core_tree_next_member(const DynCoreTree *ct, igraph_integer_t v)
{
	if (!ct || v < 0 || v >= ct->vcount)
		return -1;
	return ct->next_in_node[v];
}

int dyn_core_tree_node_count(const DynCoreTree *ct)
{
	return ct ? ct->node_count : 0;
}

void dyn_core_tree_destroy(DynCoreTree *ct)
{
	if (!ct)
		return;
	dyn_kcore_destroy(ct->kc);
	free(ct->nodes);
	free(ct->node_of);
	free(ct->next_in_node);
	free(ct);
}
