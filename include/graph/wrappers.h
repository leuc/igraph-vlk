#ifndef GRAPH_WRAPPERS_H
#define GRAPH_WRAPPERS_H

#include "graph/graph_core.h"
#include "interaction/state.h"

// Igraph wrapper functions
void wrapper_shortest_path(ExecutionContext *ctx);
void wrapper_pagerank(ExecutionContext *ctx);
void wrapper_betweenness(ExecutionContext *ctx);
void wrapper_reset_layout(ExecutionContext *ctx);
void wrapper_cycle_colors(ExecutionContext *ctx);
void wrapper_community_arrangement(ExecutionContext *ctx);

// Layout wrapper functions
void wrapper_lay_force_fr(ExecutionContext *ctx);
void wrapper_lay_force_kk(ExecutionContext *ctx);
void wrapper_lay_force_drl(ExecutionContext *ctx);
void wrapper_lay_force_dh(ExecutionContext *ctx);
void wrapper_lay_tree_rt(ExecutionContext *ctx);
void wrapper_lay_tree_sug(ExecutionContext *ctx);
void wrapper_lay_geo_circle(ExecutionContext *ctx);
void wrapper_lay_geo_star(ExecutionContext *ctx);
void wrapper_lay_geo_grid(ExecutionContext *ctx);
void wrapper_lay_geo_sphere(ExecutionContext *ctx);
void wrapper_lay_geo_rand(ExecutionContext *ctx);
void wrapper_lay_bip_mds(ExecutionContext *ctx);
void wrapper_lay_bip_sug(ExecutionContext *ctx);

// Dimension Reduction / Embedding wrappers
void wrapper_lay_umap(ExecutionContext *ctx);

#endif // GRAPH_WRAPPERS_H
