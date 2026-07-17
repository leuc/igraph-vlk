/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GRAPH_WRAPPERS_COMMUNITY_H
#define GRAPH_WRAPPERS_COMMUNITY_H

#include "interaction/state.h"
#include <igraph.h>

// Pure worker functions - no UI or state dependencies
// All return igraph_vector_int_t* (membership vector) on success, NULL on failure

// Multilevel (Louvain)
void *compute_igraph_community_multilevel(ExecutionContext *ctx);

// Leiden
void *compute_igraph_community_leiden(ExecutionContext *ctx);

// Walktrap
void *compute_igraph_community_walktrap(ExecutionContext *ctx);

// Edge Betweenness (Girvan-Newman)
void *compute_igraph_community_edge_betweenness(ExecutionContext *ctx);

// Fast Greedy
void *compute_igraph_community_fastgreedy(ExecutionContext *ctx);

// Infomap
void *compute_igraph_community_infomap(ExecutionContext *ctx);

// Label Propagation
void *compute_igraph_community_label_propagation(ExecutionContext *ctx);

// Spinglass
void *compute_igraph_community_spinglass(ExecutionContext *ctx);

// Leading Eigenvector
void *compute_igraph_community_leading_eigenvector(ExecutionContext *ctx);

// Optimal Modularity
void *compute_igraph_community_optimal_modularity(ExecutionContext *ctx);

// Voronoi
void *compute_igraph_community_voronoi(ExecutionContext *ctx);

// Fluid Communities
void *compute_igraph_community_fluid_communities(ExecutionContext *ctx);

// Standard apply and free functions for community membership
void apply_community_membership(ExecutionContext *ctx, void *result_data);
void free_community_membership(void *result_data);

// Deterministic per-community color (golden-ratio hue stepping + HSV->RGB),
// shared by the static community-detection apply path and the streaming
// DynLeiden recolor path. Depends only on comm_id, not on the total
// community count, so it works unchanged for arbitrarily large/sparse ids
// (e.g. representative vertex ids), not just compact 0..C-1 indices.
void community_id_to_rgb(igraph_integer_t comm_id, float out_rgb[3]);

#endif // GRAPH_WRAPPERS_COMMUNITY_H
