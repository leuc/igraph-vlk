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

/*
 * Dynamic Layered Sphere maintenance for edge insertions.
 *
 * Sphere assignment is read directly from a DynCoreTree (graph/dyn_core_tree.h)
 * instead of being approximated per recompute. Every populated coreness level
 * gets one sphere, ranked descending: the highest populated
 * level is sphere 0 (the nucleus), level 0 (the tree root: coreness-0/isolated
 * vertices) is always the outermost sphere. Multiple disjoint same-level tree
 * nodes with the same coreness share one sphere. DynLeiden membership controls
 * intra-shell grouping and ordering, not sphere assignment.
 *
 * touched_levels and community_changed mark dirty spheres for reseeding. A
 * persistent level-to-sphere mapping detects rank shifts caused by levels
 * becoming populated or empty; those shifts require a full rebuild.
 *
 * Community membership is read from DynLeiden. Coreness and hierarchy come
 * from DynCoreTree. Edge and vertex deletion are not implemented.
 *
 * Main-thread only. Reads but does not mutate the graph.
 */

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
 * through so unchanged spheres can be skipped.
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
