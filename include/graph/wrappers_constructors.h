#ifndef GRAPH_WRAPPERS_CONSTRUCTORS_H
#define GRAPH_WRAPPERS_CONSTRUCTORS_H

#include "interaction/state.h"
#include <igraph.h>

// Pure worker functions - no UI or state dependencies
// All return igraph_t* (new graph) on success, NULL on failure

// Deterministic graphs
void *compute_igraph_ring(igraph_t *graph);			  // Generate ring graph
void *compute_igraph_star(igraph_t *graph);			  // Generate star graph
void *compute_igraph_kary_tree(igraph_t *graph);	  // Generate tree (k-ary)
void *compute_igraph_square_lattice(igraph_t *graph); // Generate lattice
void *compute_igraph_full(igraph_t *graph);			  // Generate full clique
void *compute_igraph_cycle_graph(igraph_t *graph);	  // Generate cycle graph
void *compute_igraph_famous(igraph_t *graph);		  // Generate famous graph (Zachary karate)

// Stochastic graphs
void *compute_igraph_erdos_renyi_game_gnp(igraph_t *graph); // Erdős-Rényi (GNP)
void *compute_igraph_barabasi_game(igraph_t *graph);		// Barabási-Albert
void *compute_igraph_watts_strogatz_game(igraph_t *graph);	// Watts-Strogatz small-world
void *compute_igraph_forest_fire_game(igraph_t *graph);		// Forest fire model
void *compute_igraph_tree_game(igraph_t *graph);			// Random tree
void *compute_igraph_degree_sequence_game(igraph_t *graph); // Random graph with given degree sequence

// Bipartite graphs
void *compute_igraph_bipartite_game_gnm(igraph_t *graph);	// Random bipartite graph
void *compute_igraph_bipartite_projection(igraph_t *graph); // Project existing bipartite graph

// Spatial graphs
void *compute_igraph_nearest_neighbor_graph(igraph_t *graph); // Geometric random graph
void *compute_igraph_gabriel_graph(igraph_t *graph);		  // Gabriel graph

// Standard apply and free functions for graph replacement
void apply_new_graph(ExecutionContext *ctx, void *result_data);
void free_new_graph(void *result_data);

#endif // GRAPH_WRAPPERS_CONSTRUCTORS_H
