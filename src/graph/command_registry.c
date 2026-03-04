#include "command_registry.h"
#include "graph/wrappers_layout.h"

const CommandDef g_command_registry[] = {
    // =========================================================================
    // File menu
    // =========================================================================
    { "File", "file_new", "New Graph", NULL, NULL, NULL },
    { "File", "file_open", "Open Graph", NULL, NULL, NULL },
    { "File", "file_save", "Save Graph", NULL, NULL, NULL },
    { "File", "app_exit", "Exit", NULL, NULL, NULL },

    // =========================================================================
    // Generate menu - Deterministic Graphs
    // =========================================================================
    { "Generate/Deterministic Graphs", "gen_det_ring", "Ring", NULL, NULL, NULL },
    { "Generate/Deterministic Graphs", "gen_det_star", "Star", NULL, NULL, NULL },
    { "Generate/Deterministic Graphs", "gen_det_tree", "Tree", NULL, NULL, NULL },
    { "Generate/Deterministic Graphs", "gen_det_lattice", "Lattice", NULL, NULL, NULL },
    { "Generate/Deterministic Graphs", "gen_det_full", "Full Graph (Clique)", NULL, NULL, NULL },
    { "Generate/Deterministic Graphs", "gen_det_circle", "Circle", NULL, NULL, NULL },
    { "Generate/Deterministic Graphs", "gen_det_notable", "Notable Graphs", NULL, NULL, NULL },

    // =========================================================================
    // Generate menu - Stochastic Graphs
    // =========================================================================
    { "Generate/Stochastic Graphs", "gen_sto_er", "Erdős-Rényi (GNP / GNM)", NULL, NULL, NULL },
    { "Generate/Stochastic Graphs", "gen_sto_ba", "Barabási-Albert (Preferential attachment)", NULL, NULL, NULL },
    { "Generate/Stochastic Graphs", "gen_sto_ws", "Watts-Strogatz (Small-world)", NULL, NULL, NULL },
    { "Generate/Stochastic Graphs", "gen_sto_ff", "Forest Fire", NULL, NULL, NULL },
    { "Generate/Stochastic Graphs", "gen_sto_tree", "Random Tree", NULL, NULL, NULL },
    { "Generate/Stochastic Graphs", "gen_sto_deg", "Degree Sequence", NULL, NULL, NULL },

    // =========================================================================
    // Generate menu - Bipartite
    // =========================================================================
    { "Generate/Bipartite (Two-Mode) Graphs", "gen_bip_rand", "Generate Random Bipartite", NULL, NULL, NULL },
    { "Generate/Bipartite (Two-Mode) Graphs", "gen_bip_proj", "Create Bipartite Projections", NULL, NULL, NULL },

    // =========================================================================
    // Generate menu - Spatial
    // =========================================================================
    { "Generate/Spatial Graphs", "gen_spa_geo", "Geometric random graphs", NULL, NULL, NULL },
    { "Generate/Spatial Graphs", "gen_spa_gab", "Gabriel graphs", NULL, NULL, NULL },

    // =========================================================================
    // Layout menu - Force-Directed
    // =========================================================================
    { "Layout/Force-Directed", "lay_force_fr", "Fruchterman-Reingold", compute_lay_force_fr, apply_layout_matrix, free_layout_matrix },
    { "Layout/Force-Directed", "lay_force_kk", "Kamada-Kawai", compute_lay_force_kk, apply_layout_matrix, free_layout_matrix },
    { "Layout/Force-Directed", "lay_force_drl", "Distributed Recursive Layout (DrL)", compute_lay_force_drl, apply_layout_matrix, free_layout_matrix },
    { "Layout/Force-Directed", "lay_force_dh", "Davidson-Harel", compute_lay_force_dh, apply_layout_matrix, free_layout_matrix },
    { "Layout/Force-Directed", "lay_force_go", "GraphOpt", NULL, NULL, NULL },

    // =========================================================================
    // Layout menu - Tree & Hierarchical
    // =========================================================================
    { "Layout/Tree & Hierarchical", "lay_tree_rt", "Reingold-Tilford", compute_lay_tree_rt, apply_layout_matrix, free_layout_matrix },
    { "Layout/Tree & Hierarchical", "lay_tree_sug", "Sugiyama", compute_lay_tree_sug, apply_layout_matrix, free_layout_matrix },

    // =========================================================================
    // Layout menu - Geometric
    // =========================================================================
    { "Layout/Geometric", "lay_geo_circle", "Circle", compute_lay_geo_circle, apply_layout_matrix, free_layout_matrix },
    { "Layout/Geometric", "lay_geo_star", "Star", compute_lay_geo_star, apply_layout_matrix, free_layout_matrix },
    { "Layout/Geometric", "lay_geo_grid", "Grid", compute_lay_geo_grid, apply_layout_matrix, free_layout_matrix },
    { "Layout/Geometric", "lay_geo_sphere", "Sphere", compute_lay_geo_sphere, apply_layout_matrix, free_layout_matrix },
    { "Layout/Geometric", "lay_geo_rand", "Random", compute_lay_geo_rand, apply_layout_matrix, free_layout_matrix },

    // =========================================================================
    // Layout menu - Bipartite
    // =========================================================================
    { "Layout/Bipartite Layouts", "lay_bip_mds", "MDS", compute_lay_bip_mds, apply_layout_matrix, free_layout_matrix },
    { "Layout/Bipartite Layouts", "lay_bip_sug", "Sugiyama (Bipartite)", compute_lay_bip_sug, apply_layout_matrix, free_layout_matrix },

    // =========================================================================
    // Analysis menu - Centrality & Roles
    // =========================================================================
    { "Analysis/Centrality & Roles", "ana_cent_deg", "Degree", NULL, NULL, NULL },
    { "Analysis/Centrality & Roles", "ana_cent_clo", "Closeness", NULL, NULL, NULL },
    { "Analysis/Centrality & Roles", "ana_cent_btw", "Betweenness (Node & Edge)", NULL, NULL, NULL },
    { "Analysis/Centrality & Roles", "ana_cent_eig", "Eigenvector Centrality", NULL, NULL, NULL },
    { "Analysis/Centrality & Roles", "ana_cent_pager", "PageRank", NULL, NULL, NULL },
    { "Analysis/Centrality & Roles", "ana_cent_hits", "Hubs & Authorities (HITS)", NULL, NULL, NULL },

    // =========================================================================
    // Analysis menu - Global Network Properties
    // =========================================================================
    { "Analysis/Global Network Properties", "ana_glob_diam", "Diameter", NULL, NULL, NULL },
    { "Analysis/Global Network Properties", "ana_glob_rad", "Radius", NULL, NULL, NULL },
    { "Analysis/Global Network Properties", "ana_glob_apl", "Average Path Length", NULL, NULL, NULL },
    { "Analysis/Global Network Properties", "ana_glob_assort", "Assortativity", NULL, NULL, NULL },
    { "Analysis/Global Network Properties", "ana_glob_mod", "Modularity", NULL, NULL, NULL },
    { "Analysis/Global Network Properties", "ana_glob_dens", "Density", NULL, NULL, NULL },
    { "Analysis/Global Network Properties", "ana_glob_trans", "Transitivity", NULL, NULL, NULL },

    // =========================================================================
    // Analysis menu - Distances & Paths
    // =========================================================================
    { "Analysis/Distances & Paths/Shortest Paths", "ana_dist_sp_dij", "Dijkstra", NULL, NULL, NULL },
    { "Analysis/Distances & Paths/Shortest Paths", "ana_dist_sp_bf", "Bellman-Ford", NULL, NULL, NULL },
    { "Analysis/Distances & Paths/Shortest Paths", "ana_dist_sp_astar", "A*", NULL, NULL, NULL },
    { "Analysis/Distances & Paths/Shortest Paths", "ana_dist_sp_joh", "Johnson", NULL, NULL, NULL },
    { "Analysis/Distances & Paths", "ana_dist_ecc", "Eccentricity", NULL, NULL, NULL },

    // =========================================================================
    // Analysis menu - Flows & Cuts
    // =========================================================================
    { "Analysis/Flows & Cuts", "ana_flow_max", "Maximum Flow", NULL, NULL, NULL },
    { "Analysis/Flows & Cuts", "ana_flow_min", "Minimum Cut", NULL, NULL, NULL },
    { "Analysis/Flows & Cuts", "ana_flow_gom", "Gomory-Hu Tree", NULL, NULL, NULL },
    { "Analysis/Flows & Cuts", "ana_flow_adh", "Adhesion / Cohesion", NULL, NULL, NULL },

    // =========================================================================
    // Analysis menu - Graph Embedding
    // =========================================================================
    { "Analysis/Graph Embedding", "ana_emb_adj", "Adjacency Spectral Embedding", NULL, NULL, NULL },
    { "Analysis/Graph Embedding", "ana_emb_lap", "Laplacian Spectral Embedding", NULL, NULL, NULL },

    // =========================================================================
    // Topology menu - Components & Connectivity
    // =========================================================================
    { "Topology & Motifs/Components & Connectivity", "top_comp_conn", "Connected Components", NULL, NULL, NULL },
    { "Topology & Motifs/Components & Connectivity", "top_comp_bicon", "Biconnected Components", NULL, NULL, NULL },
    { "Topology & Motifs/Components & Connectivity", "top_comp_art", "Articulation Points", NULL, NULL, NULL },
    { "Topology & Motifs/Components & Connectivity", "top_comp_sep", "Vertex Separators", NULL, NULL, NULL },

    // =========================================================================
    // Topology menu - Cliques & Independent Sets
    // =========================================================================
    { "Topology & Motifs/Cliques & Independent Sets", "top_cliq_maxl", "Maximal Cliques", NULL, NULL, NULL },
    { "Topology & Motifs/Cliques & Independent Sets", "top_cliq_maxm", "Maximum Cliques", NULL, NULL, NULL },
    { "Topology & Motifs/Cliques & Independent Sets", "top_cliq_ind", "Largest Independent Vertex Sets", NULL, NULL, NULL },

    // =========================================================================
    // Topology menu - Cycles & Paths
    // =========================================================================
    { "Topology & Motifs/Cycles & Paths", "top_cyc_eul", "Eulerian Paths", NULL, NULL, NULL },
    { "Topology & Motifs/Cycles & Paths", "top_cyc_fund", "Fundamental Cycles", NULL, NULL, NULL },

    // =========================================================================
    // Topology menu - Motifs & Censuses
    // =========================================================================
    { "Topology & Motifs/Motifs & Censuses", "top_mot_tri", "Triad Census", NULL, NULL, NULL },
    { "Topology & Motifs/Motifs & Censuses", "top_mot_dya", "Dyad Census", NULL, NULL, NULL },
    { "Topology & Motifs/Motifs & Censuses", "top_mot_net", "Network Motifs", NULL, NULL, NULL },

    // =========================================================================
    // Topology menu - Graph Isomorphism
    // =========================================================================
    { "Topology & Motifs/Graph Isomorphism", "top_iso_vf2", "VF2", NULL, NULL, NULL },
    { "Topology & Motifs/Graph Isomorphism", "top_iso_bliss", "Bliss", NULL, NULL, NULL },

    // =========================================================================
    // Topology menu - Graph Coloring
    // =========================================================================
    { "Topology & Motifs/Graph Coloring", "top_col_greedy", "Greedy coloring", NULL, NULL, NULL },

    // =========================================================================
    // Communities menu - Detection
    // =========================================================================
    { "Communities/Detection", "com_det_louv", "Louvain Method (Multilevel)", NULL, NULL, NULL },
    { "Communities/Detection", "com_det_walk", "Walktrap (Random walks)", NULL, NULL, NULL },
    { "Communities/Detection", "com_det_btw", "Edge Betweenness (Girvan-Newman)", NULL, NULL, NULL },
    { "Communities/Detection", "com_det_fast", "Fast Greedy", NULL, NULL, NULL },
    { "Communities/Detection", "com_det_info", "Infomap", NULL, NULL, NULL },
    { "Communities/Detection", "com_det_lab", "Label Propagation", NULL, NULL, NULL },
    { "Communities/Detection", "com_det_spin", "Spinglass", NULL, NULL, NULL },

    // =========================================================================
    // Communities menu - Compare
    // =========================================================================
    { "Communities/Compare", "com_comp_sj", "Split-join distance", NULL, NULL, NULL },
    { "Communities/Compare", "com_comp_ari", "Adjusted Rand Index", NULL, NULL, NULL },

    // =========================================================================
    // Communities menu - Graphlets
    // =========================================================================
    { "Communities/Graphlets", "com_glet_basis", "basis", NULL, NULL, NULL },
    { "Communities/Graphlets", "com_glet_proj", "projections", NULL, NULL, NULL },

    // =========================================================================
    // Communities menu - Hierarchical Random Graphs
    // =========================================================================
    { "Communities/Hierarchical Random Graphs", "com_hrg_fit", "Fit", NULL, NULL, NULL },
    { "Communities/Hierarchical Random Graphs", "com_hrg_cons", "consensus trees", NULL, NULL, NULL },

    // =========================================================================
    // Processes menu - Search & Traversal
    // =========================================================================
    { "Processes & Traversal/Search & Traversal", "pro_search_bfs", "Breadth-First Search", NULL, NULL, NULL },
    { "Processes & Traversal/Search & Traversal", "pro_search_dfs", "Depth-First Search", NULL, NULL, NULL },
    { "Processes & Traversal/Search & Traversal", "pro_search_topo", "Topological Sorting", NULL, NULL, NULL },

    // =========================================================================
    // Processes menu - Processes on Graphs
    // =========================================================================
    { "Processes & Traversal/Processes on Graphs", "pro_proc_rw", "Simulate Random Walks", NULL, NULL, NULL },
    { "Processes & Traversal/Processes on Graphs", "pro_proc_rt", "Rank / transition behaviors", NULL, NULL, NULL },
};

const int g_command_registry_size = sizeof(g_command_registry) / sizeof(g_command_registry[0]);
