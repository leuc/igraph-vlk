#ifndef GRAPH_WRAPPERS_LAYOUT_H
#define GRAPH_WRAPPERS_LAYOUT_H

#include <igraph.h>
#include "interaction/state.h"

// Pure worker functions - no UI or state dependencies

// Force-directed layouts
void* compute_lay_force_fr(igraph_t* graph);
void* compute_lay_force_kk(igraph_t* graph);
void* compute_lay_force_drl(igraph_t* graph);
void* compute_lay_force_dh(igraph_t* graph);

// Tree layouts
void* compute_lay_tree_rt(igraph_t* graph);
void* compute_lay_tree_sug(igraph_t* graph);

// Geometric layouts
void* compute_lay_geo_circle(igraph_t* graph);
void* compute_lay_geo_star(igraph_t* graph);
void* compute_lay_geo_grid(igraph_t* graph);
void* compute_lay_geo_sphere(igraph_t* graph);
void* compute_lay_geo_rand(igraph_t* graph);

// Bipartite layouts
void* compute_lay_bip_mds(igraph_t* graph);
void* compute_lay_bip_sug(igraph_t* graph);

// Dimensionality reduction
void* compute_lay_umap(igraph_t* graph);

// Standard apply and free functions
void free_layout_matrix(void* result_data);
void apply_layout_matrix(ExecutionContext* ctx, void* result_data);

#endif // GRAPH_WRAPPERS_LAYOUT_H
