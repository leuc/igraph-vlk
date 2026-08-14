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
 * The shared main-path pipeline uses level-synchronous gather sweeps to
 * produce, per node:
 *   lnW    ln(number of paths source -> v)          eq. 2.8b
 *   lnX    ln(number of paths v -> sink)            eq. 2.8c
 *   height max weight of any path source -> v       eq. 2.12
 *   depth  max weight of any path v -> sink         eq. 2.13
 *
 * SPLC and Unit use the forward count sweep. SPC, SPE, NPPC, and SPNP also
 * use the reverse sweep. NPPC replaces path-count recurrence with exact
 * predecessor and successor reachability sets. SPNP (Hummon & Doreian 1989;
 * generating-function form, Batagelj 2003 section 3.4) reuses SPLC's forward
 * "+1" self-term recursion (lnW = ln L-, every node also counts as its own
 * trivial 1-node path) but additionally runs it in reverse for lnX = ln L+,
 * then materializes the arc weight as the SPC-style product lnW+lnX. The
 * remaining result stages run together after the final visible sweep.
 */

/**
 * Build the GPU buffers for a criticality run.
 * @param r           Renderer instance
 * @param graph       Graph data; must be a DAG (caller's responsibility)
 * @param levels      Per-node level from calculate_dag_levels(), size n
 * @param num_levels  max_level + 1
 * @param weight_mode CRIT_WEIGHT_SPLC, CRIT_WEIGHT_UNIT, CRIT_WEIGHT_SPC, CRIT_WEIGHT_SPE, CRIT_WEIGHT_NPPC, or CRIT_WEIGHT_SPNP
 * @return true on success
 */
bool renderer_init_criticality_buffers(Renderer *r, GraphData *graph, const igraph_vector_int_t *levels, int num_levels, uint32_t weight_mode);

/** Start one live, level-ticked main-path weighting run. */
bool renderer_start_main_path_weighting(Renderer *r, const GraphData *graph, const igraph_vector_int_t *levels, float duration);

/** Record the next forward or reverse level dispatch into a frame command buffer. */
void renderer_dispatch_main_path_weight_level(Renderer *r, VkCommandBuffer cmd);

/**
 * Advance the NPPC reachability batch by at most one tile, non-blocking. Call once per frame
 * while `r->crit.nppc_batch_pending` is true; submits the next tile only once the previous tile's
 * fence has signaled, so the app keeps rendering and responding between tiles instead of stalling
 * for the whole batch. No-op when no NPPC batch is pending.
 */
void renderer_tick_nppc_batch(Renderer *r);

/** Persist the complete weighting, presentation, Basket, and Global Path result. */
void renderer_readback_main_path_result(Renderer *r, GraphData *graph);

/** Release all criticality GPU buffers and host-side level bookkeeping. */
void renderer_destroy_criticality_buffers(Renderer *r);
void renderer_cancel_main_path(Renderer *r);

#endif
