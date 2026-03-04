#include "command_registry.h"
#include "graph/wrappers_layout.h"

const CommandDef g_command_registry[] = {
    // =========================================================================
    // Main Menu Categories (top level)
    // =========================================================================
    { "Main", "main_file", "File", NULL, NULL, NULL },
    { "Main", "main_generate", "Generate", NULL, NULL, NULL },
    { "Main", "main_layout", "Layout", NULL, NULL, NULL },
    { "Main", "main_analysis", "Analysis", NULL, NULL, NULL },
    { "Main", "main_topology", "Topology & Motifs", NULL, NULL, NULL },
    { "Main", "main_communities", "Communities", NULL, NULL, NULL },
    { "Main", "main_processes", "Processes & Traversal", NULL, NULL, NULL },

    // =========================================================================
    // File Menu
    // =========================================================================
    { "File", "file_new", "New Graph", NULL, NULL, NULL },
    { "File", "file_open", "Open Graph", NULL, NULL, NULL },
    { "File", "file_save", "Save Graph", NULL, NULL, NULL },
    { "File", "app_exit", "Exit", NULL, NULL, NULL },

    // =========================================================================
    // Generate Menu - Sub-categories
    // =========================================================================
    { "Generate", "gen_deterministic", "Deterministic Graphs", NULL, NULL, NULL },
    { "Generate", "gen_stochastic", "Stochastic Graphs", NULL, NULL, NULL },
    { "Generate", "gen_bipartite", "Bipartite Graphs", NULL, NULL, NULL },
    { "Generate", "gen_spatial", "Spatial Graphs", NULL, NULL, NULL },

    // Generate - Deterministic Graphs
    { "Generate/Deterministic", "gen_det_ring", "Ring", NULL, NULL, NULL },
    { "Generate/Deterministic", "gen_det_star", "Star", NULL, NULL, NULL },
    { "Generate/Deterministic", "gen_det_tree", "Tree", NULL, NULL, NULL },
    { "Generate/Deterministic", "gen_det_lattice", "Lattice", NULL, NULL, NULL },
    { "Generate/Deterministic", "gen_det_full", "Full Graph (Clique)", NULL, NULL, NULL },
    { "Generate/Deterministic", "gen_det_circle", "Circle", NULL, NULL, NULL },
    { "Generate/Deterministic", "gen_det_notable", "Notable Graphs", NULL, NULL, NULL },

    // Generate - Stochastic Graphs
    { "Generate/Stochastic", "gen_sto_er", "Erdős-Rényi", NULL, NULL, NULL },
    { "Generate/Stochastic", "gen_sto_ba", "Barabási-Albert", NULL, NULL, NULL },
    { "Generate/Stochastic", "gen_sto_ws", "Watts-Strogatz", NULL, NULL, NULL },
    { "Generate/Stochastic", "gen_sto_ff", "Forest Fire", NULL, NULL, NULL },
    { "Generate/Stochastic", "gen_sto_tree", "Random Tree", NULL, NULL, NULL },
    { "Generate/Stochastic", "gen_sto_deg", "Degree Sequence", NULL, NULL, NULL },

    // Generate - Bipartite Graphs
    { "Generate/Bipartite", "gen_bip_rand", "Generate Random Bipartite", NULL, NULL, NULL },
    { "Generate/Bipartite", "gen_bip_proj", "Create Bipartite Projections", NULL, NULL, NULL },

    // Generate - Spatial Graphs
    { "Generate/Spatial", "gen_spa_geo", "Geometric Random Graphs", NULL, NULL, NULL },
    { "Generate/Spatial", "gen_spa_gab", "Gabriel Graphs", NULL, NULL, NULL },

    // =========================================================================
    // Layout Menu - Sub-categories
    // =========================================================================
    { "Layout", "layout_force", "Force-Directed Layouts", NULL, NULL, NULL },
    { "Layout", "layout_tree", "Tree & Hierarchical", NULL, NULL, NULL },
    { "Layout", "layout_geo", "Geometric Layouts", NULL, NULL, NULL },
    { "Layout", "layout_bip", "Bipartite Layouts", NULL, NULL, NULL },
    { "Layout", "layout_dim", "Dimension Reduction", NULL, NULL, NULL },

    // Layout - Force-Directed
    { "Layout/Force-Directed", "lay_force_fr", "Fruchterman-Reingold", compute_lay_force_fr, apply_layout_matrix, free_layout_matrix },
    { "Layout/Force-Directed", "lay_force_kk", "Kamada-Kawai", compute_lay_force_kk, apply_layout_matrix, free_layout_matrix },
    { "Layout/Force-Directed", "lay_force_drl", "Distributed Recursive", compute_lay_force_drl, apply_layout_matrix, free_layout_matrix },
    { "Layout/Force-Directed", "lay_force_dh", "Davidson-Harel", compute_lay_force_dh, apply_layout_matrix, free_layout_matrix },

    // Layout - Tree & Hierarchical
    { "Layout/Tree", "lay_tree_rt", "Reingold-Tilford", compute_lay_tree_rt, apply_layout_matrix, free_layout_matrix },
    { "Layout/Tree", "lay_tree_sug", "Sugiyama", compute_lay_tree_sug, apply_layout_matrix, free_layout_matrix },

    // Layout - Geometric
    { "Layout/Geometric", "lay_geo_circle", "Circle", compute_lay_geo_circle, apply_layout_matrix, free_layout_matrix },
    { "Layout/Geometric", "lay_geo_star", "Star", compute_lay_geo_star, apply_layout_matrix, free_layout_matrix },
    { "Layout/Geometric", "lay_geo_grid", "Grid", compute_lay_geo_grid, apply_layout_matrix, free_layout_matrix },
    { "Layout/Geometric", "lay_geo_sphere", "Sphere", compute_lay_geo_sphere, apply_layout_matrix, free_layout_matrix },
    { "Layout/Geometric", "lay_geo_rand", "Random", compute_lay_geo_rand, apply_layout_matrix, free_layout_matrix },

    // Layout - Bipartite
    { "Layout/Bipartite", "lay_bip_mds", "MDS", compute_lay_bip_mds, apply_layout_matrix, free_layout_matrix },
    { "Layout/Bipartite", "lay_bip_sug", "Sugiyama (Bipartite)", compute_lay_bip_sug, apply_layout_matrix, free_layout_matrix },

    // Layout - Dimension Reduction
    { "Layout/Dimension Reduction", "lay_umap", "UMAP 3D", compute_lay_umap, apply_layout_matrix, free_layout_matrix },

    // =========================================================================
    // Analysis Menu - Sub-categories
    // =========================================================================
    { "Analysis", "analysis_cent", "Centrality & Roles", NULL, NULL, NULL },
    { "Analysis", "analysis_glob", "Global Network Properties", NULL, NULL, NULL },
    { "Analysis", "analysis_dist", "Distances & Paths", NULL, NULL, NULL },
    { "Analysis", "analysis_flow", "Flows & Cuts", NULL, NULL, NULL },
    { "Analysis", "analysis_emb", "Graph Embedding", NULL, NULL, NULL },

    // Analysis - Centrality & Roles
    { "Analysis/Centrality", "ana_cent_deg", "Degree", NULL, NULL, NULL },
    { "Analysis/Centrality", "ana_cent_clo", "Closeness", NULL, NULL, NULL },
    { "Analysis/Centrality", "ana_cent_btw", "Betweenness", NULL, NULL, NULL },
    { "Analysis/Centrality", "ana_cent_eig", "Eigenvector Centrality", NULL, NULL, NULL },
    { "Analysis/Centrality", "ana_cent_pager", "PageRank", NULL, NULL, NULL },
    { "Analysis/Centrality", "ana_cent_hits", "Hubs & Authorities", NULL, NULL, NULL },

    // Analysis - Global Network Properties
    { "Analysis/Global", "ana_glob_diam", "Diameter", NULL, NULL, NULL },
    { "Analysis/Global", "ana_glob_rad", "Radius", NULL, NULL, NULL },
    { "Analysis/Global", "ana_glob_apl", "Average Path Length", NULL, NULL, NULL },
    { "Analysis/Global", "ana_glob_assort", "Assortativity", NULL, NULL, NULL },
    { "Analysis/Global", "ana_glob_mod", "Modularity", NULL, NULL, NULL },
    { "Analysis/Global", "ana_glob_dens", "Density", NULL, NULL, NULL },
    { "Analysis/Global", "ana_glob_trans", "Transitivity", NULL, NULL, NULL },

    // Analysis - Distances & Paths (nested under Distances & Paths)
    { "Analysis/Distances", "ana_dist_sp", "Shortest Paths", NULL, NULL, NULL },
    { "Analysis/Distances", "ana_dist_ecc", "Eccentricity", NULL, NULL, NULL },

    // Analysis - Shortest Paths sub-menu
    { "Analysis/Distances/Shortest Paths", "ana_dist_sp_dij", "Dijkstra", NULL, NULL, NULL },
    { "Analysis/Distances/Shortest Paths", "ana_dist_sp_bf", "Bellman-Ford", NULL, NULL, NULL },
    { "Analysis/Distances/Shortest Paths", "ana_dist_sp_astar", "A*", NULL, NULL, NULL },
    { "Analysis/Distances/Shortest Paths", "ana_dist_sp_joh", "Johnson", NULL, NULL, NULL },

    // Analysis - Flows & Cuts
    { "Analysis/Flows", "ana_flow_max", "Maximum Flow", NULL, NULL, NULL },
    { "Analysis/Flows", "ana_flow_min", "Minimum Cut", NULL, NULL, NULL },
    { "Analysis/Flows", "ana_flow_gom", "Gomory-Hu Tree", NULL, NULL, NULL },
    { "Analysis/Flows", "ana_flow_adh", "Adhesion / Cohesion", NULL, NULL, NULL },

    // Analysis - Graph Embedding
    { "Analysis/Embedding", "ana_emb_adj", "Adjacency Spectral", NULL, NULL, NULL },
    { "Analysis/Embedding", "ana_emb_lap", "Laplacian Spectral", NULL, NULL, NULL },

    // =========================================================================
    // Topology Menu - Sub-categories
    // =========================================================================
    { "Topology", "top_comp", "Components & Connectivity", NULL, NULL, NULL },
    { "Topology", "top_cliq", "Cliques & Independent Sets", NULL, NULL, NULL },
    { "Topology", "top_cyc", "Cycles & Paths", NULL, NULL, NULL },
    { "Topology", "top_mot", "Motifs & Censuses", NULL, NULL, NULL },
    { "Topology", "top_iso", "Graph Isomorphism", NULL, NULL, NULL },
    { "Topology", "top_col", "Graph Coloring", NULL, NULL, NULL },

    // Topology - Components & Connectivity
    { "Topology/Components", "top_comp_conn", "Connected Components", NULL, NULL, NULL },
    { "Topology/Components", "top_comp_bicon", "Biconnected Components", NULL, NULL, NULL },
    { "Topology/Components", "top_comp_art", "Articulation Points", NULL, NULL, NULL },
    { "Topology/Components", "top_comp_sep", "Vertex Separators", NULL, NULL, NULL },

    // Topology - Cliques & Independent Sets
    { "Topology/Cliques", "top_cliq_maxl", "Maximal Cliques", NULL, NULL, NULL },
    { "Topology/Cliques", "top_cliq_maxm", "Maximum Cliques", NULL, NULL, NULL },
    { "Topology/Cliques", "top_cliq_ind", "Independent Sets", NULL, NULL, NULL },

    // Topology - Cycles & Paths
    { "Topology/Cycles", "top_cyc_eul", "Eulerian Paths", NULL, NULL, NULL },
    { "Topology/Cycles", "top_cyc_fund", "Fundamental Cycles", NULL, NULL, NULL },

    // Topology - Motifs & Censuses
    { "Topology/Motifs", "top_mot_tri", "Triad Census", NULL, NULL, NULL },
    { "Topology/Motifs", "top_mot_dya", "Dyad Census", NULL, NULL, NULL },
    { "Topology/Motifs", "top_mot_net", "Network Motifs", NULL, NULL, NULL },

    // Topology - Graph Isomorphism
    { "Topology/Isomorphism", "top_iso_vf2", "VF2", NULL, NULL, NULL },
    { "Topology/Isomorphism", "top_iso_bliss", "Bliss", NULL, NULL, NULL },

    // Topology - Graph Coloring
    { "Topology/Coloring", "top_col_greedy", "Greedy Coloring", NULL, NULL, NULL },

    // =========================================================================
    // Communities Menu - Sub-categories
    // =========================================================================
    { "Communities", "com_det", "Detect Community Structure", NULL, NULL, NULL },
    { "Communities", "com_comp", "Compare Communities", NULL, NULL, NULL },
    { "Communities", "com_glet", "Graphlets", NULL, NULL, NULL },
    { "Communities", "com_hrg", "Hierarchical Random Graphs", NULL, NULL, NULL },

    // Communities - Detect
    { "Communities/Detect", "com_det_louv", "Louvain Method", NULL, NULL, NULL },
    { "Communities/Detect", "com_det_walk", "Walktrap", NULL, NULL, NULL },
    { "Communities/Detect", "com_det_btw", "Edge Betweenness", NULL, NULL, NULL },
    { "Communities/Detect", "com_det_fast", "Fast Greedy", NULL, NULL, NULL },
    { "Communities/Detect", "com_det_info", "Infomap", NULL, NULL, NULL },
    { "Communities/Detect", "com_det_lab", "Label Propagation", NULL, NULL, NULL },
    { "Communities/Detect", "com_det_spin", "Spinglass", NULL, NULL, NULL },

    // Communities - Compare
    { "Communities/Compare", "com_comp_sj", "Split-join Distance", NULL, NULL, NULL },
    { "Communities/Compare", "com_comp_ari", "Adjusted Rand Index", NULL, NULL, NULL },

    // Communities - Graphlets
    { "Communities/Graphlets", "com_glet_basis", "Basis", NULL, NULL, NULL },
    { "Communities/Graphlets", "com_glet_proj", "Projections", NULL, NULL, NULL },

    // Communities - Hierarchical Random Graphs
    { "Communities/HRG", "com_hrg_fit", "Fit", NULL, NULL, NULL },
    { "Communities/HRG", "com_hrg_cons", "Consensus Trees", NULL, NULL, NULL },

    // =========================================================================
    // Processes Menu - Sub-categories
    // =========================================================================
    { "Processes", "pro_search", "Search & Traversal", NULL, NULL, NULL },
    { "Processes", "pro_proc", "Processes on Graphs", NULL, NULL, NULL },

    // Processes - Search & Traversal
    { "Processes/Search", "pro_search_bfs", "Breadth-First Search", NULL, NULL, NULL },
    { "Processes/Search", "pro_search_dfs", "Depth-First Search", NULL, NULL, NULL },
    { "Processes/Search", "pro_search_topo", "Topological Sorting", NULL, NULL, NULL },

    // Processes - Processes on Graphs
    { "Processes/Simulation", "pro_proc_rw", "Simulate Random Walks", NULL, NULL, NULL },
    { "Processes/Simulation", "pro_proc_rt", "Rank / Transition", NULL, NULL, NULL },
};

const int g_command_registry_size = sizeof(g_command_registry) / sizeof(g_command_registry[0]);
