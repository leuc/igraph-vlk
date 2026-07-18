/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GRAPH_DYN_CORE_TREE_ORDER_H
#define GRAPH_DYN_CORE_TREE_ORDER_H

#include "graph/dyn_core_tree.h"

#include <igraph/igraph.h>
#include <stdbool.h>

/* ============================================================================
 * Heuristic crossing-reduction ORDER maintenance for the dynamic k-core tree.
 *
 * Maintains one igraph_real_t "rank" per vertex — a barycenter-style relative
 * order value meaningful only WITHIN the vertex's current DynCoreTree node
 * (never globally comparable across tree nodes). This is the incremental,
 * heuristic alternative to porting Sugiyama-style k-level crossing reduction
 * wholesale (rejected: that model is flat and dummy-vertex-based, neither of
 * which fits DynCoreTree's actual shape). Instead this exploits two
 * structural facts about DynCoreTree (dyn_core_tree.h):
 *
 * 1. Disjoint same-level tree nodes never share an edge — if two same-
 *    coreness vertices were connected, core maintenance would already have
 *    merged them into one tree node. So intra-sphere crossing reduction
 *    decomposes into one independent ordering problem PER TREE NODE.
 * 2. Every tree node has a unique parent whose induced subgraph provably
 *    contains all of the node's members' neighbors at the parent's coreness
 *    level (that is what hierarchical-core nesting means). Parent<->child is
 *    exactly the layered-sphere's adjacent-rank sphere pair (dense rank is
 *    assigned only over POPULATED levels, so a tree parent/child edge —
 *    even one that skips unpopulated levels — always lands on adjacent
 *    sphere ranks), so a node's own parent-edge scan is a cheap, already-
 *    bounded source of inter-sphere neighbor positions; no whole-sphere
 *    adjacency structure or graph-wide edge scan is needed.
 *
 * A vertex's rank is therefore a weighted barycenter of (a) the ranks of its
 * already-ranked neighbors in its OWN tree node (intra-sphere pull) and (b)
 * the ranks of its already-ranked neighbors in its tree node's PARENT
 * (inter-sphere pull) — pure order arithmetic, no physical coordinates, no
 * knowledge of spheres/Hilbert curves. graph/dyn_layered_sphere.c consumes
 * this rank as its NodePlacement.density ordering key in place of arrival
 * timestamp; that is the entire integration surface on the layout side.
 *
 * Scope (v1): a vertex's rank is computed ONCE, when it first appears
 * (mirroring dyn_layered_sphere.c's own DYN_LS_TIMESTAMP_ATTR key, which is
 * likewise fixed at arrival and never revisited) — DynCoreTree's public API
 * only reports touched LEVELS (dyn_core_tree_on_edges' touched_levels), not
 * which individual vertices moved between tree nodes, so there is no cheaper
 * way to detect "this existing vertex's ideal rank changed" without an O(V)
 * rescan; re-deriving ranks for movers is left as a documented limitation,
 * consistent with dyn_layered_sphere.c's own accepted heuristic tradeoffs
 * elsewhere (e.g. its fast local-append path skipping touched_levels
 * entirely).
 *
 * Edges more than one tree level apart (a vertex's neighbor living in a
 * grandparent-or-higher tree node, possible via DynCoreTree's own skip-layer
 * edges) are not part of the barycenter input — documented simplification,
 * not a silent gap.
 *
 * Threading: main thread only, same as dyn_core_tree.h.
 * ============================================================================ */

typedef struct DynCoreTreeOrder DynCoreTreeOrder;

/**
 * Create a maintainer bootstrapped from the current graph and its already-
 * built DynCoreTree, assigning every vertex 0..vcount-1 a rank in id order
 * (mirroring dyn_core_tree_init's own "replay" bootstrap philosophy).
 * @return New handle, or NULL on allocation failure.
 */
DynCoreTreeOrder *dyn_core_tree_order_init(const igraph_t *g, const DynCoreTree *ct);

/**
 * Advance the maintained ranks after a batch of edge insertions. The graph g
 * and DynCoreTree ct must already reflect the new state (ct's own
 * dyn_core_tree_on_edges must have already run for this batch). Only vertices
 * new since the last call get a rank assigned; existing vertices' ranks are
 * left untouched (see the v1 scope note in the file header).
 * @return false on allocation failure, true otherwise.
 */
bool dyn_core_tree_order_on_update(DynCoreTreeOrder *dto, const igraph_t *g, const DynCoreTree *ct);

/**
 * The maintained relative-order rank of vertex v, or 0.0 if dto is NULL or v
 * is out of range / not yet ranked. Only meaningful compared against another
 * vertex currently in the SAME DynCoreTree node (or that node's parent).
 */
igraph_real_t dyn_core_tree_order_rank(const DynCoreTreeOrder *dto, igraph_integer_t v);

/**
 * Free the maintainer.
 */
void dyn_core_tree_order_destroy(DynCoreTreeOrder *dto);

#endif // GRAPH_DYN_CORE_TREE_ORDER_H
