/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GRAPH_DYN_LS_SPHERE_ROTATION_H
#define GRAPH_DYN_LS_SPHERE_ROTATION_H

#include "graph/layered_sphere_common.h"

#include <igraph/igraph.h>
#include <stdbool.h>

/*
 * Per-sphere rigid rotation for dynamic Layered Sphere, adapted
 * from TopoLayout's torque-based crossing reduction (Archambault, Munzner,
 * Auber, "TopoLayout: Multi-Level Graph Layout by Topological Features",
 * IEEE TVCG 13(2), 2007, Section III-B.5) to 3D and to a streaming setting.
 *
 * TopoLayout rotates a 2D meta-node about its perpendicular axis using
 * `t = (pi/2) * sign(r x f) * (r . f)`, averaged over incident edges. Here a
 * meta-node is a sphere, so rotation uses a 3D axis-angle. For each inter-sphere
 * edge (u in sphere s, neighbor n outside s), r_hat = normalize(position of
 * u), f_hat = normalize(position of n - position of u), and the edge's
 * contribution is `axis * angle` where `axis = normalize(r_hat x f_hat)` and
 * `angle = asin(|r_hat x f_hat|)`. Averaged contributions form Omega_want.
 * Each call applies one GEM-damped, clamped quaternion increment and
 * renormalizes the persistent quaternion.
 *
 * DynLayeredSphere owns the rotation arrays. Rotation changes coordinates only;
 * it does not alter SphereGrid occupancy or Hilbert ordering.
 *
 * Main-thread only.
 */

// Quaternions use (w, x, y, z). Arrays are indexed by stable grid slot, not
// sphere rank, and grow to at least needed entries.
bool dyn_ls_rotation_ensure_capacity(double **sphere_rotation, double **sphere_prev_omega, int **sphere_rotation_steps, int **sphere_settled_streak, int *capacity, int needed);

// Reset a newly populated grid slot. Preserve state across overflow rebuilds
// and rank-shift rescaling because the slot still represents the same sphere.
void dyn_ls_rotation_reset(double *sphere_rotation, double *sphere_prev_omega, int *sphere_rotation_steps, int *sphere_settled_streak, int s);

// Computes and applies one damped rotation increment to sphere s's
// persistent quaternion, from the net torque of its inter-sphere edges (see
// file header). Reads sphere s's own members' CURRENT layout positions (call
// after ctx's LayeredSphereContext.sphere_rotation has already been applied
// to s's freshly-seeded occupants this recompute) and any neighbor spheres'
// current positions as fixed anchors. Mutates sphere_rotation[4*s..] and
// sphere_prev_omega[3*s..], and increments sphere_rotation_steps[s] iff a
// nonzero rotation was actually applied. Does NOT itself re-write any node's
// layout position — call dyn_ls_apply_sphere_rotation afterward to make the
// updated quaternion visible. Returns true iff the quaternion was actually
// changed (i.e. sphere_rotation_steps[s] was incremented) this call.
// sphere_settled_streak counts consecutive ticks with stable torque. At the
// configured limit, rotation pauses until a disturbance resets the streak.
bool dyn_ls_rotate_sphere_step(const igraph_t *g, const LayeredSphereContext *ctx, int s, double *sphere_rotation, double *sphere_prev_omega, int *sphere_rotation_steps, int *sphere_settled_streak);

// Re-walks sphere s's occupied slots and re-writes each occupant's layout
// position via write_slot_position under sphere_rotation[4*s..]'s current
// value. O(occupancy of s); never touches slot_occupant/grid geometry.
void dyn_ls_apply_sphere_rotation(LayeredSphereContext *ctx, int s, const double *sphere_rotation);

#endif // GRAPH_DYN_LS_SPHERE_ROTATION_H
