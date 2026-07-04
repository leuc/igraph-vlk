/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GRAPH_WRAPPERS_LAYOUT_H
#define GRAPH_WRAPPERS_LAYOUT_H

#include "interaction/state.h"
#include <igraph.h>

// Pure worker functions - no UI or state dependencies

// Force-directed layouts
void *compute_igraph_layout_fruchterman_reingold_3d(ExecutionContext *ctx);
void *compute_igraph_layout_fruchterman_reingold(ExecutionContext *ctx);
void *compute_igraph_layout_kamada_kawai_3d(ExecutionContext *ctx);
void *compute_igraph_layout_kamada_kawai(ExecutionContext *ctx);
void *compute_igraph_layout_drl_3d(ExecutionContext *ctx);
void *compute_igraph_layout_drl(ExecutionContext *ctx);
void *compute_igraph_layout_davidson_harel(ExecutionContext *ctx);
void *compute_igraph_layout_graphopt(ExecutionContext *ctx);
void *compute_igraph_layout_gem(ExecutionContext *ctx);
void *compute_igraph_layout_forceatlas2_3d(ExecutionContext *ctx);
void *compute_igraph_layout_yifan_hu(ExecutionContext *ctx);
void *compute_igraph_layout_yifan_hu_3d(ExecutionContext *ctx);
void *compute_igraph_layout_lgl(ExecutionContext *ctx);

// Binary classification-based graph layouts (BCGL)
void *compute_igraph_layout_bcgl(ExecutionContext *ctx);
void *compute_igraph_layout_bcgl_3d(ExecutionContext *ctx);

// Tree layouts
void *compute_igraph_layout_reingold_tilford(ExecutionContext *ctx);
void *compute_igraph_layout_sugiyama(ExecutionContext *ctx);
void *compute_igraph_layout_sugiyama_radial(ExecutionContext *ctx);

// Geometric layouts
void *compute_igraph_layout_circle(ExecutionContext *ctx);
void *compute_igraph_layout_circle_2d(ExecutionContext *ctx);
void *compute_igraph_layout_star(ExecutionContext *ctx);
void *compute_igraph_layout_grid_3d(ExecutionContext *ctx);
void *compute_igraph_layout_grid(ExecutionContext *ctx);
void *compute_igraph_layout_sphere(ExecutionContext *ctx);
void *compute_igraph_layout_random_3d(ExecutionContext *ctx);
void *compute_igraph_layout_random(ExecutionContext *ctx);

// Bipartite layouts
void *compute_igraph_layout_mds(ExecutionContext *ctx);
void *compute_igraph_layout_mds_3d(ExecutionContext *ctx);
void *compute_igraph_layout_mds_spherical(ExecutionContext *ctx);
void *compute_igraph_layout_bipartite(ExecutionContext *ctx);
void *compute_igraph_layout_bipartite_simple(ExecutionContext *ctx);

// Dimensionality reduction / Embedding
void *compute_igraph_layout_umap_3d(ExecutionContext *ctx);
void *compute_igraph_layout_umap(ExecutionContext *ctx);
void *compute_igraph_layout_bhtsne_3d(ExecutionContext *ctx);
void *compute_igraph_layout_bhtsne(ExecutionContext *ctx);

// Community-based layouts
void *compute_layout_layered_sphere(ExecutionContext *ctx);

// GPU-accelerated layouts
void *compute_layout_bcgl(ExecutionContext *ctx);

// Layout post-processing: center at origin + auto-scale (no rotation)
void layout_center_and_autoscale(igraph_matrix_t *mat);

// Seed helper: copies current positions into result when use_as_seed is set
igraph_bool_t layout_fill_seed(ExecutionContext *ctx, igraph_matrix_t *result, igraph_integer_t vcount);

// Standard apply and free functions
void free_layout_matrix(void *result_data);
void apply_layout_matrix(ExecutionContext *ctx, void *result_data);

// BFS/DFS trigger (re-run animation on current graph)
void apply_bfs_trigger(ExecutionContext *ctx, void *result_data);
void apply_dfs_trigger(ExecutionContext *ctx, void *result_data);
void apply_topo_trigger(ExecutionContext *ctx, void *result_data);

// Seed toggle
void *compute_use_current_positions_as_seed(ExecutionContext *ctx);
void apply_use_current_positions_as_seed(ExecutionContext *ctx, void *result_data);

// Random seed initialization (replicates igraph internals)
void *compute_seed_random_uniform(ExecutionContext *ctx);
void *compute_seed_random_bounded(ExecutionContext *ctx);
void *compute_seed_random_normal(ExecutionContext *ctx);

// BCGL apply, free, and GPU poll
void apply_layout_bcgl(ExecutionContext *ctx, void *result_data);
void free_layout_bcgl(void *result_data);
bool poll_bcgl_gpu(ExecutionContext *ctx);

#endif // GRAPH_WRAPPERS_LAYOUT_H
