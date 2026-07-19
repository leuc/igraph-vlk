/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "graph/dyn_ls_sphere_rotation.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define DYN_LS_ROTATION_DAMP_HORIZON 20.0		// sphere_rotation_steps[s] reaching this many applied-rotation events is treated as fully warmed up, saturating the damping factor at 1.0
#define DYN_LS_ROTATION_MAX_STEP_RAD 0.15		// angular-step budget at DYN_LS_ROTATION_REFERENCE_RADIUS; a streaming recompute gets one shot rather than an iterative convergence loop
#define DYN_LS_ROTATION_REFERENCE_RADIUS 5.0	// sphere_radius_for()'s floor, i.e. the innermost/smallest sphere's radius
#define DYN_LS_ROTATION_SETTLED_DELTA_RAD 0.001 // tick-to-tick CHANGE in torque (want vector) below this means the torque has converged to a steady value; observed in practice: some spheres' torque plateaus at a stable NONZERO value (e.g. structurally asymmetric edge layout with no exact zero-torque orientation) rather than shrinking to 0, so magnitude alone can't detect convergence — only whether it has stopped moving can
#define DYN_LS_ROTATION_SETTLED_STREAK 8		// consecutive below-delta ticks required before a sphere is treated as settled, so one lucky small-delta sample mid-correction doesn't false-trigger

// out = a * b (Hamilton product); a is the rotation applied SECOND to a
// vector already rotated by b (i.e. combined = a . b applies b then a).
static void quat_multiply(const double a[4], const double b[4], double out[4])
{
	out[0] = a[0] * b[0] - a[1] * b[1] - a[2] * b[2] - a[3] * b[3];
	out[1] = a[0] * b[1] + a[1] * b[0] + a[2] * b[3] - a[3] * b[2];
	out[2] = a[0] * b[2] - a[1] * b[3] + a[2] * b[0] + a[3] * b[1];
	out[3] = a[0] * b[3] + a[1] * b[2] - a[2] * b[1] + a[3] * b[0];
}

static void quat_normalize(double q[4])
{
	double len = sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
	if (len < 1e-12) {
		q[0] = 1.0;
		q[1] = q[2] = q[3] = 0.0;
		return;
	}
	q[0] /= len;
	q[1] /= len;
	q[2] /= len;
	q[3] /= len;
}

void dyn_ls_rotation_reset(double *sphere_rotation, double *sphere_prev_omega, int *sphere_rotation_steps, int *sphere_settled_streak, int s)
{
	sphere_rotation[4 * s + 0] = 1.0;
	sphere_rotation[4 * s + 1] = 0.0;
	sphere_rotation[4 * s + 2] = 0.0;
	sphere_rotation[4 * s + 3] = 0.0;
	sphere_prev_omega[3 * s + 0] = 0.0;
	sphere_prev_omega[3 * s + 1] = 0.0;
	sphere_prev_omega[3 * s + 2] = 0.0;
	sphere_rotation_steps[s] = 0;
	sphere_settled_streak[s] = 0;
}

bool dyn_ls_rotation_ensure_capacity(double **sphere_rotation, double **sphere_prev_omega, int **sphere_rotation_steps, int **sphere_settled_streak, int *capacity, int needed)
{
	if (needed <= *capacity)
		return true;
	int cap = *capacity ? *capacity : 4;
	while (cap < needed)
		cap *= 2;

	double *rot_grown = realloc(*sphere_rotation, sizeof(double) * 4 * (size_t)cap);
	double *prev = realloc(*sphere_prev_omega, sizeof(double) * 3 * (size_t)cap);
	int *steps = realloc(*sphere_rotation_steps, sizeof(int) * (size_t)cap);
	int *streak = realloc(*sphere_settled_streak, sizeof(int) * (size_t)cap);
	if (rot_grown)
		*sphere_rotation = rot_grown;
	if (prev)
		*sphere_prev_omega = prev;
	if (steps)
		*sphere_rotation_steps = steps;
	if (streak)
		*sphere_settled_streak = streak;
	if (!rot_grown || !prev || !steps || !streak) {
		fprintf(stderr, "dyn_ls_sphere_rotation: realloc rotation state to capacity %d failed\n", cap);
		return false;
	}

	for (int s = *capacity; s < cap; s++)
		dyn_ls_rotation_reset(*sphere_rotation, *sphere_prev_omega, *sphere_rotation_steps, *sphere_settled_streak, s);
	*capacity = cap;
	return true;
}

