/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GRAPH_DYN_CORE_TREE_ORDER_H
#define GRAPH_DYN_CORE_TREE_ORDER_H

#include "graph/dyn_core_tree.h"

#include <igraph/igraph.h>
#include <stdbool.h>

/* Heuristic crossing-reduction order for the dynamic k-core tree.
 *
 * Maintains one igraph_real_t "rank" per vertex — a barycenter-style relative
 * order value meaningful only WITHIN the vertex's current DynCoreTree node
 * and is not comparable across tree nodes. It uses two DynCoreTree properties:
 *
 * 1. Disjoint same-level tree nodes never share an edge — if two same-
 *    coreness vertices were connected, core maintenance would already have
 *    merged them into one tree node. So intra-sphere crossing reduction
 *    decomposes into one independent ordering problem PER TREE NODE.
 * 2. Every tree node has a unique parent whose induced subgraph provably
 *    contains all of the node's members' neighbors at the parent's coreness
 *    level (that is what hierarchical-core nesting means). Parent<->child is
 *    the layered-sphere's adjacent populated-rank pair. A node's parent-edge
 *    scan therefore supplies inter-sphere neighbor positions.
 *
 * A vertex's rank is therefore a weighted barycenter of (a) the ranks of its
 * already-ranked neighbors in its OWN tree node (intra-sphere pull) and (b)
 * the ranks of its already-ranked neighbors in its tree node's PARENT
 * (inter-sphere pull). dyn_layered_sphere.c uses the result as its
 * NodePlacement.density ordering key.
 *
 * A vertex's rank is assigned only when it first appears. Existing ranks are
 * not recomputed after vertices move between tree nodes.
 *
 * Edges more than one tree level apart (a vertex's neighbor living in a
 * grandparent-or-higher tree node, possible via DynCoreTree's own skip-layer
 * edges) are excluded from the barycenter input.
 *
 * Main-thread only. */

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
