#include "command_registry.h"
#include "graph/wrappers_layout.h"

const CommandDef g_command_registry[] = {
    // Force-Directed layouts
    { "Layout/Force-Directed", "lay_force_fr", "Fruchterman-Reingold", compute_lay_force_fr, apply_layout_matrix, free_layout_matrix },
    { "Layout/Force-Directed", "lay_force_kk", "Kamada-Kawai", compute_lay_force_kk, apply_layout_matrix, free_layout_matrix },
    { "Layout/Force-Directed", "lay_force_drl", "Distributed Recursive", compute_lay_force_drl, apply_layout_matrix, free_layout_matrix },
    { "Layout/Force-Directed", "lay_force_dh", "Davidson-Harel", compute_lay_force_dh, apply_layout_matrix, free_layout_matrix },
    
    // Tree layouts
    { "Layout/Tree", "lay_tree_rt", "Reingold-Tilford", compute_lay_tree_rt, apply_layout_matrix, free_layout_matrix },
    { "Layout/Tree", "lay_tree_sug", "Sugiyama", compute_lay_tree_sug, apply_layout_matrix, free_layout_matrix },
    
    // Geometric layouts
    { "Layout/Geometric", "lay_geo_circle", "Circle", compute_lay_geo_circle, apply_layout_matrix, free_layout_matrix },
    { "Layout/Geometric", "lay_geo_star", "Star", compute_lay_geo_star, apply_layout_matrix, free_layout_matrix },
    { "Layout/Geometric", "lay_geo_grid", "Grid", compute_lay_geo_grid, apply_layout_matrix, free_layout_matrix },
    { "Layout/Geometric", "lay_geo_sphere", "Sphere", compute_lay_geo_sphere, apply_layout_matrix, free_layout_matrix },
    { "Layout/Geometric", "lay_geo_rand", "Random", compute_lay_geo_rand, apply_layout_matrix, free_layout_matrix },
    
    // Bipartite layouts
    { "Layout/Bipartite", "lay_bip_mds", "MDS", compute_lay_bip_mds, apply_layout_matrix, free_layout_matrix },
    { "Layout/Bipartite", "lay_bip_sug", "Sugiyama (Bipartite)", compute_lay_bip_sug, apply_layout_matrix, free_layout_matrix },
    
    // Dimensionality Reduction
    { "Layout/Dimensionality Reduction", "lay_umap", "UMAP 3D", compute_lay_umap, apply_layout_matrix, free_layout_matrix },
};

const int g_command_registry_size = sizeof(g_command_registry) / sizeof(g_command_registry[0]);
