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
 * SPLC stops after the forward pass. SPC and SPE then use the reverse pass to
 * project their edge scores. The legacy height/depth helpers remain available
 * for the criticality analysis path.
 */

/**
 * Build the GPU buffers for a criticality run.
 * @param r           Renderer instance
 * @param graph       Graph data; must be a DAG (caller's responsibility)
 * @param levels      Per-node level from calculate_dag_levels(), size n
 * @param num_levels  max_level + 1
 * @param weight_mode CRIT_WEIGHT_UNIT (SPLC), CRIT_WEIGHT_SPC, or CRIT_WEIGHT_SPE
 * @return true on success
 */
bool renderer_init_criticality_buffers(Renderer *r, GraphData *graph, const igraph_vector_int_t *levels, int num_levels, uint32_t weight_mode);

/** Start one live, level-ticked main-path weighting run. */
bool renderer_start_main_path_weighting(Renderer *r);

/** Record the next forward or reverse level dispatch into a frame command buffer. */
void renderer_dispatch_main_path_weight_level(Renderer *r, VkCommandBuffer cmd);

/** Persist the completed live weighting result as the method's edge attribute. */
void renderer_readback_main_path_weights(Renderer *r, GraphData *graph);

/** Release all criticality GPU buffers and host-side level bookkeeping. */
void renderer_destroy_criticality_buffers(Renderer *r);

#endif
