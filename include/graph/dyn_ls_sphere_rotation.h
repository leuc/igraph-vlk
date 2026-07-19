/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GRAPH_DYN_LS_SPHERE_ROTATION_H
#define GRAPH_DYN_LS_SPHERE_ROTATION_H

#include "graph/layered_sphere_common.h"

#include <igraph/igraph.h>
#include <stdbool.h>

/* ============================================================================
 * Per-sphere rigid rotation for the dynamic Layered Sphere layout, adapted
 * from TopoLayout's torque-based crossing reduction (Archambault, Munzner,
 * Auber, "TopoLayout: Multi-Level Graph Layout by Topological Features",
 * IEEE TVCG 13(2), 2007, Section III-B.5) to 3D and to a streaming setting.
 *
 * TopoLayout rotates a 2D meta-node about the single axis perpendicular to
 * its embedding plane, driven by a bounded (+-pi/2) signed torque quantity
 * `t = (pi/2) * sign(r x f) * (r . f)` averaged over every edge connecting
 * that meta-node to its neighbors. Here a "meta-node" is a whole sphere
 * (a concentric shell centered at the origin), so rotation is a full 3D
 * axis-angle rotation rather than a single scalar angle: per inter-sphere
 * edge (u in sphere s, neighbor n outside s), r_hat = normalize(position of
 * u), f_hat = normalize(position of n - position of u), and the edge's
 * contribution is `axis * angle` where `axis = normalize(r_hat x f_hat)` and
 * `angle = asin(|r_hat x f_hat|)` — using the angle itself (not the raw cross
 * product, whose magnitude is sin(angle)) is what makes this a faithful 3D
 * generalization of TopoLayout's already-angular `t`. Contributions are
 * averaged over sphere s's inter-sphere edges (discovered via
 * igraph_incident + LayeredSphereContext.node_to_sphere_id, no precomputed
 * inter-sphere adjacency structure exists or is needed) into a rotation
 * vector Omega_want; a streaming recompute gets exactly one step per call
 * (no multi-iteration convergence loop the way TopoLayout's own iterative
 * meta-node relaxation or this codebase's batch layered_sphere.c's dropped
 * PHASE_INTER_SPHERE do), damped GEM-style (if Omega_want has reversed
 * direction from the previous step, its applied magnitude is scaled down,
 * growing back toward full strength as the sphere's own rotation step count
 * grows) and clamped to a fixed max step angle, then composed as an
 * incremental quaternion onto the sphere's persistent rotation state,
 * renormalized every time to correct for floating-point drift.
 *
 * Rotation state lives on the caller (DynLayeredSphere, dyn_layered_sphere.c)
 * as three flat arrays indexed by sphere rank, grown via
 * dyn_ls_rotation_ensure_capacity mirroring dyn_layered_sphere.c's own
 * doubling-realloc arrays. Applying a sphere's rotation to its members'
 * layout positions never touches SphereGrid slot/occupancy geometry — it is
 * purely a coordinate transform composed in at write_slot_position time via
 * LayeredSphereContext.sphere_rotation (layered_sphere_common.h), so
 * rotating a sphere never triggers a reseed, grid rebuild, or Hilbert
 * reordering.
 *
 * Threading: main thread only, same as the rest of graph/dyn_layered_sphere.c.
 * ============================================================================ */

// Quaternion layout throughout: 4 doubles per sphere, (w, x, y, z), identity
// = {1,0,0,0}. sphere_rotation/sphere_prev_omega/sphere_rotation_steps are
// all indexed by dyn_layered_sphere.c's stable per-level grid slot id (not
// rank — a sphere's rank can shift without its rotation state moving) and
// must have at least `needed` spheres' worth of capacity; grows (or
// allocates, if *sphere_rotation is NULL) via the same doubling pattern as
// dyn_layered_sphere.c's own arrays.
bool dyn_ls_rotation_ensure_capacity(double **sphere_rotation, double **sphere_prev_omega, int **sphere_rotation_steps, int *capacity, int needed);

// Resets sphere (grid slot) s's rotation state to identity/zero — call
// whenever dyn_layered_sphere.c (re)builds that slot's grid from scratch
// (its level populated for the first time, or its existing grid overflowed),
// since the slot's geometry — and therefore any prior rotation's meaning —
// no longer applies. A slot whose grid was NOT rebuilt (e.g. a pure rank
// shift, handled as an in-place radius rescale) keeps its rotation state
// untouched.
void dyn_ls_rotation_reset(double *sphere_rotation, double *sphere_prev_omega, int *sphere_rotation_steps, int s);

// Computes and applies one damped rotation increment to sphere s's
// persistent quaternion, from the net torque of its inter-sphere edges (see
// file header). Reads sphere s's own members' CURRENT layout positions (call
// after ctx's LayeredSphereContext.sphere_rotation has already been applied
// to s's freshly-seeded occupants this recompute) and any neighbor spheres'
// current positions as fixed anchors. Mutates sphere_rotation[4*s..] and
// sphere_prev_omega[3*s..], and increments sphere_rotation_steps[s] iff a
// nonzero rotation was actually applied. Does NOT itself re-write any node's
// layout position — call dyn_ls_apply_sphere_rotation afterward to make the
// updated quaternion visible.
void dyn_ls_rotate_sphere_step(const igraph_t *g, const LayeredSphereContext *ctx, int s, double *sphere_rotation, double *sphere_prev_omega, int *sphere_rotation_steps);

// Re-walks sphere s's occupied slots and re-writes each occupant's layout
// position via write_slot_position under sphere_rotation[4*s..]'s current
// value. O(occupancy of s); never touches slot_occupant/grid geometry.
void dyn_ls_apply_sphere_rotation(LayeredSphereContext *ctx, int s, const double *sphere_rotation);

#endif // GRAPH_DYN_LS_SPHERE_ROTATION_H
