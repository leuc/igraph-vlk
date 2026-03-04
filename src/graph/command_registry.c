#include "command_registry.h"
#include "graph/wrappers_layout.h"

const CommandDef g_command_registry[] = {
    // =========================================================================
    // File menu (level 1)
    // =========================================================================
    { "", "file_new", "New Graph", NULL, NULL, NULL },
    { "", "file_open", "Open Graph", NULL, NULL, NULL },
    { "", "file_save", "Save Graph", NULL, NULL, NULL },
    { "", "app_exit", "Exit", NULL, NULL, NULL },

    // =========================================================================
    // Generate menu (level 1-3)
    // =========================================================================
    { "Generate", "gen_deterministic", "Deterministic Graphs", NULL, NULL, NULL },
    { "Generate", "gen_stochastic", "Stochastic Graphs", NULL, NULL, NULL },
    { "Generate", "gen_bipartite", "Bipartite (Two-Mode) Graphs", NULL, NULL, NULL },
    { "Generate", "gen_spatial", "Spatial Graphs", NULL, NULL, NULL },

    // Deterministic Graphs (level 2)
    { "Generate/Deterministic Graphs", "gen_det_ring", "Ring", NULL, NULL, NULL },
    { "Generate/Deterministic Graphs", "gen_det_star", "Star", NULL, NULL, NULL },
    { "Generate/Deterministic Graphs", "gen_det_tree", "Tree", NULL, NULL, NULL },
    { "Generate/Deterministic Graphs", "gen_det_lattice", "Lattice", NULL, NULL, NULL },
    { "Generate/Deterministic Graphs", "gen_det_full", "Full Graph (Clique)", NULL, NULL, NULL },
    { "Generate/Deterministic Graphs", "gen_det_circle", "Circle", NULL, NULL, NULL },
    { "Generate/Deterministic Graphs", "gen_det_notable", "Notable Graphs", NULL, NULL, NULL },

    // Stochastic Graphs (level 2)
    { "Generate/Stochastic Graphs", "gen_sto_er", "Erdős-Rényi (GNP / GNM)", NULL, NULL, NULL },
    { "Generate/Stochastic Graphs", "gen_sto_ba", "Barabási-Albert (Preferential attachment)", NULL, NULL, NULL },
    { "Generate/Stochastic Graphs", "gen_sto_ws", "Watts-Strogatz (Small-world)", NULL, NULL, NULL },
    { "Generate/Stochastic Graphs", "gen_sto_ff", "Forest Fire", NULL, NULL, NULL },
    { "Generate/Stochastic Graphs", "gen_sto_tree", "Random Tree", NULL, NULL, NULL },
    { "Generate/Stochastic Graphs", "gen_sto_deg", "Degree Sequence", NULL, NULL, NULL },

    // Bipartite (level 2)
    { "Generate/Bipartite (Two-Mode) Graphs", "gen_bip_rand", "Generate Random Bipartite", NULL, NULL, NULL },
    { "Generate/Bipartite (Two-Mode) Graphs", "gen_bip_proj", "Create Bipartite Projections", NULL, NULL, NULL },

    // Spatial Graphs (level 2)
    { "Generate/Spatial Graphs", "gen_spa_geo", "Geometric random graphs", NULL, NULL, NULL },
    { "Generate/Spatial Graphs", "gen_spa_gab", "Gabriel graphs", NULL, NULL, NULL },

    // =========================================================================
    // Layout menu (level 1-3)
    // =========================================================================
    { "Layout", "layout_force", "Force-Directed", NULL, NULL, NULL },
    { "Layout", "layout_tree", "Tree & Hierarchical", NULL, NULL, NULL },
    { "Layout", "layout_geo", "Geometric", NULL, NULL, NULL },
    { "Layout", "layout_bip", "Bipartite Layouts", NULL, NULL, NULL },

    // Force-Directed (level 2)
    { "Layout/Force-Directed", "lay_force_fr", "Fruchterman-Reingold", compute_lay_force_fr, apply_layout_matrix, free_layout_matrix },
    { "Layout/Force-Directed", "lay_force_kk", "Kamada-Kawai", compute_lay_force_kk, apply_layout_matrix, free_layout_matrix },
    { "Layout/Force-Directed", "lay_force_drl", "Distributed Recursive Layout (DrL)", compute_lay_force_drl, apply_layout_matrix, free_layout_matrix },
    { "Layout/Force-Directed", "lay_force_dh", "Davidson-Harel", compute_lay_force_dh, apply_layout_matrix, free_layout_matrix },
    { "Layout/Force-Directed", "lay_force_go", "GraphOpt", NULL, NULL, NULL },

    // Tree & Hierarchical (level 2)
    { "Layout/Tree & Hierarchical", "lay_tree_rt", "Reingold-Tilford", compute_lay_tree_rt, apply_layout_matrix, free_layout_matrix },
    { "Layout/Tree & Hierarchical", "lay_tree_sug", "Sugiyama", compute_lay_tree_sug, apply_layout_matrix, free_layout_matrix },

    // Geometric (level 2)
    { "Layout/Geometric", "lay_geo_circle", "Circle", compute_lay_geo_circle, apply_layout_matrix, free_layout_matrix },
    { "Layout/Geometric", "lay_geo_star", "Star", compute_lay_geo_star, apply_layout_matrix, free_layout_matrix },
    { "Layout/Geometric", "lay_geo_grid", "Grid", compute_lay_geo_grid, apply_layout_matrix, free_layout_matrix },
    { "Layout/Geometric", "lay_geo_sphere", "Sphere", compute_lay_geo_sphere, apply_layout_matrix, free_layout_matrix },
    { "Layout/Geometric", "lay_geo_rand", "Random", compute_lay_geo_rand, apply_layout_matrix, free_layout_matrix },

    // Bipartite Layouts (level 2)
    { "Layout/Bipartite Layouts", "lay_bip_mds", "MDS", compute_lay_bip_mds, apply_layout_matrix, free_layout_matrix },
    { "Layout/Bipartite Layouts", "lay_bip_sug", "Sugiyama (Bipartite)", compute_lay_bip_sug, apply_layout_matrix, free_layout_matrix },

    // =========================================================================
    // Analysis menu (level 1-4)
    // =========================================================================
    { "Analysis", "analysis_cent", "Centrality & Roles", NULL, NULL, NULL },
    { "Analysis", "analysis_glob", "Global Network Properties", NULL, NULL, NULL },
    { "Analysis", "analysis_dist", "Distances & Paths", NULL, NULL, NULL },
    { "Analysis", "analysis_flow", "Flows & Cuts", NULL, NULL, NULL },
    { "Analysis", "analysis_emb", "Graph Embedding", NULL, NULL, NULL },

    // Centrality & Roles (level 2)
    { "Analysis/Centrality & Roles", "ana_cent_deg", "Degree", NULL, NULL, NULL },
    { "Analysis/Centrality & Roles", "ana_cent_clo", "Closeness", NULL, NULL, NULL },
    { "Analysis/Centrality & Roles", "ana_cent_btw", "Betweenness (Node & Edge)", NULL, NULL, NULL },
    { "Analysis/Centrality & Roles", "ana_cent_eig", "Eigenvector Centrality", NULL, NULL, NULL },
    { "Analysis/Centrality & Roles", "ana_cent_pager", "PageRank", NULL, NULL, NULL },
    { "Analysis/Centrality & Roles", "ana_cent_hits", "Hubs & Authorities (HITS)", NULL, NULL, NULL },

    // Global Network Properties (level 2)
    { "Analysis/Global Network Properties", "ana_glob_diam", "Diameter", NULL, NULL, NULL },
    { "Analysis/Global Network Properties", "ana_glob_rad", "Radius", NULL, NULL, NULL },
    { "Analysis/Global Network Properties", "ana_glob_apl", "Average Path Length", NULL, NULL, NULL },
    { "Analysis/Global Network Properties", "ana_glob_assort", "Assortativity", NULL, NULL, NULL },
    { "Analysis/Global Network Properties", "ana_glob_mod", "Modularity", NULL, NULL, NULL },
    { "Analysis/Global Network Properties", "ana_glob_dens", "Density", NULL, NULL, NULL },
    { "Analysis/Global Network Properties", "ana_glob_trans", "Transitivity", NULL, NULL, NULL },

    // Distances & Paths (level 2)
    { "Analysis/Distances & Paths", "analysis_sp", "Shortest Paths", NULL, NULL, NULL },
    { "Analysis/Distances & Paths", "ana_dist_ecc", "Eccentricity", NULL, NULL, NULL },

    // Shortest Paths (level 3)
    { "Analysis/Distances & Paths/Shortest Paths", "ana_dist_sp_dij", "Dijkstra", NULL, NULL, NULL },
    { "Analysis/Distances & Paths/Shortest Paths", "ana_dist_sp_bf", "Bellman-Ford", NULL, NULL, NULL },
    { "Analysis/Distances & Paths/Shortest Paths", "ana_dist_sp_astar", "A*", NULL, NULL, NULL },
    { "Analysis/Distances & Paths/Shortest Paths", "ana_dist_sp_joh", "Johnson", NULL, NULL, NULL },

    // Flows & Cuts (level 2)
    { "Analysis/Flows & Cuts", "ana_flow_max", "Maximum Flow", NULL, NULL, NULL },
    { "Analysis/Flows & Cuts", "ana_flow_min", "Minimum Cut", NULL, NULL, NULL },
    { "Analysis/Flows & Cuts", "ana_flow_gom", "Gomory-Hu Tree", NULL, NULL, NULL },
    { "Analysis/Flows & Cuts", "ana_flow_adh", "Adhesion / Cohesion", NULL, NULL, NULL },

    // Graph Embedding (level 2)
    { "Analysis/Graph Embedding", "ana_emb_adj", "Adjacency Spectral Embedding", NULL, NULL, NULL },
    { "Analysis/Graph Embedding", "ana_emb_lap", "Laplacian Spectral Embedding", NULL, NULL, NULL },

    // =========================================================================
    // Topology & Motifs menu (level 1-3)
    // =========================================================================
    { "Topology & Motifs", "top_comp", "Components & Connectivity", NULL, NULL, NULL },
    { "Topology & Motifs", "top_cliq", "Cliques & Independent Sets", NULL, NULL, NULL },
    { "Topology & Motifs", "top_cyc", "Cycles & Paths", NULL, NULL, NULL },
    { "Topology & Motifs", "top_mot", "Motifs & Censuses", NULL, NULL, NULL },
    { "Topology & Motifs", "top_iso", "Graph Isomorphism", NULL, NULL, NULL },
    { "Topology & Motifs", "top_col", "Graph Coloring", NULL, NULL, NULL },

    // Components & Connectivity (level 2)
    { "Topology & Motifs/Components & Connectivity", "top_comp_conn", "Connected Components", NULL, NULL, NULL },
    { "Topology & Motifs/Components & Connectivity", "top_comp_bicon", "Biconnected Components", NULL, NULL, NULL },
    { "Topology & Motifs/Components & Connectivity", "top_comp_art", "Articulation Points", NULL, NULL, NULL },
    { "Topology & Motifs/Components & Connectivity", "top_comp_sep", "Vertex Separators", NULL, NULL, NULL },

    // Cliques & Independent Sets (level 2)
    { "Topology & Motifs/Cliques & Independent Sets", "top_cliq_maxl", "Maximal Cliques", NULL, NULL, NULL },
    { "Topology & Motifs/Cliques & Independent Sets", "top_cliq_maxm", "Maximum Cliques", NULL, NULL, NULL },
    { "Topology & Motifs/Cliques & Independent Sets", "top_cliq_ind", "Largest Independent Vertex Sets", NULL, NULL, NULL },

    // Cycles & Paths (level 2)
    { "Topology & Motifs/Cycles & Paths", "top_cyc_eul", "Eulerian Paths", NULL, NULL, NULL },
    { "Topology & Motifs/Cycles & Paths", "top_cyc_fund", "Fundamental Cycles", NULL, NULL, NULL },

    // Motifs & Censuses (level 2)
    { "Topology & Motifs/Motifs & Censuses", "top_mot_tri", "Triad Census", NULL, NULL, NULL },
    { "Topology & Motifs/Motifs & Censuses", "top_mot_dya", "Dyad Census", NULL, NULL, NULL },
    { "Topology & Motifs/Motifs & Censuses", "top_mot_net", "Network Motifs", NULL, NULL, NULL },

    // Graph Isomorphism (level 2)
    { "Topology & Motifs/Graph Isomorphism", "top_iso_vf2", "VF2", NULL, NULL, NULL },
    { "Topology & Motifs/Graph Isomorphism", "top_iso_bliss", "Bliss", NULL, NULL, NULL },

    // Graph Coloring (level 2)
    { "Topology & Motifs/Graph Coloring", "top_col_greedy", "Greedy coloring", NULL, NULL, NULL },

    // =========================================================================
    // Communities menu (level 1-3)
    // =========================================================================
    { "Communities", "com_det", "Detection", NULL, NULL, NULL },
    { "Communities", "com_comp", "Compare", NULL, NULL, NULL },
    { "Communities", "com_glet", "Graphlets", NULL, NULL, NULL },
    { "Communities", "com_hrg", "Hierarchical Random Graphs", NULL, NULL, NULL },

    // Detection (level 2)
    { "Communities/Detection", "com_det_louv", "Louvain Method (Multilevel)", NULL, NULL, NULL },
    { "Communities/Detection", "com_det_walk", "Walktrap (Random walks)", NULL, NULL, NULL },
    { "Communities/Detection", "com_det_btw", "Edge Betweenness (Girvan-Newman)", NULL, NULL, NULL },
    { "Communities/Detection", "com_det_fast", "Fast Greedy", NULL, NULL, NULL },
    { "Communities/Detection", "com_det_info", "Infomap", NULL, NULL, NULL },
    { "Communities/Detection", "com_det_lab", "Label Propagation", NULL, NULL, NULL },
    { "Communities/Detection", "com_det_spin", "Spinglass", NULL, NULL, NULL },

    // Compare (level 2)
    { "Communities/Compare", "com_comp_sj", "Split-join distance", NULL, NULL, NULL },
    { "Communities/Compare", "com_comp_ari", "Adjusted Rand Index", NULL, NULL, NULL },

    // Graphlets (level 2)
    { "Communities/Graphlets", "com_glet_basis", "basis", NULL, NULL, NULL },
    { "Communities/Graphlets", "com_glet_proj", "projections", NULL, NULL, NULL },

    // Hierarchical Random Graphs (level 2)
    { "Communities/Hierarchical Random Graphs", "com_hrg_fit", "Fit", NULL, NULL, NULL },
    { "Communities/Hierarchical Random Graphs", "com_hrg_cons", "consensus trees", NULL, NULL, NULL },

    // =========================================================================
    // Processes & Traversal menu (level 1-3)
    // =========================================================================
    { "Processes & Traversal", "pro_search", "Search & Traversal", NULL, NULL, NULL },
    { "Processes & Traversal", "pro_proc", "Processes on Graphs", NULL, NULL, NULL },

    // Search & Traversal (level 2)
    { "Processes & Traversal/Search & Traversal", "pro_search_bfs", "Breadth-First Search", NULL, NULL, NULL },
    { "Processes & Traversal/Search & Traversal", "pro_search_dfs", "Depth-First Search", NULL, NULL, NULL },
    { "Processes & Traversal/Search & Traversal", "pro_search_topo", "Topological Sorting", NULL, NULL, NULL },

    // Processes on Graphs (level 2)
    { "Processes & Traversal/Processes on Graphs", "pro_proc_rw", "Simulate Random Walks", NULL, NULL, NULL },
    { "Processes & Traversal/Processes on Graphs", "pro_proc_rt", "Rank / transition behaviors", NULL, NULL, NULL },
};

const int g_command_registry_size = sizeof(g_command_registry) / sizeof(g_command_registry[0]);
