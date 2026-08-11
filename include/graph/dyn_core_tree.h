/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GRAPH_DYN_CORE_TREE_H
#define GRAPH_DYN_CORE_TREE_H

#include <igraph/igraph.h>
#include <stdbool.h>

/* Dynamic k-core hierarchy maintenance for edge insertions.
 *
 * dyn_k-core.h maintains only core(v) per vertex. This maintains the k-core
 * hierarchy itself, including nesting among k-cores, per [Lin et al.,
 * "Hierarchical Core Maintenance on Large Dynamic Graphs", PVLDB 14(5), 2021].
 * Theorems 1-6].
 *
 * Tree shape (their Def. 6): for every integer k, (i) each k-core is
 * contained by exactly one (k-1)-core, (ii) k-cores at the same k are
 * disjoint. A node at layer k holds the vertices of one connected
 * k-core with coreness == k; a tree edge from a k1-layer node to a k2-layer
 * node (k1 < k2) means the k2-core is nested inside the k1-core. It may skip
 * empty intermediate layers.
 * A single persistent root node (level 0) anchors every connected component's
 * first real layer, and also directly holds every currently-isolated
 * (coreness-0) vertex.
 *
 * Maintenance reuses dyn-k-core's touched set V*. Per inserted edge, the
 * ancestors of the two endpoints' tree nodes are merged bottom-up until they
 * coincide (Algorithm 1 lines 1-13); V* is grouped by final coreness
 * ascending; each group gets
 * a freshly created child node; existing neighbor subtrees are merged into or
 * reparented under the topmost new node (Theorem 4/5's NC set); and the
 * source node is removed (its own children reparented to its former parent)
 * if it ends up with no direct members left (Algorithm 1 lines 14-25).
 *
 * Only insertion (Algorithms 1-2) is implemented. Deletion and node splitting
 * (Algorithms 3-5) are not implemented.
 *
 * The module owns its DynKCore instance. Main-thread only; reads but does not
 * mutate the graph. */

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
 * @param touched_levels If not NULL, the LEVEL of every tree node created,
 *                    merged into, reparented, or removed while processing
 *                    this batch is appended (duplicates possible, not
 *                    deduplicated). A level is captured at the moment of the
 *                    event, so — unlike a node id — it stays correct even for
 *                    a node later removed, or whose slot is reallocated to an
 *                    unrelated node later in this same batch (the node pool
 *                    reuses freed ids immediately; a caller reading a stale
 *                    id after the call could otherwise silently observe the
 *                    wrong, reincarnated node's level).
 * @return false on unrecoverable failure (the maintainer is then stale;
 *         re-create it via dyn_core_tree_init), true otherwise.
 */
bool dyn_core_tree_on_edges(DynCoreTree *ct, const igraph_t *g, const igraph_vector_int_t *new_edges, igraph_vector_int_t *touched_levels);

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
 * Total number of live nodes, including the root.
 */
int dyn_core_tree_node_count(const DynCoreTree *ct);

/**
 * Free the maintainer (never touches the graph).
 */
void dyn_core_tree_destroy(DynCoreTree *ct);

#endif // GRAPH_DYN_CORE_TREE_H
