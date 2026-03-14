#include "command_registry.h"
#include "graph/wrappers_centrality.h"
#include "graph/wrappers_community.h"
#include "graph/wrappers_constructors.h"
#include "graph/wrappers_layout.h"
#include "graph/wrappers_paths.h"
#include "graph/wrappers_structural.h"

const CommandDef g_command_registry[] = {
	// =========================================================================
	// File menu
	// =========================================================================
	{"File", "file_new", "New Graph", NULL, NULL, NULL},
	{"File", "file_open", "Open Graph", NULL, NULL, NULL},
	{"File", "file_save", "Save Graph", NULL, NULL, NULL},
	{"File", "app_exit", "Exit", NULL, NULL, NULL},

	// =========================================================================
	// Generate menu - Deterministic Graphs
	// =========================================================================
	{"Generate/Deterministic Graphs", "igraph_ring", "Ring", compute_igraph_ring, apply_new_graph, free_new_graph},
	{"Generate/Deterministic Graphs", "igraph_star", "Star", compute_igraph_star, apply_new_graph, free_new_graph},
	{"Generate/Deterministic Graphs", "igraph_kary_tree", "Tree", compute_igraph_kary_tree, apply_new_graph, free_new_graph},
	{"Generate/Deterministic Graphs", "igraph_square_lattice", "Lattice", compute_igraph_square_lattice, apply_new_graph, free_new_graph},
	{"Generate/Deterministic Graphs", "igraph_full", "Full Graph (Clique)", compute_igraph_full, apply_new_graph, free_new_graph},
	{"Generate/Deterministic Graphs", "igraph_cycle_graph", "Circle", compute_igraph_cycle_graph, apply_new_graph, free_new_graph},
	{"Generate/Deterministic Graphs", "igraph_famous", "Notable Graphs", compute_igraph_famous, apply_new_graph, free_new_graph},

	// =========================================================================
	// Generate menu - Stochastic Graphs
	// =========================================================================
	{"Generate/Stochastic Graphs", "igraph_erdos_renyi_game_gnp", "Erdős-Rényi (GNP / GNM)", compute_igraph_erdos_renyi_game_gnp, apply_new_graph, free_new_graph},
	{"Generate/Stochastic Graphs", "igraph_barabasi_game", "Barabási-Albert (Preferential attachment)", compute_igraph_barabasi_game, apply_new_graph, free_new_graph},
	{"Generate/Stochastic Graphs", "igraph_watts_strogatz_game", "Watts-Strogatz (Small-world)", compute_igraph_watts_strogatz_game, apply_new_graph, free_new_graph},
	{"Generate/Stochastic Graphs", "igraph_forest_fire_game", "Forest Fire", compute_igraph_forest_fire_game, apply_new_graph, free_new_graph},
	{"Generate/Stochastic Graphs", "igraph_tree_game", "Random Tree", compute_igraph_tree_game, apply_new_graph, free_new_graph},
	{"Generate/Stochastic Graphs", "igraph_degree_sequence_game", "Degree Sequence", compute_igraph_degree_sequence_game, apply_new_graph, free_new_graph},

	// =========================================================================
	// Generate menu - Bipartite
	// =========================================================================
	{"Generate/Bipartite Graphs", "igraph_bipartite_game_gnm", "Generate Random Bipartite", compute_igraph_bipartite_game_gnm, apply_new_graph, free_new_graph},
	{"Generate/Bipartite Graphs", "igraph_bipartite_projection", "Create Bipartite Projections", compute_igraph_bipartite_projection, apply_new_graph, free_new_graph},

	// =========================================================================
	// Generate menu - Spatial
	// =========================================================================
	{"Generate/Spatial Graphs", "igraph_nearest_neighbor_graph", "Geometric random graphs", compute_igraph_nearest_neighbor_graph, apply_new_graph, free_new_graph},
	{"Generate/Spatial Graphs", "igraph_gabriel_graph", "Gabriel graphs", compute_igraph_gabriel_graph, apply_new_graph, free_new_graph},

	// =========================================================================
	// Layout menu - Force-Directed
	// =========================================================================
	{"Layout/Force-Directed", "igraph_layout_fruchterman_reingold_3d", "Fruchterman-Reingold (3D)", compute_igraph_layout_fruchterman_reingold_3d, apply_layout_matrix, free_layout_matrix},
	{"Layout/Force-Directed", "igraph_layout_fruchterman_reingold", "Fruchterman-Reingold (2D)", compute_igraph_layout_fruchterman_reingold, apply_layout_matrix, free_layout_matrix},
	{"Layout/Force-Directed", "igraph_layout_kamada_kawai_3d", "Kamada-Kawai (3D)", compute_igraph_layout_kamada_kawai_3d, apply_layout_matrix, free_layout_matrix},
	{"Layout/Force-Directed", "igraph_layout_kamada_kawai", "Kamada-Kawai (2D)", compute_igraph_layout_kamada_kawai, apply_layout_matrix, free_layout_matrix},
	{"Layout/Force-Directed", "igraph_layout_drl_3d", "Distributed Recursive Layout (DrL) (3D)", compute_igraph_layout_drl_3d, apply_layout_matrix, free_layout_matrix},
	{"Layout/Force-Directed", "igraph_layout_drl", "Distributed Recursive Layout (DrL) (2D)", compute_igraph_layout_drl, apply_layout_matrix, free_layout_matrix},
	{"Layout/Force-Directed", "igraph_layout_davidson_harel", "Davidson-Harel", compute_igraph_layout_davidson_harel, apply_layout_matrix, free_layout_matrix},
	{"Layout/Force-Directed", "igraph_layout_graphopt", "GraphOpt", compute_igraph_layout_graphopt, apply_layout_matrix, free_layout_matrix},
	{"Layout/Force-Directed", "igraph_layout_lgl", "Large Graph Layout (LGL)", compute_igraph_layout_lgl, apply_layout_matrix, free_layout_matrix},
	{"Layout/Force-Directed", "igraph_layout_gem", "GEM", compute_igraph_layout_gem, apply_layout_matrix_centered, free_layout_matrix},
	{"Layout/Force-Directed", "igraph_layout_yifan_hu", "Yifan Hu", compute_igraph_layout_yifan_hu, apply_layout_matrix, free_layout_matrix},
	{"Layout/Force-Directed", "igraph_layout_yifan_hu_3d", "Yifan Hu (3D)", compute_igraph_layout_yifan_hu_3d, apply_layout_matrix, free_layout_matrix},

	// =========================================================================
	// Layout menu - Tree & Hierarchical
	// =========================================================================
	{"Layout/Tree & Hierarchical", "igraph_layout_reingold_tilford", "Reingold-Tilford", compute_igraph_layout_reingold_tilford, apply_layout_matrix, free_layout_matrix},
	{"Layout/Tree & Hierarchical", "igraph_layout_sugiyama", "Sugiyama", compute_igraph_layout_sugiyama, apply_layout_matrix, free_layout_matrix},
	{"Layout/Tree & Hierarchical", "igraph_layout_sugiyama_radial", "Radial Sugiyama", compute_igraph_layout_sugiyama_radial, apply_layout_matrix, free_layout_matrix},

	// Non-Igraph
	{"Layout", "lay_layered_sphere", "Layered Sphere", compute_layout_layered_sphere, apply_layout_matrix, free_layout_matrix},
	// =========================================================================
	// Layout menu - Geometric
	// =========================================================================
	{"Layout/Geometric", "igraph_layout_circle", "Circle (3D)", compute_igraph_layout_circle, apply_layout_matrix, free_layout_matrix},
	{"Layout/Geometric", "igraph_layout_circle_2d", "Circle (2D)", compute_igraph_layout_circle_2d, apply_layout_matrix, free_layout_matrix},
	{"Layout/Geometric", "igraph_layout_star", "Star", compute_igraph_layout_star, apply_layout_matrix, free_layout_matrix},
	{"Layout/Geometric", "igraph_layout_grid_3d", "Grid (3D)", compute_igraph_layout_grid_3d, apply_layout_matrix, free_layout_matrix},
	{"Layout/Geometric", "igraph_layout_grid", "Grid (2D)", compute_igraph_layout_grid, apply_layout_matrix, free_layout_matrix},
	{"Layout/Geometric", "igraph_layout_sphere", "Sphere", compute_igraph_layout_sphere, apply_layout_matrix, free_layout_matrix},
	{"Layout/Geometric", "igraph_layout_random_3d", "Random (3D)", compute_igraph_layout_random_3d, apply_layout_matrix, free_layout_matrix},
	{"Layout/Geometric", "igraph_layout_random", "Random (2D)", compute_igraph_layout_random, apply_layout_matrix, free_layout_matrix},

	// =========================================================================
	// Layout menu - Bipartite Layouts
	// =========================================================================
	{"Layout/Bipartite", "igraph_layout_mds", "MDS", compute_igraph_layout_mds, apply_layout_matrix, free_layout_matrix},
	{"Layout/Bipartite", "igraph_layout_bipartite", "Sugiyama (Bipartite)", compute_igraph_layout_bipartite, apply_layout_matrix, free_layout_matrix},
	{"Layout/Bipartite", "igraph_layout_bipartite_simple", "Bipartite (Simple)", compute_igraph_layout_bipartite_simple, apply_layout_matrix, free_layout_matrix},

	// =========================================================================
	// Layout menu - Graph Embedding
	// =========================================================================
	{"Layout/Dimension Reduction ", "igraph_layout_umap_2d", "UMAP (2D)", compute_igraph_layout_umap, apply_layout_matrix, free_layout_matrix},
	{"Layout/Dimension Reduction ", "igraph_layout_umap_3d", "UMAP (3D)", compute_igraph_layout_umap_3d, apply_layout_matrix, free_layout_matrix},

	// =========================================================================
	// Analysis menu - Centrality & Roles
	// =========================================================================
	{"Analysis/Centrality", "igraph_degree", "Degree", compute_igraph_degree, apply_centrality_scores, free_centrality_scores},
	{"Analysis/Centrality", "igraph_closeness_cutoff", "Closeness", compute_igraph_closeness_cutoff, apply_centrality_scores, free_centrality_scores},
	{"Analysis/Centrality", "igraph_betweenness", "Betweenness", compute_igraph_betweenness, apply_centrality_scores, free_centrality_scores},
	{"Analysis/Centrality", "igraph_eigenvector_centrality", "Eigenvector Centrality", compute_igraph_eigenvector_centrality, apply_centrality_scores, free_centrality_scores},
	{"Analysis/Centrality", "igraph_pagerank", "PageRank", compute_igraph_pagerank, apply_centrality_scores, free_centrality_scores},
	{"Analysis/Centrality", "igraph_hub_and_authority_scores", "HITS (Hub)", compute_igraph_hub_and_authority_scores, apply_centrality_scores, free_centrality_scores},
	{"Analysis/Centrality", "igraph_harmonic_centrality", "Harmonic", compute_igraph_harmonic_centrality, apply_centrality_scores, free_centrality_scores},
	{"Analysis/Centrality", "igraph_strength", "Strength (Weighted Degree)", compute_igraph_strength, apply_centrality_scores, free_centrality_scores},
	{"Analysis/Centrality", "igraph_constraint", "Constraint (Structural Holes)", compute_igraph_constraint, apply_centrality_scores, free_centrality_scores},

	// =========================================================================
	// Analysis menu - Global Network Properties
	// =========================================================================
	{"Analysis/Global Properties", "igraph_diameter", "Diameter", compute_igraph_diameter, apply_info_card, free_info_card},
	{"Analysis/Global Properties", "igraph_radius", "Radius", compute_igraph_radius, apply_info_card, free_info_card},
	{"Analysis/Global Properties", "igraph_average_path_length", "Average Path Length", compute_igraph_average_path_length, apply_info_card, free_info_card},
	{"Analysis/Global Properties", "igraph_assortativity_degree", "Assortativity", compute_igraph_assortativity_degree, apply_info_card, free_info_card},
	{"Analysis/Global Properties", "igraph_density", "Density", compute_igraph_density, apply_info_card, free_info_card},
	{"Analysis/Global Properties", "igraph_transitivity_undirected", "Transitivity (undirected)", compute_igraph_transitivity_undirected, apply_info_card, free_info_card},

	// =========================================================================
	// Communities menu - Detection
	// =========================================================================
	{"Communities/Detection", "igraph_community_multilevel", "Louvain Method (Multilevel)", compute_igraph_community_multilevel, apply_community_membership, free_community_membership},
	{"Communities/Detection", "igraph_community_leiden", "Leiden", compute_igraph_community_leiden, apply_community_membership, free_community_membership},
	{"Communities/Detection", "igraph_community_walktrap", "Walktrap (Random walks)", compute_igraph_community_walktrap, apply_community_membership, free_community_membership},
	{"Communities/Detection", "igraph_community_edge_betweenness", "Edge Betweenness (Girvan-Newman)", compute_igraph_community_edge_betweenness, apply_community_membership, free_community_membership},
	{"Communities/Detection", "igraph_community_fastgreedy", "Fast Greedy", compute_igraph_community_fastgreedy, apply_community_membership, free_community_membership},
	{"Communities/Detection", "igraph_community_infomap", "Infomap", compute_igraph_community_infomap, apply_community_membership, free_community_membership},
	{"Communities/Detection", "igraph_community_label_propagation", "Label Propagation", compute_igraph_community_label_propagation, apply_community_membership, free_community_membership},
	{"Communities/Detection", "igraph_community_spinglass", "Spinglass", compute_igraph_community_spinglass, apply_community_membership, free_community_membership},
	{"Communities/Detection", "igraph_community_leading_eigenvector", "Leading Eigenvector", compute_igraph_community_leading_eigenvector, apply_community_membership, free_community_membership},
	{"Communities/Detection", "igraph_community_optimal_modularity", "Optimal Modularity", compute_igraph_community_optimal_modularity, apply_community_membership, free_community_membership},
	{"Communities/Detection", "igraph_community_voronoi", "Voronoi", compute_igraph_community_voronoi, apply_community_membership, free_community_membership},
	{"Communities/Detection", "igraph_community_fluid_communities", "Fluid Communities", compute_igraph_community_fluid_communities, apply_community_membership, free_community_membership},

};
const int g_command_registry_size = sizeof(g_command_registry) / sizeof(g_command_registry[0]);
