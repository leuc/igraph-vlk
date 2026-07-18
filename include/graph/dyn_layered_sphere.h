/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GRAPH_DYN_LAYERED_SPHERE_H
#define GRAPH_DYN_LAYERED_SPHERE_H

#include <igraph/igraph.h>
#include <stdbool.h>

/* ============================================================================
 * Dynamic (streaming) Layered Sphere layout maintenance, insertion-only.
 *
 * Mirrors the static/batch Layered Sphere layout's PHASE_INIT bucketing
 * (src/graph/layered_sphere.c) exactly: communities are sorted by average
 * coreness, greedily bucketed onto concentric spheres (sphere 0 = the
 * nucleus, sized to the largest/densest community; further spheres opened
 * once the current one's capacity is exceeded), and each sphere gets its own
 * Hilbert-curve slot grid (src/graph/layered_sphere_common.c) with members
 * seeded evenly-spaced in (community, timestamp) order — coreness only
 * decides which sphere a community lands on, not a member's position within
 * it. The timestamp is read as a real "timestamp" igraph vertex attribute
 * (set in graph/stream.c's ensure_vertex from wall-clock arrival time,
 * falling back to vertex id if the attribute isn't present at all), so that
 * same-community members stay in a stable relative order across recomputes
 * instead of reshuffling arbitrarily. This module never computes coreness
 * or community membership itself — it only consumes the
 * live arrays maintained by DynKCore/DynLeiden (graph/dyn_k-core.h,
 * graph/dyn_leiden.h), exactly the way graph/stream.c already drives those
 * two, so bucketing/placement stays O(1)-per-vertex-lookup cheap.
 *
 * Unlike the batch algorithm, there is no iterative relaxation phase
 * (PHASE_INTRA_SPHERE/PHASE_INTER_SPHERE) — placement is pure sort-and-
 * bucket. Because deriving coreness/community is cheap, the entire
 * bucketing+placement pass reruns from scratch on every
 * dyn_layered_sphere_on_update/_init call rather than being maintained
 * incrementally: an intentional full O(V + C log C) recompute per call, not
 * O(touched) like DynKCore/DynLeiden. Vertices can move to a different
 * sphere/slot on every call as a result.
 *
 * Edge/vertex deletion is out of scope (the stream is insertion-only,
 * matching dyn_k-core.h and dyn_leiden.h).
 *
 * Threading: main thread only (reads the live igraph_t; never mutates it).
 * ============================================================================ */

typedef struct DynLayeredSphere DynLayeredSphere;

/**
 * Create a maintainer and run the first bucketing+placement pass (the graph
 * may be empty).
 * @param coreness  Live per-vertex coreness (e.g. dyn_kcore_values()).
 * @param community Live per-vertex community membership, ids are
 *                  representative vertex ids (e.g. dyn_leiden_membership()).
 * @param layout    Caller-owned matrix to write positions into; must already
 *                  be sized igraph_vcount(g) x 3 (or more).
 * @return New handle, or NULL on allocation/igraph failure.
 */
DynLayeredSphere *dyn_layered_sphere_init(const igraph_t *g, const int *coreness, const igraph_integer_t *community, igraph_matrix_t *layout);

/**
 * Rerun the full bucketing+placement pass against the current graph/
 * coreness/community state. Call after dyn_kcore_on_edges/
 * dyn_leiden_on_edges have been advanced for the same batch — every vertex
 * is repositioned every call, so no "changed vertices" list is needed.
 * @param layout Caller-owned matrix to write positions into; grown by the
 *               caller to igraph_vcount(g) x 3 beforehand.
 * @return false on unrecoverable failure (the maintainer is then stale;
 *         re-create it via dyn_layered_sphere_init), true otherwise.
 */
bool dyn_layered_sphere_on_update(DynLayeredSphere *dls, const igraph_t *g, const int *coreness, const igraph_integer_t *community, igraph_matrix_t *layout);

/**
 * Free the maintainer (never touches the graph or the layout matrix).
 */
void dyn_layered_sphere_destroy(DynLayeredSphere *dls);

#endif // GRAPH_DYN_LAYERED_SPHERE_H
