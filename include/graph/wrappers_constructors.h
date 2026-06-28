/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GRAPH_WRAPPERS_CONSTRUCTORS_H
#define GRAPH_WRAPPERS_CONSTRUCTORS_H

#include "interaction/state.h"
#include <igraph.h>

// Pure worker functions - no UI or state dependencies
// All return igraph_t* (new graph) on success, NULL on failure

// Deterministic graphs
void *compute_igraph_ring(ExecutionContext *ctx);			// Generate ring graph
void *compute_igraph_star(ExecutionContext *ctx);			// Generate star graph
void *compute_igraph_kary_tree(ExecutionContext *ctx);		// Generate tree (k-ary)
void *compute_igraph_square_lattice(ExecutionContext *ctx); // Generate lattice
void *compute_igraph_full(ExecutionContext *ctx);			// Generate full clique
void *compute_igraph_cycle_graph(ExecutionContext *ctx);	// Generate cycle graph
void *compute_igraph_famous(ExecutionContext *ctx);			// Generate famous graph (Zachary karate)

// Stochastic graphs
void *compute_igraph_erdos_renyi_game_gnp(ExecutionContext *ctx); // Erdős-Rényi (GNP)
void *compute_igraph_barabasi_game(ExecutionContext *ctx);		  // Barabási-Albert
void *compute_igraph_watts_strogatz_game(ExecutionContext *ctx);  // Watts-Strogatz small-world
void *compute_igraph_forest_fire_game(ExecutionContext *ctx);	  // Forest fire model
void *compute_igraph_tree_game(ExecutionContext *ctx);			  // Random tree
void *compute_igraph_degree_sequence_game(ExecutionContext *ctx); // Random graph with given degree sequence

// Bipartite graphs
void *compute_igraph_bipartite_game_gnm(ExecutionContext *ctx);	  // Random bipartite graph
void *compute_igraph_bipartite_projection(ExecutionContext *ctx); // Project existing bipartite graph

// Spatial graphs
void *compute_igraph_nearest_neighbor_graph(ExecutionContext *ctx); // Geometric random graph
void *compute_igraph_gabriel_graph(ExecutionContext *ctx);			// Gabriel graph

// Standard apply and free functions for graph replacement
void apply_new_graph(ExecutionContext *ctx, void *result_data);
void free_new_graph(void *result_data);

#endif // GRAPH_WRAPPERS_CONSTRUCTORS_H
