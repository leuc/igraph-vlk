/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GRAPH_DYN_CORE_TREE_H
#define GRAPH_DYN_CORE_TREE_H

#include <igraph/igraph.h>
#include <stdbool.h>

/* ============================================================================
 * Dynamic (streaming) k-core HIERARCHY maintenance, insertion-only.
 *
 * dyn_k-core.h maintains only core(v) per vertex. This maintains the k-core
 * HIERARCHY TREE itself — which k-cores nest inside which — incrementally
 * against the same edge stream, per [Lin, Zhang, Lin, Zhang, Tian,
 * "Hierarchical Core Maintenance on Large Dynamic Graphs", PVLDB 14(5), 2021].
 * That paper's whole premise is that per-vertex coreness maintenance (the
 * dyn_k-core.c approach) does not track the connections AMONG k-cores, which
 * their Theorems 1-6 show can be recovered cheaply from the same touched-
 * vertex set the per-vertex maintenance already computes.
 *
 * Tree shape (their Def. 6): for every integer k, (i) each k-core is
 * contained by exactly one (k-1)-core, (ii) k-cores at the same k are
 * disjoint. Node n at layer k holds exactly the vertices of one connected
 * k-core with coreness == k; a tree edge from a k1-layer node to a k2-layer
 * node (k1 < k2) means the k2-core is nested inside the k1-core, and MAY skip
 * layers if no k'-core (k1 < k' < k2) sits between them — e.g. a self-loop
 * lifting a vertex's coreness by 2 in one call produces exactly such a
 * skip-layer edge, with no vertex ever passing through the skipped layer.
 * A single persistent root node (level 0) anchors every connected component's
 * first real layer, and also directly holds every currently-isolated
 * (coreness-0) vertex.
 *
 * Maintenance reuses dyn_k-core.h's own touched-vertex output (V* in the
 * paper) as the trigger for tree updates — no separate graph traversal is
 * needed to find it, since dyn_k-core.c's subcore BFS already computes
 * exactly that set for its own peel/lift logic. Per inserted edge: the
 * ancestors of the two endpoints' tree nodes are merged bottom-up until they
 * coincide (Algorithm 1 lines 1-13); V* is grouped by final coreness
 * (ascending, generalizing the paper's fixed "K+1" to handle a multi-layer
 * jump from a self-loop as a chain of skip-layer creations); each group gets
 * a freshly created child node; existing neighbor subtrees are merged into or
 * reparented under the topmost new node (Theorem 4/5's NC set); and the
 * source node is removed (its own children reparented to its former parent)
 * if it ends up with no direct members left (Algorithm 1 lines 14-25).
 *
 * Only insertion (Algorithms 1/2 of the paper) is implemented, matching
 * dyn_k-core.h's own insertion-only scope; deletion (their Algorithms 3-5,
 * node splitting) is out of scope.
 *
 * This module is standalone: it owns its own DynKCore instance (dyn_k-core.c
 * is used unmodified, through its public API only) and does not touch
 * anything in graph/dyn_layered_sphere.c or graph/layered_sphere_common.c.
 *
 * Threading: main thread only (reads the live igraph_t; never mutates it).
 * ============================================================================ */

typedef struct DynCoreTree DynCoreTree;

// The root node's id is always 0 and it always exists (never removed), even
// when it has no direct members: it anchors every connected component's
// first real layer and holds every currently-isolated (coreness-0) vertex.
#define DYN_CORE_TREE_ROOT 0

/**
 * Create a maintainer bootstrapped from the current graph by replaying its
 * edges one at a time through the same incremental path dyn_core_tree_on_edges
 * uses (O(m) insertions into an initially empty internal state) — unlike
 * dyn_k-core_init's one-shot igraph_coreness() bootstrap, there is no
 * dedicated O(m) static hierarchy-construction algorithm here; replaying is
 * simpler and correct by construction, at the cost of amortized-only (not
 * worst-case) linearity. The graph may be empty.
 * @return New handle, or NULL on allocation/igraph failure.
 */
DynCoreTree *dyn_core_tree_init(const igraph_t *g);

/**
 * Advance the maintained hierarchy after a batch of edge insertions.
 * The graph g must ALREADY contain the new edges (and any new vertices);
 * new vertices start at coreness 0, as direct members of the root node.
 * @param new_edges  Flat vector (from0,to0,from1,to1,...) of the edges just
 *                    inserted, in insertion order; NULL means "no new edges,
 *                    just sync the vertex count".
 * @param touched_nodes If not NULL, ids of every tree node created, merged
 *                    into, reparented, or removed while processing this batch
 *                    are appended (duplicates possible, and a removed node's
 *                    id may be reused by a later dyn_core_tree_init/on_edges
 *                    call — treat entries as "this shell changed" signals for
 *                    the batch just processed, not as stable identities).
 * @return false on unrecoverable failure (the maintainer is then stale;
 *         re-create it via dyn_core_tree_init), true otherwise.
 */
bool dyn_core_tree_on_edges(DynCoreTree *ct, const igraph_t *g, const igraph_vector_int_t *new_edges, igraph_vector_int_t *touched_nodes);

/**
 * The tree node currently containing vertex v (DYN_CORE_TREE_ROOT for an
 * isolated/coreness-0 vertex, -1 if v is out of range).
 */
int dyn_core_tree_node_of(const DynCoreTree *ct, igraph_integer_t v);

/**
 * The coreness layer of a node (0 for the root). -1 if node is out of range.
 */
int dyn_core_tree_level(const DynCoreTree *ct, int node);

/**
 * The parent node id, or -1 for the root (or an out-of-range node).
 */
int dyn_core_tree_parent(const DynCoreTree *ct, int node);

/**
 * The first child node id, or -1 if node has none (or is out of range).
 * Walk a node's children via dyn_core_tree_next_sibling from this id.
 */
int dyn_core_tree_first_child(const DynCoreTree *ct, int node);

/**
 * The next sibling node id in the same parent's child list, or -1 at the end
 * (or for an out-of-range node).
 */
int dyn_core_tree_next_sibling(const DynCoreTree *ct, int node);

/**
 * Number of vertices directly contained in this node (its OWN layer's
 * members, not its descendants'). 0 for an out-of-range node.
 */
igraph_integer_t dyn_core_tree_member_count(const DynCoreTree *ct, int node);

/**
 * The first vertex directly contained in this node, or -1 if empty (or node
 * is out of range). Walk a node's members via dyn_core_tree_next_member.
 */
igraph_integer_t dyn_core_tree_first_member(const DynCoreTree *ct, int node);

/**
 * The next vertex in the same node's member list after v, or -1 at the end
 * (or if v is out of range / not currently in the tree).
 */
igraph_integer_t dyn_core_tree_next_member(const DynCoreTree *ct, igraph_integer_t v);

/**
 * Total number of currently-alive nodes (including the root). Diagnostic/test
 * use; not needed for normal traversal.
 */
int dyn_core_tree_node_count(const DynCoreTree *ct);

/**
 * Free the maintainer (never touches the graph).
 */
void dyn_core_tree_destroy(DynCoreTree *ct);

#endif // GRAPH_DYN_CORE_TREE_H
