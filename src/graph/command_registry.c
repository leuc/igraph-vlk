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
	{"Generate/Deterministic Graphs", "gen_det_ring", "Ring", compute_gen_ring, apply_new_graph, free_new_graph},
	{"Generate/Deterministic Graphs", "gen_det_star", "Star", compute_gen_star, apply_new_graph, free_new_graph},
	{"Generate/Deterministic Graphs", "gen_det_tree", "Tree", compute_gen_tree, apply_new_graph, free_new_graph},
	{"Generate/Deterministic Graphs", "gen_det_lattice", "Lattice", compute_gen_lattice, apply_new_graph, free_new_graph},
	{"Generate/Deterministic Graphs", "gen_det_full", "Full Graph (Clique)", compute_gen_full, apply_new_graph, free_new_graph},
	{"Generate/Deterministic Graphs", "gen_det_circle", "Circle", compute_gen_circle, apply_new_graph, free_new_graph},
	{"Generate/Deterministic Graphs", "gen_det_notable", "Notable Graphs", compute_gen_notable, apply_new_graph, free_new_graph},

	// =========================================================================
	// Generate menu - Stochastic Graphs
	// =========================================================================
	{"Generate/Stochastic Graphs", "gen_sto_er", "Erdős-Rényi (GNP / GNM)", compute_gen_er, apply_new_graph, free_new_graph},
	{"Generate/Stochastic Graphs", "gen_sto_ba", "Barabási-Albert (Preferential attachment)", compute_gen_ba, apply_new_graph, free_new_graph},
	{"Generate/Stochastic Graphs", "gen_sto_ws", "Watts-Strogatz (Small-world)", compute_gen_ws, apply_new_graph, free_new_graph},
	{"Generate/Stochastic Graphs", "gen_sto_ff", "Forest Fire", compute_gen_forest_fire, apply_new_graph, free_new_graph},
	{"Generate/Stochastic Graphs", "gen_sto_tree", "Random Tree", compute_gen_random_tree, apply_new_graph, free_new_graph},
	{"Generate/Stochastic Graphs", "gen_sto_deg", "Degree Sequence", compute_gen_degree_seq, apply_new_graph, free_new_graph},

	// =========================================================================
	// Generate menu - Bipartite
	// =========================================================================
	{"Generate/Bipartite (Two-Mode) Graphs", "gen_bip_rand", "Generate Random Bipartite", compute_gen_bipartite_random, apply_new_graph, free_new_graph},
	{"Generate/Bipartite (Two-Mode) Graphs", "gen_bip_proj", "Create Bipartite Projections", compute_gen_bipartite_projection, apply_new_graph, free_new_graph},

	// =========================================================================
	// Generate menu - Spatial
	// =========================================================================
	{"Generate/Spatial Graphs", "gen_spa_geo", "Geometric random graphs", compute_gen_geometric, apply_new_graph, free_new_graph},
	{"Generate/Spatial Graphs", "gen_spa_gab", "Gabriel graphs", compute_gen_gabriel, apply_new_graph, free_new_graph},

	// =========================================================================
	// Layout menu - Force-Directed
	// =========================================================================
	{"Layout/Force-Directed", "lay_force_fr", "Fruchterman-Reingold (3D)", compute_lay_force_fr, apply_layout_matrix, free_layout_matrix},
	{"Layout/Force-Directed", "lay_force_fr_2d", "Fruchterman-Reingold (2D)", compute_lay_force_fr_2d, apply_layout_matrix, free_layout_matrix},
	{"Layout/Force-Directed", "lay_force_kk", "Kamada-Kawai (3D)", compute_lay_force_kk, apply_layout_matrix, free_layout_matrix},
	{"Layout/Force-Directed", "lay_force_kk_2d", "Kamada-Kawai (2D)", compute_lay_force_kk_2d, apply_layout_matrix, free_layout_matrix},
	{"Layout/Force-Directed", "lay_force_drl", "Distributed Recursive Layout (DrL) (3D)", compute_lay_force_drl, apply_layout_matrix, free_layout_matrix},
	{"Layout/Force-Directed", "lay_force_drl_2d", "Distributed Recursive Layout (DrL) (2D)", compute_lay_force_drl_2d, apply_layout_matrix, free_layout_matrix},
	{"Layout/Force-Directed", "lay_force_dh", "Davidson-Harel", compute_lay_force_dh, apply_layout_matrix, free_layout_matrix},
	{"Layout/Force-Directed", "lay_force_go", "GraphOpt", compute_lay_force_go, apply_layout_matrix, free_layout_matrix},
	{"Layout/Force-Directed", "lay_force_lgl", "Large Graph Layout (LGL)", compute_lay_force_lgl, apply_layout_matrix, free_layout_matrix},
	{"Layout/Force-Directed", "lay_force_gem", "GEM", compute_lay_force_gem, apply_layout_matrix_centered, free_layout_matrix},
	{"Layout", "lay_layered_sphere", "Layered Sphere", compute_lay_layered_sphere, apply_layout_matrix, free_layout_matrix},

	// =========================================================================
	// Layout menu - Tree & Hierarchical
	// =========================================================================
	{"Layout/Tree & Hierarchical", "lay_tree_rt", "Reingold-Tilford", compute_lay_tree_rt, apply_layout_matrix, free_layout_matrix},
	{"Layout/Tree & Hierarchical", "lay_tree_sug", "Sugiyama", compute_lay_tree_sug, apply_layout_matrix, free_layout_matrix},

	// =========================================================================
	// Layout menu - Geometric
	// =========================================================================
	{"Layout/Geometric", "lay_geo_circle", "Circle (3D)", compute_lay_geo_circle, apply_layout_matrix, free_layout_matrix},
	{"Layout/Geometric", "lay_geo_circle_2d", "Circle (2D)", compute_lay_geo_circle_2d, apply_layout_matrix, free_layout_matrix},
	{"Layout/Geometric", "lay_geo_star", "Star", compute_lay_geo_star, apply_layout_matrix, free_layout_matrix},
	{"Layout/Geometric", "lay_geo_grid", "Grid (3D)", compute_lay_geo_grid, apply_layout_matrix, free_layout_matrix},
	{"Layout/Geometric", "lay_geo_grid_2d", "Grid (2D)", compute_lay_geo_grid_2d, apply_layout_matrix, free_layout_matrix},
	{"Layout/Geometric", "lay_geo_sphere", "Sphere", compute_lay_geo_sphere, apply_layout_matrix, free_layout_matrix},
	{"Layout/Geometric", "lay_geo_rand", "Random (3D)", compute_lay_geo_rand, apply_layout_matrix, free_layout_matrix},
	{"Layout/Geometric", "lay_geo_rand_2d", "Random (2D)", compute_lay_geo_rand_2d, apply_layout_matrix, free_layout_matrix},

	// =========================================================================
	// Layout menu - Bipartite Layouts
	// =========================================================================
	{"Layout/Bipartite Layouts", "lay_bip_mds", "MDS", compute_lay_bip_mds, apply_layout_matrix, free_layout_matrix},
	{"Layout/Bipartite Layouts", "lay_bip_sug", "Sugiyama (Bipartite)", compute_lay_bip_sug, apply_layout_matrix, free_layout_matrix},
	{"Layout/Bipartite Layouts", "lay_bip_simple", "Bipartite (Simple)", compute_lay_bip_simple, apply_layout_matrix, free_layout_matrix},

	// =========================================================================
	// Layout menu - Graph Embedding
	// =========================================================================
	{"Layout/Graph Embedding", "lay_emb_umap_2d", "UMAP (2D)", compute_lay_umap_2d, apply_layout_matrix, free_layout_matrix},
	{"Layout/Graph Embedding", "lay_emb_umap_3d", "UMAP (3D)", compute_lay_umap, apply_layout_matrix, free_layout_matrix},

	// =========================================================================
	// Analysis menu - Centrality & Roles
	// =========================================================================
	{"Analysis/Centrality & Roles", "ana_cent_deg", "Degree", compute_cent_degree, apply_centrality_scores, free_centrality_scores},
	{"Analysis/Centrality & Roles", "ana_cent_clo", "Closeness", compute_cent_closeness, apply_centrality_scores, free_centrality_scores},
	{"Analysis/Centrality & Roles", "ana_cent_btw", "Betweenness", compute_cent_betweenness, apply_centrality_scores, free_centrality_scores},
	{"Analysis/Centrality & Roles", "ana_cent_eig", "Eigenvector Centrality", compute_cent_eigenvector, apply_centrality_scores, free_centrality_scores},
	{"Analysis/Centrality & Roles", "ana_cent_pager", "PageRank", compute_cent_pagerank, apply_centrality_scores, free_centrality_scores},
	{"Analysis/Centrality & Roles", "ana_cent_hits", "HITS (Hub)", compute_cent_hits, apply_centrality_scores, free_centrality_scores},
	{"Analysis/Centrality & Roles", "ana_cent_harm", "Harmonic", compute_cent_harmonic, apply_centrality_scores, free_centrality_scores},
	{"Analysis/Centrality & Roles", "ana_cent_strength", "Strength (Weighted Degree)", compute_cent_strength, apply_centrality_scores, free_centrality_scores},
	{"Analysis/Centrality & Roles", "ana_cent_constraint", "Constraint (Structural Holes)", compute_cent_constraint, apply_centrality_scores, free_centrality_scores},

	// =========================================================================
	// Analysis menu - Global Network Properties
	// =========================================================================
	{"Analysis/Global Network Properties", "ana_glob_diam", "Diameter", compute_ana_diameter, apply_info_card, free_info_card},
	{"Analysis/Global Network Properties", "ana_glob_rad", "Radius", compute_ana_glob_rad, apply_info_card, free_info_card},
	{"Analysis/Global Network Properties", "ana_glob_apl", "Average Path Length", compute_ana_glob_apl, apply_info_card, free_info_card},
	{"Analysis/Global Network Properties", "ana_glob_assort", "Assortativity", compute_ana_glob_assort, apply_info_card, free_info_card},
	{"Analysis/Global Network Properties", "ana_glob_dens", "Density", compute_ana_glob_dens, apply_info_card, free_info_card},
	{"Analysis/Global Network Properties", "ana_glob_trans", "Transitivity (undirected)", compute_ana_glob_trans, apply_info_card, free_info_card},

	// =========================================================================
	// Analysis menu - Distances & Paths
	// =========================================================================
	{"Analysis/Distances & Paths/Shortest Paths", "ana_dist_sp_dij", "Dijkstra", NULL, NULL, NULL},
	{"Analysis/Distances & Paths/Shortest Paths", "ana_dist_sp_bf", "Bellman-Ford", NULL, NULL, NULL},
	{"Analysis/Distances & Paths/Shortest Paths", "ana_dist_sp_astar", "A*", NULL, NULL, NULL},
	{"Analysis/Distances & Paths/Shortest Paths", "ana_dist_sp_joh", "Johnson", NULL, NULL, NULL},
	{"Analysis/Distances & Paths", "ana_dist_ecc", "Eccentricity", NULL, NULL, NULL},

	// =========================================================================
	// Analysis menu - Flows & Cuts
	// =========================================================================
	{"Analysis/Flows & Cuts", "ana_flow_max", "Maximum Flow", NULL, NULL, NULL},
	{"Analysis/Flows & Cuts", "ana_flow_min", "Minimum Cut", NULL, NULL, NULL},
	{"Analysis/Flows & Cuts", "ana_flow_gom", "Gomory-Hu Tree", NULL, NULL, NULL},
	{"Analysis/Flows & Cuts", "ana_flow_adh", "Adhesion / Cohesion", NULL, NULL, NULL},

	// =========================================================================
	// Analysis menu - Graph Embedding
	// =========================================================================
	{"Analysis/Graph Embedding", "ana_emb_adj", "Adjacency Spectral Embedding", NULL, NULL, NULL},
	{"Analysis/Graph Embedding", "ana_emb_lap", "Laplacian Spectral Embedding", NULL, NULL, NULL},

	// =========================================================================
	// Topology menu - Components & Connectivity
	// =========================================================================
	{"Topology & Motifs/Components & Connectivity", "top_comp_conn", "Connected Components", NULL, NULL, NULL},
	{"Topology & Motifs/Components & Connectivity", "top_comp_bicon", "Biconnected Components", NULL, NULL, NULL},
	{"Topology & Motifs/Components & Connectivity", "top_comp_art", "Articulation Points", NULL, NULL, NULL},
	{"Topology & Motifs/Components & Connectivity", "top_comp_sep", "Vertex Separators", NULL, NULL, NULL},

	// =========================================================================
	// Topology menu - Cliques & Independent Sets
	// =========================================================================
	{"Topology & Motifs/Cliques & Independent Sets", "top_cliq_maxl", "Maximal Cliques", NULL, NULL, NULL},
	{"Topology & Motifs/Cliques & Independent Sets", "top_cliq_maxm", "Maximum Cliques", NULL, NULL, NULL},
	{"Topology & Motifs/Cliques & Independent Sets", "top_cliq_ind", "Largest Independent Vertex Sets", NULL, NULL, NULL},

	// =========================================================================
	// Topology menu - Cycles & Paths
	// =========================================================================
	{"Topology & Motifs/Cycles & Paths", "top_cyc_eul", "Eulerian Paths", NULL, NULL, NULL},
	{"Topology & Motifs/Cycles & Paths", "top_cyc_fund", "Fundamental Cycles", NULL, NULL, NULL},

	// =========================================================================
	// Topology menu - Motifs & Censuses
	// =========================================================================
	{"Topology & Motifs/Motifs & Censuses", "top_mot_tri", "Triad Census", NULL, NULL, NULL},
	{"Topology & Motifs/Motifs & Censuses", "top_mot_dya", "Dyad Census", NULL, NULL, NULL},
	{"Topology & Motifs/Motifs & Censuses", "top_mot_net", "Network Motifs", NULL, NULL, NULL},

	// =========================================================================
	// Topology menu - Graph Isomorphism
	// =========================================================================
	{"Topology & Motifs/Graph Isomorphism", "top_iso_vf2", "VF2", NULL, NULL, NULL},
	{"Topology & Motifs/Graph Isomorphism", "top_iso_bliss", "Bliss", NULL, NULL, NULL},

	// =========================================================================
	// Topology menu - Graph Coloring
	// =========================================================================
	{"Topology & Motifs/Graph Coloring", "top_col_greedy", "Greedy coloring", NULL, NULL, NULL},

	// =========================================================================
	// Communities menu - Detection
	// =========================================================================
	{"Communities/Detection", "com_det_louv", "Louvain Method (Multilevel)", compute_com_multilevel, apply_community_membership, free_community_membership},
	{"Communities/Detection", "com_det_leiden", "Leiden", compute_com_leiden, apply_community_membership, free_community_membership},
	{"Communities/Detection", "com_det_walk", "Walktrap (Random walks)", compute_com_walktrap, apply_community_membership, free_community_membership},
	{"Communities/Detection", "com_det_btw", "Edge Betweenness (Girvan-Newman)", compute_com_edge_betweenness, apply_community_membership, free_community_membership},
	{"Communities/Detection", "com_det_fast", "Fast Greedy", compute_com_fastgreedy, apply_community_membership, free_community_membership},
	{"Communities/Detection", "com_det_info", "Infomap", compute_com_infomap, apply_community_membership, free_community_membership},
	{"Communities/Detection", "com_det_lab", "Label Propagation", compute_com_label_prop, apply_community_membership, free_community_membership},
	{"Communities/Detection", "com_det_spin", "Spinglass", compute_com_spinglass, apply_community_membership, free_community_membership},
	{"Communities/Detection", "com_det_eig", "Leading Eigenvector", compute_com_leading_eigenvector, apply_community_membership, free_community_membership},
	{"Communities/Detection", "com_det_opt", "Optimal Modularity", compute_com_optimal_modularity, apply_community_membership, free_community_membership},
	{"Communities/Detection", "com_det_vor", "Voronoi", compute_com_voronoi, apply_community_membership, free_community_membership},
	{"Communities/Detection", "com_det_fluid", "Fluid Communities", compute_com_fluid, apply_community_membership, free_community_membership},

	// =========================================================================
	// Communities menu - Compare
	// =========================================================================
	{"Communities/Compare", "com_comp_sj", "Split-join distance", NULL, NULL, NULL},
	{"Communities/Compare", "com_comp_ari", "Adjusted Rand Index", NULL, NULL, NULL},

	// =========================================================================
	// Communities menu - Graphlets
	// =========================================================================
	{"Communities/Graphlets", "com_glet_basis", "basis", NULL, NULL, NULL},
	{"Communities/Graphlets", "com_glet_proj", "projections", NULL, NULL, NULL},

	// =========================================================================
	// Communities menu - Hierarchical Random Graphs
	// =========================================================================
	{"Communities/Hierarchical Random Graphs", "com_hrg_fit", "Fit", NULL, NULL, NULL},
	{"Communities/Hierarchical Random Graphs", "com_hrg_cons", "consensus trees", NULL, NULL, NULL},

	// =========================================================================
	// Processes menu - Search & Traversal
	// =========================================================================
	{"Processes & Traversal/Search & Traversal", "pro_search_bfs", "Breadth-First Search", NULL, NULL, NULL},
	{"Processes & Traversal/Search & Traversal", "pro_search_dfs", "Depth-First Search", NULL, NULL, NULL},
	{"Processes & Traversal/Search & Traversal", "pro_search_topo", "Topological Sorting", NULL, NULL, NULL},

	// =========================================================================
	// Processes menu - Processes on Graphs
	// =========================================================================
	{"Processes & Traversal/Processes on Graphs", "pro_proc_rw", "Simulate Random Walks", NULL, NULL, NULL},
	{"Processes & Traversal/Processes on Graphs", "pro_proc_rt", "Rank / transition behaviors", NULL, NULL, NULL},
};
const int g_command_registry_size = sizeof(g_command_registry) / sizeof(g_command_registry[0]);
