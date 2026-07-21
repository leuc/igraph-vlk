/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "command_registry.h"
#include "graph/repo_netzschleuder.h"
#include "graph/stream.h"
#include "graph/wrappers_centrality.h"
#include "graph/wrappers_community.h"
#include "graph/wrappers_constructors.h"
#include "graph/wrappers_cycles.h"
#include "graph/wrappers_filter.h"
#include "graph/wrappers_flow.h"
#include "graph/wrappers_kcore_tree.h"
#include "graph/wrappers_layout.h"
#include "graph/wrappers_paths.h"
#include "graph/wrappers_splc.h"
#include "graph/wrappers_structural.h"

const CommandDef g_command_registry[] = {
	{"Data/Patterns", "igraph_ring", "Ring", compute_igraph_ring, apply_new_graph, free_new_graph},
	{"Data/Patterns", "igraph_star", "Star", compute_igraph_star, apply_new_graph, free_new_graph},
	{"Data/Patterns", "igraph_kary_tree", "Tree", compute_igraph_kary_tree, apply_new_graph, free_new_graph},
	{"Data/Patterns", "igraph_square_lattice", "Lattice", compute_igraph_square_lattice, apply_new_graph, free_new_graph},
	{"Data/Patterns", "igraph_full", "Full Graph (Clique)", compute_igraph_full, apply_new_graph, free_new_graph},
	{"Data/Patterns", "igraph_cycle_graph", "Circle", compute_igraph_cycle_graph, apply_new_graph, free_new_graph},

	{"Data/Random", "igraph_erdos_renyi_game_gnp", "Erdős-Rényi (GNP / GNM)", compute_igraph_erdos_renyi_game_gnp, apply_new_graph, free_new_graph},
	{"Data/Random", "igraph_barabasi_game", "Barabási-Albert (Preferential attachment)", compute_igraph_barabasi_game, apply_new_graph, free_new_graph},
	{"Data/Random", "igraph_watts_strogatz_game", "Watts-Strogatz (Small-world)", compute_igraph_watts_strogatz_game, apply_new_graph, free_new_graph},
	{"Data/Random", "igraph_forest_fire_game", "Forest Fire", compute_igraph_forest_fire_game, apply_new_graph, free_new_graph},
	{"Data/Random", "igraph_tree_game", "Random Tree", compute_igraph_tree_game, apply_new_graph, free_new_graph},
	{"Data/Random", "igraph_degree_sequence_game", "Degree Sequence", compute_igraph_degree_sequence_game, apply_new_graph, free_new_graph},

	{"Data/Bipartite", "igraph_bipartite_game_gnm", "Generate Random Bipartite", compute_igraph_bipartite_game_gnm, apply_new_graph, free_new_graph},
	{"Data/Bipartite", "igraph_bipartite_projection", "Create Bipartite Projections", compute_igraph_bipartite_projection, apply_new_graph, free_new_graph},

	{"Data/Spatial", "igraph_nearest_neighbor_graph", "Geometric random graphs", compute_igraph_nearest_neighbor_graph, apply_new_graph, free_new_graph},
	{"Data/Spatial", "igraph_gabriel_graph", "Gabriel graphs", compute_igraph_gabriel_graph, apply_new_graph, free_new_graph},

	{"Data/Stream", "toggle_stream_pause", "[ ] Pause", compute_toggle_stream_pause, apply_toggle_stream_pause, free_noop},
	{"Data/Stream", "toggle_stream_layered_sphere", "[x] Live Layered Sphere", compute_toggle_stream_layered_sphere, apply_toggle_stream_layered_sphere, free_noop},

	{"Layout/Seed", "use_current_positions_as_seed", "[ ] Use current positions as seed", compute_use_current_positions_as_seed, apply_use_current_positions_as_seed, free_noop},
	{"Layout/Seed", "seed_random_uniform", "Random Uniform [-1, 1]", compute_seed_random_uniform, apply_layout_matrix, free_layout_matrix},
	{"Layout/Seed", "seed_random_bounded", "Random Bounded [-sqrt(n)/2]", compute_seed_random_bounded, apply_layout_matrix, free_layout_matrix},
	{"Layout/Seed", "seed_random_normal", "Random Normal N(0, 0.01)", compute_seed_random_normal, apply_layout_matrix, free_layout_matrix},

	{"Layout/Force-Directed", "igraph_layout_fruchterman_reingold", "Fruchterman-Reingold (2D)", compute_igraph_layout_fruchterman_reingold, apply_layout_matrix, free_layout_matrix},
	{"Layout/Force-Directed", "igraph_layout_fruchterman_reingold_3d", "Fruchterman-Reingold (3D)", compute_igraph_layout_fruchterman_reingold_3d, apply_layout_matrix, free_layout_matrix},
	{"Layout/Force-Directed", "igraph_layout_kamada_kawai", "Kamada-Kawai (2D)", compute_igraph_layout_kamada_kawai, apply_layout_matrix, free_layout_matrix},
	{"Layout/Force-Directed", "igraph_layout_kamada_kawai_3d", "Kamada-Kawai (3D)", compute_igraph_layout_kamada_kawai_3d, apply_layout_matrix, free_layout_matrix},
	{"Layout/Force-Directed", "igraph_layout_drl", "Distributed Recursive Layout (DrL) (2D)", compute_igraph_layout_drl, apply_layout_matrix, free_layout_matrix},
	{"Layout/Force-Directed", "igraph_layout_drl_3d", "Distributed Recursive Layout (DrL) (3D)", compute_igraph_layout_drl_3d, apply_layout_matrix, free_layout_matrix},
	{"Layout/Force-Directed", "igraph_layout_davidson_harel", "Davidson-Harel", compute_igraph_layout_davidson_harel, apply_layout_matrix, free_layout_matrix},
	{"Layout/Force-Directed", "igraph_layout_graphopt", "GraphOpt", compute_igraph_layout_graphopt, apply_layout_matrix, free_layout_matrix},
	{"Layout/Force-Directed", "igraph_layout_lgl", "Large Graph Layout (LGL)", compute_igraph_layout_lgl, apply_layout_matrix, free_layout_matrix},
	{"Layout/Force-Directed", "igraph_layout_gem", "GEM", compute_igraph_layout_gem, apply_layout_matrix, free_layout_matrix},
	{"Layout/Force-Directed", "igraph_layout_forceatlas2_3d", "ForceAtlas2 (3D)", compute_igraph_layout_forceatlas2_3d, apply_layout_matrix, free_layout_matrix},
	{"Layout/Force-Directed", "igraph_layout_yifan_hu", "Yifan Hu (2D)", compute_igraph_layout_yifan_hu, apply_layout_matrix, free_layout_matrix},
	{"Layout/Force-Directed", "igraph_layout_yifan_hu_3d", "Yifan Hu (3D)", compute_igraph_layout_yifan_hu_3d, apply_layout_matrix, free_layout_matrix},

	{"Layout/Hierarchical", "igraph_layout_reingold_tilford", "Reingold-Tilford", compute_igraph_layout_reingold_tilford, apply_layout_matrix, free_layout_matrix},
	{"Layout/Hierarchical", "igraph_layout_sugiyama", "Sugiyama", compute_igraph_layout_sugiyama, apply_layout_matrix, free_layout_matrix},
	{"Layout/Hierarchical", "igraph_layout_sugiyama_radial", "Radial Sugiyama", compute_igraph_layout_sugiyama_radial, apply_layout_matrix, free_layout_matrix},

	{"Layout/Geometric", "igraph_layout_circle_2d", "Circle (2D)", compute_igraph_layout_circle_2d, apply_layout_matrix, free_layout_matrix},
	{"Layout/Geometric", "igraph_layout_circle", "Circle (3D)", compute_igraph_layout_circle, apply_layout_matrix, free_layout_matrix},
	{"Layout/Geometric", "igraph_layout_sphere", "Sphere", compute_igraph_layout_sphere, apply_layout_matrix, free_layout_matrix},
	{"Layout/Geometric", "igraph_layout_star", "Star", compute_igraph_layout_star, apply_layout_matrix, free_layout_matrix},
	{"Layout/Geometric", "igraph_layout_grid", "Grid (2D)", compute_igraph_layout_grid, apply_layout_matrix, free_layout_matrix},
	{"Layout/Geometric", "igraph_layout_grid_3d", "Grid (3D)", compute_igraph_layout_grid_3d, apply_layout_matrix, free_layout_matrix},
	{"Layout/Geometric", "igraph_layout_random", "Random (2D)", compute_igraph_layout_random, apply_layout_matrix, free_layout_matrix},
	{"Layout/Geometric", "igraph_layout_random_3d", "Random (3D)", compute_igraph_layout_random_3d, apply_layout_matrix, free_layout_matrix},

	{"Layout/Bipartite", "igraph_layout_bipartite", "Sugiyama (Bipartite)", compute_igraph_layout_bipartite, apply_layout_matrix, free_layout_matrix},
	{"Layout/Bipartite", "igraph_layout_bipartite_simple", "Bipartite (Simple)", compute_igraph_layout_bipartite_simple, apply_layout_matrix, free_layout_matrix},

	{"Layout/Embedding", "igraph_layout_mds", "Torgerson MDS (2D)", compute_igraph_layout_mds, apply_layout_matrix, free_layout_matrix},
	{"Layout/Embedding", "igraph_layout_mds_3d", "Torgerson MDS (3D)", compute_igraph_layout_mds_3d, apply_layout_matrix, free_layout_matrix},
	{"Layout/Embedding", "igraph_layout_mds_spherical", "Spherical MDS (3D)", compute_igraph_layout_mds_spherical, apply_layout_matrix, free_layout_matrix},

	{"Layout/Embedding", "igraph_layout_umap_2d", "UMAP (2D)", compute_igraph_layout_umap, apply_layout_matrix, free_layout_matrix},
	{"Layout/Embedding", "igraph_layout_umap_3d", "UMAP (3D)", compute_igraph_layout_umap_3d, apply_layout_matrix, free_layout_matrix},
	{"Layout/Embedding", "igraph_layout_bhtsne", "t-SNE (Barnes-Hut) (2D)", compute_igraph_layout_bhtsne, apply_layout_matrix, free_layout_matrix},
	{"Layout/Embedding", "igraph_layout_bhtsne_3d", "t-SNE (Barnes-Hut) (3D)", compute_igraph_layout_bhtsne_3d, apply_layout_matrix, free_layout_matrix},

	{"Layout/Binary Classification", "igraph_layout_bcgl", "BCGL-t (2D)", compute_igraph_layout_bcgl, apply_layout_matrix, free_layout_matrix},
	{"Layout/Binary Classification", "igraph_layout_bcgl_3d", "BCGL-t (3D)", compute_igraph_layout_bcgl_3d, apply_layout_matrix, free_layout_matrix},
	{"Layout/Binary Classification", "lay_bcgl", "BCGL-t (3D GPU Compute)", compute_layout_bcgl, apply_layout_bcgl, free_layout_bcgl, poll_bcgl_gpu},

	{"Layout", "lay_layered_sphere", "Layered Sphere", compute_layout_layered_sphere, apply_layout_matrix, free_layout_matrix},

	{"Rank", "igraph_degree", "Degree", compute_igraph_degree, apply_centrality_scores, centrality_scores_free},
	{"Rank", "igraph_closeness_cutoff", "Closeness", compute_igraph_closeness_cutoff, apply_centrality_scores, centrality_scores_free},
	{"Rank", "igraph_betweenness", "Betweenness", compute_igraph_betweenness, apply_centrality_scores, centrality_scores_free},
	{"Rank", "igraph_eigenvector_centrality", "Eigenvector Centrality", compute_igraph_eigenvector_centrality, apply_centrality_scores, centrality_scores_free},
	{"Rank", "igraph_pagerank", "PageRank", compute_igraph_pagerank, apply_centrality_scores, centrality_scores_free},
	{"Rank", "igraph_hub_and_authority_scores", "HITS (Hub)", compute_igraph_hub_and_authority_scores, apply_centrality_scores, centrality_scores_free},
	{"Rank", "igraph_harmonic_centrality", "Harmonic", compute_igraph_harmonic_centrality, apply_centrality_scores, centrality_scores_free},
	{"Rank", "igraph_strength", "Strength (Weighted Degree)", compute_igraph_strength, apply_centrality_scores, centrality_scores_free},
	{"Rank", "igraph_constraint", "Constraint (Structural Holes)", compute_igraph_constraint, apply_centrality_scores, centrality_scores_free},
	{"Rank", "igraph_coreness", "Coreness (k-Core)", compute_igraph_coreness, apply_centrality_scores, centrality_scores_free},

	{"Group", "igraph_community_multilevel", "Louvain Method (Multilevel)", compute_igraph_community_multilevel, apply_community_membership, free_community_membership},
	{"Group", "igraph_community_leiden", "Leiden", compute_igraph_community_leiden, apply_community_membership, free_community_membership},
	{"Group", "igraph_community_walktrap", "Walktrap (Random walks)", compute_igraph_community_walktrap, apply_community_membership, free_community_membership},
	{"Group", "igraph_community_edge_betweenness", "Edge Betweenness (Girvan-Newman)", compute_igraph_community_edge_betweenness, apply_community_membership, free_community_membership},
	{"Group", "igraph_community_fastgreedy", "Fast Greedy", compute_igraph_community_fastgreedy, apply_community_membership, free_community_membership},
	{"Group", "igraph_community_infomap", "Infomap", compute_igraph_community_infomap, apply_community_membership, free_community_membership},
	{"Group", "igraph_community_label_propagation", "Label Propagation", compute_igraph_community_label_propagation, apply_community_membership, free_community_membership},
	{"Group", "igraph_community_spinglass", "Spinglass", compute_igraph_community_spinglass, apply_community_membership, free_community_membership},
	{"Group", "igraph_community_leading_eigenvector", "Leading Eigenvector", compute_igraph_community_leading_eigenvector, apply_community_membership, free_community_membership},
	{"Group", "igraph_community_optimal_modularity", "Optimal Modularity", compute_igraph_community_optimal_modularity, apply_community_membership, free_community_membership},
	{"Group", "igraph_community_voronoi", "Voronoi", compute_igraph_community_voronoi, apply_community_membership, free_community_membership},
	{"Group", "igraph_community_fluid_communities", "Fluid Communities", compute_igraph_community_fluid_communities, apply_community_membership, free_community_membership},

	{"Follow", "bfs_trigger", "Breadth-first search from single source", compute_inline_pass, apply_bfs_trigger, free_noop},
	{"Follow", "dfs_trigger", "Depth-first search from single source", compute_inline_pass, apply_dfs_trigger, free_noop},
	{"Follow", "topo_trigger", "Topological sort", compute_inline_pass, apply_topo_trigger, free_noop},
	{"Follow", "kcore_tree_trigger", "K-Core Tree", compute_kcore_tree_trigger, apply_kcore_tree_trigger, free_kcore_tree_result},
	{"Follow", "splc_animation", "Search Path Link Count", compute_splc_animation, apply_splc_animation, free_noop, poll_splc_gpu},
	{"Follow", "maxflow_sampled", "Max Flow (Sampled Pairs)", compute_maxflow_sampling, apply_maxflow_sampling, free_maxflow_result},

	{"", "graph_properties", "Properties", compute_graph_properties, apply_info_card, info_card_free},

	{"Structure", "igraph_diameter", "Diameter", compute_igraph_diameter, apply_info_card, info_card_free},
	{"Structure", "igraph_radius", "Radius", compute_igraph_radius, apply_info_card, info_card_free},
	{"Structure", "igraph_average_path_length", "Average Path Length", compute_igraph_average_path_length, apply_info_card, info_card_free},
	{"Structure", "igraph_assortativity_degree", "Assortativity", compute_igraph_assortativity_degree, apply_info_card, info_card_free},
	{"Structure", "igraph_density", "Density", compute_igraph_density, apply_info_card, info_card_free},
	{"Structure", "igraph_transitivity_undirected", "Transitivity (undirected)", compute_igraph_transitivity_undirected, apply_info_card, info_card_free},

	{"Alter", "remove_feedback_arc_set", "Remove feedback arc set", compute_remove_feedback_arc_set, apply_remove_feedback_arc_set, free_noop},
	{"Alter", "igraph_simplify", "Simplify (remove multi-edges & loops)", compute_igraph_simplify, apply_inplace_graph_update, free_noop},
	{"Alter", "to_directed", "To directed", compute_to_directed, apply_inplace_graph_update, free_noop},
	{"Alter", "to_undirected_collapse", "To undirected (collapse)", compute_to_undirected_collapse, apply_inplace_graph_update, free_noop},
	{"Alter", "to_undirected_mutual", "To undirected (mutual)", compute_to_undirected_mutual, apply_inplace_graph_update, free_noop},

	// Data menu — Repository (display_name=NULL: built by dynamic population)
	{"Data/Repository", "netzschleuder_download", NULL, run_netzschleuder_download, apply_netzschleuder_download, free_netzschleuder_download, NULL, (const CommandParamDef[]){{"entry_id", PARAM_TYPE_STRING, 0, 0, NULL, 0}, {"version_id", PARAM_TYPE_STRING, 0, 0, NULL, 0}}, 2},

	// Data menu — Famous (display_name=NULL: built by dynamic population)
	{"Data/Famous", "igraph_famous", NULL, compute_igraph_famous, apply_new_graph, free_new_graph, NULL, (const CommandParamDef[]){{"name", PARAM_TYPE_STRING, 0, 0, NULL, 0}}, 1},

	// Show menu — Filter (display_name=NULL: built by dynamic population)
	{"Show", "filter_show_all", NULL, compute_inline_pass, apply_filter_reset, free_noop},
	{"Show", "filter_by_attr", NULL, compute_filter_by_attr, apply_filter_by_attr, free_filter_params, NULL, (const CommandParamDef[]){{"attr_name", PARAM_TYPE_STRING, 0, 0, NULL, 0}, {"attr_value", PARAM_TYPE_STRING, 0, 0, NULL, 0}}, 2},

	// Root menu
	{"", NULL, "Node", NULL, NULL, NULL}, // branch anchor
	{"", "quit", "Quit", compute_inline_pass, apply_quit, free_noop},

};
const int g_command_registry_size = sizeof(g_command_registry) / sizeof(g_command_registry[0]);
