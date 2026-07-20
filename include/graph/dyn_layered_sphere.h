/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GRAPH_DYN_LAYERED_SPHERE_H
#define GRAPH_DYN_LAYERED_SPHERE_H

#include "graph/dyn_core_tree.h"
#include "graph/dyn_core_tree_order.h"

#include <igraph/igraph.h>
#include <stdbool.h>

/* ============================================================================
 * Dynamic (streaming) Layered Sphere layout maintenance, insertion-only.
 *
 * Sphere assignment is read directly from a DynCoreTree (graph/dyn_core_tree.h)
 * instead of being approximated per recompute: every currently-populated
 * coreness LEVEL in the tree gets exactly one sphere (strict 1:1, no capacity-
 * based packing of adjacent levels), ranked DESCENDING — the highest populated
 * level is sphere 0 (the nucleus), level 0 (the tree root: coreness-0/isolated
 * vertices) is always the outermost sphere. Multiple disjoint same-level tree
 * nodes (unconnected components with the same coreness) still share one
 * sphere, preserving "shell = degree of centrality" rather than switching to
 * a connectivity-based grouping. DynLeiden community membership is the
 * intra-shell grouping/ordering key (unchanged in spirit from before), not
 * the shell-assignment key.
 *
 * Change detection is exact, not approximated: dyn_core_tree_on_edges'
 * touched_levels output says precisely which spheres' populations moved
 * (radial change), and DynLeiden's own community_changed output says which
 * vertices' community label moved without necessarily changing coreness
 * (angular-only change) — both are unioned into a per-sphere dirty flag, and
 * only dirty spheres are cleared and re-seeded. A separate persistent
 * per-sphere level record detects the rarer case where the level-to-sphere-
 * index RANKING itself shifts (a previously-unpopulated level becomes
 * populated, or vice versa, changing every affected level's rank even when no
 * single level's own membership actually moved) and forces a full rebuild
 * when it does, since a sphere index can then represent a completely
 * different level's population than it did last time.
 *
 * Community membership itself is still read live from DynLeiden
 * (graph/dyn_leiden.h) via O(1) per-vertex lookups; only coreness/hierarchy
 * information comes from the tree now.
 *
 * Edge/vertex deletion is out of scope (the stream is insertion-only,
 * matching dyn_core_tree.h and dyn_leiden.h).
 *
 * Threading: main thread only (reads the live igraph_t; never mutates it).
 * ============================================================================ */

typedef struct DynLayeredSphere DynLayeredSphere;

/**
 * Create a maintainer and run the first bucketing+placement pass (the graph
 * may be empty).
 * @param ct        Live k-core hierarchy (e.g. from dyn_core_tree_init()).
 * @param order     Live crossing-reduction rank per vertex (e.g. from
 *                  dyn_core_tree_order_init()), used as the intra-sphere
 *                  ordering key in place of arrival timestamp; NULL falls
 *                  back to timestamp/vertex-id ordering as before.
 * @param community Live per-vertex community membership, ids are
 *                  representative vertex ids (e.g. dyn_leiden_membership()).
 * @param layout    Caller-owned matrix to write positions into; must already
 *                  be sized igraph_vcount(g) x 3 (or more).
 * @return New handle, or NULL on allocation/igraph failure.
 */
DynLayeredSphere *dyn_layered_sphere_init(const igraph_t *g, const DynCoreTree *ct, const DynCoreTreeOrder *order, const igraph_integer_t *community, igraph_matrix_t *layout);

/**
 * Advance the layout after a batch of edge insertions. Call after
 * dyn_core_tree_on_edges/dyn_leiden_on_edges have been advanced for the same
 * batch, passing their touched_levels/community_changed output straight
 * through — that is what lets this call skip spheres that provably didn't
 * change instead of re-deriving everything from scratch.
 * @param touched_levels  touched_levels output from this batch's
 *                        dyn_core_tree_on_edges call (may be NULL/empty if
 *                        nothing coreness-related changed).
 * @param order           Live crossing-reduction rank per vertex, already
 *                        advanced for this batch (e.g. via
 *                        dyn_core_tree_order_on_update()); NULL falls back
 *                        to timestamp/vertex-id ordering as before.
 * @param community_changed changed-vertex output from this batch's
 *                        dyn_leiden_on_edges call (may be NULL/empty if no
 *                        community reassignment happened).
 * @param layout          Caller-owned matrix to write positions into; grown
 *                        by the caller to igraph_vcount(g) x 3 beforehand.
 * @param out_changed     Set to true iff any node position actually changed
 *                        this call (reseed, new vertex placement, or a
 *                        sphere rotation step that wasn't a no-op/settled
 *                        tick); false on a fully quiescent call. May be NULL
 *                        if the caller doesn't need it.
 * @return false on unrecoverable failure (the maintainer is then stale;
 *         re-create it via dyn_layered_sphere_init), true otherwise.
 */
bool dyn_layered_sphere_on_update(DynLayeredSphere *dls, const igraph_t *g, const DynCoreTree *ct, const igraph_vector_int_t *touched_levels, const DynCoreTreeOrder *order, const igraph_integer_t *community, const igraph_vector_int_t *community_changed, igraph_matrix_t *layout, bool *out_changed);

/**
 * Free the maintainer (never touches the graph or the layout matrix).
 */
void dyn_layered_sphere_destroy(DynLayeredSphere *dls);

#endif // GRAPH_DYN_LAYERED_SPHERE_H
