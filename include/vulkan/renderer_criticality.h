/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef RENDERER_CRITICALITY_H
#define RENDERER_CRITICALITY_H

#include "renderer.h"

/**
 * Generalised criticality on the GPU — Price & Evans, "Understanding Main
 * Path Analysis", arXiv:2512.12355 section 2.5.
 *
 * Four level-synchronous gather sweeps produce, per node:
 *   lnW    ln(number of paths source -> v)          eq. 2.8b
 *   lnX    ln(number of paths v -> sink)            eq. 2.8c
 *   height max weight of any path source -> v       eq. 2.12
 *   depth  max weight of any path v -> sink         eq. 2.13
 *
 * The caller derives H, criticality c = H - h - d, and the basket from these.
 */

/**
 * Build the GPU buffers for a criticality run.
 * @param r           Renderer instance
 * @param graph       Graph data; must be a DAG (caller's responsibility)
 * @param levels      Per-node level from calculate_dag_levels(), size n
 * @param num_levels  max_level + 1
 * @param weight_mode CRIT_WEIGHT_UNIT or CRIT_WEIGHT_SPE
 * @return true on success
 */
bool renderer_init_criticality_buffers(Renderer *r, GraphData *graph, const igraph_vector_int_t *levels, int num_levels, uint32_t weight_mode);

/**
 * Record all 4 * num_levels dispatches and submit them as a single batch.
 * Non-blocking: poll with renderer_criticality_ready().
 * @return true if the submit succeeded
 */
bool renderer_dispatch_criticality(Renderer *r);

/** True once the submitted criticality batch has finished on the GPU. */
bool renderer_criticality_ready(Renderer *r);

/**
 * Copy the four per-node result arrays out of GPU memory.
 * Each out parameter may be NULL to skip it; non-NULL ones receive a
 * malloc'd array of node_count floats that the caller must free().
 * @return true on success
 */
bool renderer_readback_criticality(Renderer *r, float **out_height, float **out_depth, float **out_lnw, float **out_lnx);

/** Release all criticality GPU buffers and host-side level bookkeeping. */
void renderer_destroy_criticality_buffers(Renderer *r);

#endif
