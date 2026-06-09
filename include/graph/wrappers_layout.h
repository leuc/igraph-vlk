#ifndef GRAPH_WRAPPERS_LAYOUT_H
#define GRAPH_WRAPPERS_LAYOUT_H

#include "interaction/state.h"
#include <igraph.h>

// Pure worker functions - no UI or state dependencies

// Force-directed layouts
void *compute_igraph_layout_fruchterman_reingold_3d(igraph_t *graph);
void *compute_igraph_layout_fruchterman_reingold(igraph_t *graph);
void *compute_igraph_layout_kamada_kawai_3d(igraph_t *graph);
void *compute_igraph_layout_kamada_kawai(igraph_t *graph);
void *compute_igraph_layout_drl_3d(igraph_t *graph);
void *compute_igraph_layout_drl(igraph_t *graph);
void *compute_igraph_layout_davidson_harel(igraph_t *graph);
void *compute_igraph_layout_graphopt(igraph_t *graph);
void *compute_igraph_layout_gem(igraph_t *graph);
void *compute_igraph_layout_forceatlas2_3d(igraph_t *graph);
void *compute_igraph_layout_yifan_hu(igraph_t *graph);
void *compute_igraph_layout_yifan_hu_3d(igraph_t *graph);
void *compute_igraph_layout_lgl(igraph_t *graph);

// Tree layouts
void *compute_igraph_layout_reingold_tilford(igraph_t *graph);
void *compute_igraph_layout_sugiyama(igraph_t *graph);
void *compute_igraph_layout_sugiyama_radial(igraph_t *graph);

// Geometric layouts
void *compute_igraph_layout_circle(igraph_t *graph);
void *compute_igraph_layout_circle_2d(igraph_t *graph);
void *compute_igraph_layout_star(igraph_t *graph);
void *compute_igraph_layout_grid_3d(igraph_t *graph);
void *compute_igraph_layout_grid(igraph_t *graph);
void *compute_igraph_layout_sphere(igraph_t *graph);
void *compute_igraph_layout_random_3d(igraph_t *graph);
void *compute_igraph_layout_random(igraph_t *graph);

// Bipartite layouts
void *compute_igraph_layout_mds(igraph_t *graph);
void *compute_igraph_layout_mds_3d(igraph_t *graph);
void *compute_igraph_layout_bipartite(igraph_t *graph);
void *compute_igraph_layout_bipartite_simple(igraph_t *graph);

// Dimensionality reduction / Embedding
void *compute_igraph_layout_umap_3d(igraph_t *graph);
void *compute_igraph_layout_umap(igraph_t *graph);
void *compute_igraph_layout_bhtsne_3d(igraph_t *graph);
void *compute_igraph_layout_bhtsne(igraph_t *graph);

// Community-based layouts
void *compute_layout_layered_sphere(igraph_t *graph);

// GPU-accelerated escape layout
void *compute_escape_layout(igraph_t *graph);

// Standard apply and free functions
void free_layout_matrix(void *result_data);
void apply_layout_matrix(ExecutionContext *ctx, void *result_data);

// GPU escape layout simulation driver and cleanup
struct Renderer;
struct AppState;
void igraph_vlk_layout_escape_drive(struct AppState *state, float ideal_length, uint32_t max_iters, float epsilon);
void igraph_vlk_layout_escape_cleanup(struct Renderer *r);

#endif // GRAPH_WRAPPERS_LAYOUT_H
