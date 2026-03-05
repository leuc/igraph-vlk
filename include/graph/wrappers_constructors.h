#ifndef GRAPH_WRAPPERS_CONSTRUCTORS_H
#define GRAPH_WRAPPERS_CONSTRUCTORS_H

#include "interaction/state.h"
#include <igraph.h>

// Pure worker functions - no UI or state dependencies
// All return igraph_t* (new graph) on success, NULL on failure

// Deterministic graphs
void *compute_gen_ring(igraph_t *graph);	// Generate ring graph
void *compute_gen_star(igraph_t *graph);	// Generate star graph
void *compute_gen_tree(igraph_t *graph);	// Generate tree (k-ary)
void *compute_gen_lattice(igraph_t *graph); // Generate lattice
void *compute_gen_full(igraph_t *graph);	// Generate full clique
void *compute_gen_circle(igraph_t *graph);	// Generate cycle graph
void *compute_gen_notable(igraph_t *graph); // Generate famous graph (Zachary karate)

// Stochastic graphs
void *compute_gen_er(igraph_t *graph);			// Erdős-Rényi (GNP)
void *compute_gen_ba(igraph_t *graph);			// Barabási-Albert
void *compute_gen_ws(igraph_t *graph);			// Watts-Strogatz small-world
void *compute_gen_forest_fire(igraph_t *graph); // Forest fire model
void *compute_gen_random_tree(igraph_t *graph); // Random tree
void *compute_gen_degree_seq(igraph_t *graph);	// Random graph with given degree sequence

// Bipartite graphs
void *compute_gen_bipartite_random(igraph_t *graph);	 // Random bipartite graph
void *compute_gen_bipartite_projection(igraph_t *graph); // Project existing bipartite graph

// Spatial graphs
void *compute_gen_geometric(igraph_t *graph); // Geometric random graph
void *compute_gen_gabriel(igraph_t *graph);	  // Gabriel graph

// Standard apply and free functions for graph replacement
void apply_new_graph(ExecutionContext *ctx, void *result_data);
void free_new_graph(void *result_data);

#endif // GRAPH_WRAPPERS_CONSTRUCTORS_H
