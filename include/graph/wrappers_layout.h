#ifndef GRAPH_WRAPPERS_LAYOUT_H
#define GRAPH_WRAPPERS_LAYOUT_H

#include "interaction/state.h"
#include <igraph.h>

// Pure worker functions - no UI or state dependencies

// Force-directed layouts
void *compute_lay_force_fr(igraph_t *graph);
void *compute_lay_force_fr_2d(igraph_t *graph);
void *compute_lay_force_kk(igraph_t *graph);
void *compute_lay_force_kk_2d(igraph_t *graph);
void *compute_lay_force_drl(igraph_t *graph);
void *compute_lay_force_drl_2d(igraph_t *graph);
void *compute_lay_force_dh(igraph_t *graph);
void *compute_lay_force_go(igraph_t *graph);
void *compute_lay_force_gem(igraph_t *graph);
void *compute_lay_force_lgl(igraph_t *graph);

// Tree layouts
void *compute_lay_tree_rt(igraph_t *graph);
void *compute_lay_tree_sug(igraph_t *graph);

// Geometric layouts
void *compute_lay_geo_circle(igraph_t *graph);
void *compute_lay_geo_circle_2d(igraph_t *graph);
void *compute_lay_geo_star(igraph_t *graph);
void *compute_lay_geo_grid(igraph_t *graph);
void *compute_lay_geo_grid_2d(igraph_t *graph);
void *compute_lay_geo_sphere(igraph_t *graph);
void *compute_lay_geo_rand(igraph_t *graph);
void *compute_lay_geo_rand_2d(igraph_t *graph);

// Bipartite layouts
void *compute_lay_bip_mds(igraph_t *graph);
void *compute_lay_bip_sug(igraph_t *graph);
void *compute_lay_bip_simple(igraph_t *graph);

// Dimensionality reduction / Embedding
void *compute_lay_umap(igraph_t *graph);
void *compute_lay_umap_2d(igraph_t *graph);

// Community-based layouts
void *compute_lay_layered_sphere(igraph_t *graph);

// Standard apply and free functions
void free_layout_matrix(void *result_data);
void apply_layout_matrix(ExecutionContext *ctx, void *result_data);
void apply_layout_matrix_centered(ExecutionContext *ctx, void *result_data);

#endif // GRAPH_WRAPPERS_LAYOUT_H