void dyn_ls_rotate_sphere_step(const igraph_t *g, const LayeredSphereContext *ctx, int s, double *sphere_rotation, double *sphere_prev_omega, int *sphere_rotation_steps, int *sphere_settled_streak)
{
	const SphereGrid *grid = &ctx->grids[s];
	double sum_x = 0.0, sum_y = 0.0, sum_z = 0.0;
	int edge_count = 0;

	igraph_vector_int_t neis;
	if (igraph_vector_int_init(&neis, 0) != IGRAPH_SUCCESS)
		return;

	for (int k = 0; k < grid->max_slots; k++) {
		int u = grid->slot_occupant[k];
		if (u < 0 || u >= ctx->vcount)
			continue;

		double ux = MATRIX(*ctx->layout, u, 0);
		double uy = MATRIX(*ctx->layout, u, 1);
		double uz = MATRIX(*ctx->layout, u, 2);
		double ulen = sqrt(ux * ux + uy * uy + uz * uz);
		if (ulen < 1e-9)
			continue; // node sits at the sphere's center, no meaningful radius vector
		double rx = ux / ulen, ry = uy / ulen, rz = uz / ulen;

		if (igraph_incident(g, &neis, u, IGRAPH_ALL, IGRAPH_NO_LOOPS) != IGRAPH_SUCCESS)
			continue;
		igraph_integer_t m = igraph_vector_int_size(&neis);
		for (igraph_integer_t i = 0; i < m; i++) {
			igraph_integer_t from, to;
			if (igraph_edge(g, VECTOR(neis)[i], &from, &to) != IGRAPH_SUCCESS)
				continue;
			igraph_integer_t n = (from == u) ? to : from;
			if (n < 0 || n >= ctx->vcount || ctx->node_to_sphere_id[n] == s)
				continue; // intra-sphere edge — not this phase's concern

			double fx = MATRIX(*ctx->layout, n, 0) - ux;
			double fy = MATRIX(*ctx->layout, n, 1) - uy;
			double fz = MATRIX(*ctx->layout, n, 2) - uz;
			double flen = sqrt(fx * fx + fy * fy + fz * fz);
			if (flen < 1e-9)
				continue;
			fx /= flen;
			fy /= flen;
			fz /= flen;

			double cx = ry * fz - rz * fy;
			double cy = rz * fx - rx * fz;
			double cz = rx * fy - ry * fx;
			double sin_angle = sqrt(cx * cx + cy * cy + cz * cz);
			if (sin_angle < 1e-9)
				continue; // r and f (anti)parallel — no rotational signal

			// (cx,cy,cz) has magnitude sin(angle); rescale by angle/sin(angle)
			// to get axis*angle directly, the 3D generalization of TopoLayout's
			// already-angular torque t (see file header) rather than the raw
			// cross product (whose magnitude is sin(angle), not angle).
			double angle = asin(sin_angle > 1.0 ? 1.0 : sin_angle);
			double scale = angle / sin_angle;
			sum_x += cx * scale;
			sum_y += cy * scale;
			sum_z += cz * scale;
			edge_count++;
		}
	}
	igraph_vector_int_destroy(&neis);

	if (edge_count == 0)
		return;

	double want_x = sum_x / edge_count;
	double want_y = sum_y / edge_count;
	double want_z = sum_z / edge_count;
	double want_angle = sqrt(want_x * want_x + want_y * want_y + want_z * want_z);

	double *prev = &sphere_prev_omega[3 * s];
	double delta_x = want_x - prev[0], delta_y = want_y - prev[1], delta_z = want_z - prev[2];
	double settle_delta = sqrt(delta_x * delta_x + delta_y * delta_y + delta_z * delta_z);

	if (settle_delta < DYN_LS_ROTATION_SETTLED_DELTA_RAD) {
		if (sphere_settled_streak[s] < DYN_LS_ROTATION_SETTLED_STREAK)
			sphere_settled_streak[s]++;
	} else {
		sphere_settled_streak[s] = 0;
	}
	if (sphere_settled_streak[s] >= DYN_LS_ROTATION_SETTLED_STREAK) {
		// Settled: stop touching the quaternion (and sphere_rotation_steps) until a fresh
		// disturbance pushes the torque somewhere new (settle_delta back above threshold), which
		// resets the streak above and resumes rotation next tick. Still track the baseline
		// direction so the first post-settlement tick's delta/oscillation checks compare against
		// something recent, not stale.
		prev[0] = want_x;
		prev[1] = want_y;
		prev[2] = want_z;
		return;
	}

	double dot = want_x * prev[0] + want_y * prev[1] + want_z * prev[2];

	double apply_x = want_x, apply_y = want_y, apply_z = want_z;
	double damp = 1.0; // reported as-is in the diagnostic below; 1.0 means "not oscillating, undamped"
	if (dot < 0.0) {
		// Reversed direction since the last step — oscillating around a good
		// orientation (GEM-style detection). Damp harder the earlier this
		// sphere still is in its own rotation history, easing back toward
		// full strength as it accumulates more (successfully applied) steps.
		damp = sphere_rotation_steps[s] / DYN_LS_ROTATION_DAMP_HORIZON;
		if (damp < 0.05)
			damp = 0.05;
		if (damp > 1.0)
			damp = 1.0;
		apply_x *= damp;
		apply_y *= damp;
		apply_z *= damp;
	}

	// Cap by arc length (angle * radius) rather than angle alone, so every sphere sweeps
	// roughly the same surface distance per step: the small inner sphere gets a large
	// angular budget (rotates fast/far), the large outer ones get a small one (barely
	// creep), which is what actually reads as "settled" vs. "spinning" on screen.
	double max_step_rad = DYN_LS_ROTATION_MAX_STEP_RAD * (DYN_LS_ROTATION_REFERENCE_RADIUS / grid->radius);
	if (max_step_rad > DYN_LS_ROTATION_MAX_STEP_RAD)
		max_step_rad = DYN_LS_ROTATION_MAX_STEP_RAD; // never exceed the reference sphere's own budget

	double apply_angle = sqrt(apply_x * apply_x + apply_y * apply_y + apply_z * apply_z);
	bool clamped = apply_angle > max_step_rad; // pre-clamp vs. post-clamp applied_angle in the log below shows whether the cap (vs. the torque itself) is what's keeping this sphere in motion
	if (clamped) {
		double clamp_scale = max_step_rad / apply_angle;
		apply_x *= clamp_scale;
		apply_y *= clamp_scale;
		apply_z *= clamp_scale;
		apply_angle = max_step_rad;
	}

	// Store the undamped/unclamped direction for the NEXT step's oscillation
	// comparison, mirroring GEM's "compare freshly computed direction to the
	// last freshly computed direction" rather than the amount actually applied.
	prev[0] = want_x;
	prev[1] = want_y;
	prev[2] = want_z;

	if (apply_angle < 1e-9)
		return; // negligible — leave the quaternion and step counter untouched; a sphere that reliably lands here has actually converged and drops out of the log below

	// Identifies which sphere(s) never settle: only real (non-negligible) rotation reaches this
	// line, so a slot that keeps appearing here call after call genuinely never satisfies the
	// settled_delta<threshold streak above, rather than just being caught mid-convergence. Watch
	// want_angle/settle_delta together: a slot with roughly constant want_angle but tiny
	// settle_delta is the "plateaued at a stable nonzero torque" case the streak above exists to
	// catch; still increasing settle_delta means it's genuinely still moving toward something.
	// Throttled globally (not per-slot) so it can't get stuck re-logging a slot that's actually done.
	{
		static int log_ctr = 0;
		if (++log_ctr % 16 == 1)
			fprintf(stderr, "dyn_ls_rotate: slot=%d radius=%.2f edges=%d want_angle=%.6f settle_delta=%.6f applied_angle=%.6f cap=%.5f damp=%.2f%s%s streak=%d steps=%d\n", s, grid->radius, edge_count, want_angle, settle_delta, apply_angle, max_step_rad, damp, (dot < 0.0) ? " osc" : "", clamped ? " clamped" : "", sphere_settled_streak[s], sphere_rotation_steps[s]);
	}

	double axis_x = apply_x / apply_angle, axis_y = apply_y / apply_angle, axis_z = apply_z / apply_angle;
	double half = apply_angle * 0.5;
	double dq[4] = {cos(half), axis_x * sin(half), axis_y * sin(half), axis_z * sin(half)};

	double *q = &sphere_rotation[4 * s];
	double result[4];
	quat_multiply(dq, q, result); // dq applied on top of the existing accumulated rotation q
	quat_normalize(result);
	q[0] = result[0];
	q[1] = result[1];
	q[2] = result[2];
	q[3] = result[3];

	sphere_rotation_steps[s]++;
}

void dyn_ls_apply_sphere_rotation(LayeredSphereContext *ctx, int s, const double *sphere_rotation)
{
	SphereGrid *grid = &ctx->grids[s];
	const double *quat = &sphere_rotation[4 * s];
	for (int k = 0; k < grid->max_slots; k++) {
		int occ = grid->slot_occupant[k];
		if (occ < 0)
			continue;
		write_slot_position(ctx->layout, occ, &grid->slots[k], quat);
	}
}
