#include "graph/wrappers.h"
#include "graph/graph_core.h"
#include "app_state.h"
#include "interaction/state.h"
#include "vulkan/renderer.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

// Helper function to apply a computed layout matrix to the graph and update visualization
static void apply_layout_to_graph(ExecutionContext* ctx, igraph_matrix_t* layout) {
    if (!ctx || !ctx->app_state || !ctx->current_graph) return;
    
    GraphData* data = &ctx->app_state->current_graph;
    Renderer* renderer = &ctx->app_state->renderer;
    
    if (!data->graph_initialized) {
        fprintf(stderr, "[Wrapper] Error: Graph not initialized\n");
        return;
    }
    
    // Check if layout dimensions match
    if (layout->nrow != data->node_count || layout->ncol < 2) {
        fprintf(stderr, "[Wrapper] Error: Layout dimensions don't match node count\n");
        return;
    }
    
    // Destroy old layout and replace with new one
    igraph_matrix_destroy(&data->current_layout);
    igraph_matrix_init_copy(&data->current_layout, layout);
    
    // Sync node positions from the layout matrix
    if (data->nodes) {
        for (igraph_integer_t i = 0; i < data->node_count; i++) {
            data->nodes[i].position[0] = (float)MATRIX(data->current_layout, i, 0);
            data->nodes[i].position[1] = (float)MATRIX(data->current_layout, i, 1);
            data->nodes[i].position[2] = (igraph_matrix_ncol(&data->current_layout) > 2)
                ? (float)MATRIX(data->current_layout, i, 2)
                : 0.0f;
        }
    }
    
    // Trigger renderer update to display new layout
    renderer_update_graph(renderer, data);
    
    printf("[Wrapper] Layout updated and renderer refreshed\n");
}

// ============================================================================
// Force-Directed Layout Wrappers
// ============================================================================

void wrapper_lay_force_fr(ExecutionContext* ctx) {
    if (!ctx || !ctx->current_graph) {
        fprintf(stderr, "[Wrapper] Error: Invalid context for Fruchterman-Reingold\n");
        return;
    }
    
    igraph_integer_t vcount = igraph_vcount(ctx->current_graph);
    
    igraph_matrix_t layout;
    if (igraph_matrix_init(&layout, vcount, 3) != IGRAPH_SUCCESS) {
        fprintf(stderr, "[Wrapper] Error: Failed to initialize layout matrix\n");
        return;
    }
    
    igraph_error_t result = igraph_layout_fruchterman_reingold_3d(
        ctx->current_graph, 
        &layout, 
        1,  /* use_seed */
        300,  /* iterations */
        (igraph_real_t)vcount,  /* start_temp */
        NULL,  /* weights */
        NULL,  /* minx */
        NULL,  /* maxx */
        NULL,  /* miny */
        NULL,  /* maxy */
        NULL,  /* minz */
        NULL   /* maxz */
    );
    
    if (result != IGRAPH_SUCCESS) {
        fprintf(stderr, "[Wrapper] Error: igraph_layout_fruchterman_reingold_3d failed\n");
        igraph_matrix_destroy(&layout);
        return;
    }
    
    apply_layout_to_graph(ctx, &layout);
    igraph_matrix_destroy(&layout);
}

void wrapper_lay_force_kk(ExecutionContext* ctx) {
    if (!ctx || !ctx->current_graph) {
        fprintf(stderr, "[Wrapper] Error: Invalid context for Kamada-Kawai\n");
        return;
    }
    
    igraph_integer_t vcount = igraph_vcount(ctx->current_graph);
    
    igraph_matrix_t layout;
    if (igraph_matrix_init(&layout, vcount, 3) != IGRAPH_SUCCESS) {
        fprintf(stderr, "[Wrapper] Error: Failed to initialize layout matrix\n");
        return;
    }
    
    igraph_error_t result = igraph_layout_kamada_kawai_3d(
        ctx->current_graph, 
        &layout, 
        1,  /* use_seed */
        vcount * 10,  /* maxiter */
        0.0,  /* epsilon */
        (igraph_real_t)vcount,  /* kkconst */
        NULL,  /* weights */
        NULL,  /* minx */
        NULL,  /* maxx */
        NULL,  /* miny */
        NULL,  /* maxy */
        NULL,  /* minz */
        NULL   /* maxz */
    );
    
    if (result != IGRAPH_SUCCESS) {
        fprintf(stderr, "[Wrapper] Error: igraph_layout_kamada_kawai_3d failed\n");
        igraph_matrix_destroy(&layout);
        return;
    }
    
    apply_layout_to_graph(ctx, &layout);
    igraph_matrix_destroy(&layout);
}

void wrapper_lay_force_drl(ExecutionContext* ctx) {
    if (!ctx || !ctx->current_graph) {
        fprintf(stderr, "[Wrapper] Error: Invalid context for DRL\n");
        return;
    }
    
    igraph_integer_t vcount = igraph_vcount(ctx->current_graph);
    
    igraph_matrix_t layout;
    if (igraph_matrix_init(&layout, vcount, 3) != IGRAPH_SUCCESS) {
        fprintf(stderr, "[Wrapper] Error: Failed to initialize layout matrix\n");
        return;
    }
    
    igraph_layout_drl_options_t options;
    igraph_layout_drl_options_init(&options, IGRAPH_LAYOUT_DRL_DEFAULT);
    
    igraph_error_t result = igraph_layout_drl_3d(
        ctx->current_graph, 
        &layout, 
        0,  /* use_seed */
        &options,  /* options */
        NULL  /* weights */
    );
    
    if (result != IGRAPH_SUCCESS) {
        fprintf(stderr, "[Wrapper] Error: igraph_layout_drl_3d failed\n");
        igraph_matrix_destroy(&layout);
        return;
    }
    
    apply_layout_to_graph(ctx, &layout);
    igraph_matrix_destroy(&layout);
}

void wrapper_lay_force_gopt(ExecutionContext* ctx) {
    fprintf(stderr, "[Wrapper] Error: GraphOpt layout is not available in igraph 1.0.1\n");
}

// ============================================================================
// Tree & Hierarchical Layout Wrappers
// ============================================================================

void wrapper_lay_tree_rt(ExecutionContext* ctx) {
    if (!ctx || !ctx->current_graph) {
        fprintf(stderr, "[Wrapper] Error: Invalid context for Reingold-Tilford\n");
        return;
    }
    
    igraph_integer_t vcount = igraph_vcount(ctx->current_graph);
    
    igraph_matrix_t layout;
    if (igraph_matrix_init(&layout, vcount, 3) != IGRAPH_SUCCESS) {
        fprintf(stderr, "[Wrapper] Error: Failed to initialize layout matrix\n");
        return;
    }
    
    igraph_vector_int_t roots;
    igraph_vector_int_init(&roots, vcount > 0 ? 1 : 0);
    if (vcount > 0) {
        igraph_vector_int_set(&roots, 0, 0);
    }
    
    igraph_error_t result = igraph_layout_reingold_tilford(
        ctx->current_graph, 
        &layout, 
        IGRAPH_ALL,  /* mode */
        vcount > 0 ? &roots : NULL,  /* roots */
        NULL   /* rootlevel */
    );
    
    igraph_vector_int_destroy(&roots);
    
    if (result != IGRAPH_SUCCESS) {
        fprintf(stderr, "[Wrapper] Error: igraph_layout_reingold_tilford failed\n");
        igraph_matrix_destroy(&layout);
        return;
    }
    
    // Convert 2D to 3D
    for (igraph_integer_t i = 0; i < vcount; i++) {
        igraph_matrix_set(&layout, i, 2, 0.0);
    }
    
    apply_layout_to_graph(ctx, &layout);
    igraph_matrix_destroy(&layout);
}

void wrapper_lay_tree_sug(ExecutionContext* ctx) {
    if (!ctx || !ctx->current_graph) {
        fprintf(stderr, "[Wrapper] Error: Invalid context for Sugiyama\n");
        return;
    }
    
    igraph_integer_t vcount = igraph_vcount(ctx->current_graph);
    
    igraph_matrix_t layout;
    if (igraph_matrix_init(&layout, vcount, 3) != IGRAPH_SUCCESS) {
        fprintf(stderr, "[Wrapper] Error: Failed to initialize layout matrix\n");
        return;
    }
    
    igraph_matrix_list_t routing;
    if (igraph_matrix_list_init(&routing, 0) != IGRAPH_SUCCESS) {
        fprintf(stderr, "[Wrapper] Error: Failed to initialize routing matrix list\n");
        igraph_matrix_destroy(&layout);
        return;
    }
    
    igraph_vector_int_t layers;
    igraph_vector_int_init(&layers, vcount);
    
    igraph_error_t result = igraph_layout_sugiyama(
        ctx->current_graph, 
        &layout, 
        &routing,  /* routing */
        &layers,   /* layers */
        1.0,       /* hgap */
        1.0,       /* vgap */
        100,       /* maxiter */
        NULL       /* weights */
    );
    
    igraph_matrix_list_destroy(&routing);
    igraph_vector_int_destroy(&layers);
    
    if (result != IGRAPH_SUCCESS) {
        fprintf(stderr, "[Wrapper] Error: igraph_layout_sugiyama failed\n");
        igraph_matrix_destroy(&layout);
        return;
    }
    
    // Convert 2D to 3D
    for (igraph_integer_t i = 0; i < vcount; i++) {
        igraph_matrix_set(&layout, i, 2, 0.0);
    }
    
    apply_layout_to_graph(ctx, &layout);
    igraph_matrix_destroy(&layout);
}

// ============================================================================
// Geometric Layout Wrappers
// ============================================================================

void wrapper_lay_geo_circle(ExecutionContext* ctx) {
    if (!ctx || !ctx->current_graph) {
        fprintf(stderr, "[Wrapper] Error: Invalid context for Circle\n");
        return;
    }
    
    igraph_integer_t vcount = igraph_vcount(ctx->current_graph);
    
    igraph_matrix_t layout;
    if (igraph_matrix_init(&layout, vcount, 3) != IGRAPH_SUCCESS) {
        fprintf(stderr, "[Wrapper] Error: Failed to initialize layout matrix\n");
        return;
    }
    
    igraph_vs_t order;
    igraph_vs_all(&order);
    
    igraph_error_t result = igraph_layout_circle(
        ctx->current_graph, 
        &layout, 
        order   /* order - all vertices */
    );
    
    if (result != IGRAPH_SUCCESS) {
        fprintf(stderr, "[Wrapper] Error: igraph_layout_circle failed\n");
        igraph_matrix_destroy(&layout);
        return;
    }
    
    // Convert 2D to 3D
    for (igraph_integer_t i = 0; i < vcount; i++) {
        igraph_matrix_set(&layout, i, 2, 0.0);
    }
    
    apply_layout_to_graph(ctx, &layout);
    igraph_matrix_destroy(&layout);
}

void wrapper_lay_geo_star(ExecutionContext* ctx) {
    if (!ctx || !ctx->current_graph) {
        fprintf(stderr, "[Wrapper] Error: Invalid context for Star\n");
        return;
    }
    
    igraph_integer_t vcount = igraph_vcount(ctx->current_graph);
    
    igraph_matrix_t layout;
    if (igraph_matrix_init(&layout, vcount, 3) != IGRAPH_SUCCESS) {
        fprintf(stderr, "[Wrapper] Error: Failed to initialize layout matrix\n");
        return;
    }
    
    igraph_vector_int_t order;
    igraph_vector_int_init(&order, 0);
    
    igraph_error_t result = igraph_layout_star(
        ctx->current_graph, 
        &layout, 
        0,  /* center vertex */
        &order   /* order (empty for default) */
    );
    
    igraph_vector_int_destroy(&order);
    
    if (result != IGRAPH_SUCCESS) {
        fprintf(stderr, "[Wrapper] Error: igraph_layout_star failed\n");
        igraph_matrix_destroy(&layout);
        return;
    }
    
    // Convert 2D to 3D
    for (igraph_integer_t i = 0; i < vcount; i++) {
        igraph_matrix_set(&layout, i, 2, 0.0);
    }
    
    apply_layout_to_graph(ctx, &layout);
    igraph_matrix_destroy(&layout);
}

void wrapper_lay_geo_grid(ExecutionContext* ctx) {
    if (!ctx || !ctx->current_graph) {
        fprintf(stderr, "[Wrapper] Error: Invalid context for Grid\n");
        return;
    }
    
    igraph_integer_t vcount = igraph_vcount(ctx->current_graph);
    
    igraph_matrix_t layout;
    if (igraph_matrix_init(&layout, vcount, 3) != IGRAPH_SUCCESS) {
        fprintf(stderr, "[Wrapper] Error: Failed to initialize layout matrix\n");
        return;
    }
    
    int side = (int)ceil(pow((double)vcount, 1.0 / 3.0));
    
    igraph_error_t result = igraph_layout_grid_3d(
        ctx->current_graph, 
        &layout, 
        (igraph_integer_t)side,  /* width */
        (igraph_integer_t)side    /* height */
    );
    
    if (result != IGRAPH_SUCCESS) {
        fprintf(stderr, "[Wrapper] Error: igraph_layout_grid_3d failed\n");
        igraph_matrix_destroy(&layout);
        return;
    }
    
    apply_layout_to_graph(ctx, &layout);
    igraph_matrix_destroy(&layout);
}

void wrapper_lay_geo_sphere(ExecutionContext* ctx) {
    if (!ctx || !ctx->current_graph) {
        fprintf(stderr, "[Wrapper] Error: Invalid context for Sphere\n");
        return;
    }
    
    igraph_integer_t vcount = igraph_vcount(ctx->current_graph);
    
    igraph_matrix_t layout;
    if (igraph_matrix_init(&layout, vcount, 3) != IGRAPH_SUCCESS) {
        fprintf(stderr, "[Wrapper] Error: Failed to initialize layout matrix\n");
        return;
    }
    
    igraph_error_t result = igraph_layout_sphere(
        ctx->current_graph, 
        &layout
    );
    
    if (result != IGRAPH_SUCCESS) {
        fprintf(stderr, "[Wrapper] Error: igraph_layout_sphere failed\n");
        igraph_matrix_destroy(&layout);
        return;
    }
    
    apply_layout_to_graph(ctx, &layout);
    igraph_matrix_destroy(&layout);
}

void wrapper_lay_geo_rand(ExecutionContext* ctx) {
    if (!ctx || !ctx->current_graph) {
        fprintf(stderr, "[Wrapper] Error: Invalid context for Random\n");
        return;
    }
    
    igraph_integer_t vcount = igraph_vcount(ctx->current_graph);
    
    igraph_matrix_t layout;
    if (igraph_matrix_init(&layout, vcount, 3) != IGRAPH_SUCCESS) {
        fprintf(stderr, "[Wrapper] Error: Failed to initialize layout matrix\n");
        return;
    }
    
    igraph_error_t result = igraph_layout_random_3d(
        ctx->current_graph, 
        &layout
    );
    
    if (result != IGRAPH_SUCCESS) {
        fprintf(stderr, "[Wrapper] Error: igraph_layout_random_3d failed\n");
        igraph_matrix_destroy(&layout);
        return;
    }
    
    apply_layout_to_graph(ctx, &layout);
    igraph_matrix_destroy(&layout);
}

// ============================================================================
// Bipartite Layout Wrappers
// ============================================================================

void wrapper_lay_bip_mds(ExecutionContext* ctx) {
    if (!ctx || !ctx->current_graph) {
        fprintf(stderr, "[Wrapper] Error: Invalid context for MDS\n");
        return;
    }
    
    igraph_integer_t vcount = igraph_vcount(ctx->current_graph);
    
    igraph_matrix_t dist_matrix;
    if (igraph_matrix_init(&dist_matrix, vcount, vcount) != IGRAPH_SUCCESS) {
        fprintf(stderr, "[Wrapper] Error: Failed to initialize distance matrix\n");
        return;
    }
    
    igraph_vs_t all_vs;
    igraph_vs_all(&all_vs);
    
    igraph_error_t result = igraph_distances_dijkstra(
        ctx->current_graph, 
        &dist_matrix, 
        all_vs,   /* from */
        all_vs,   /* to */
        NULL,     /* weights */
        IGRAPH_UNDIRECTED  /* mode */
    );
    
    if (result != IGRAPH_SUCCESS) {
        fprintf(stderr, "[Wrapper] Error: igraph_distances_dijkstra failed\n");
        igraph_matrix_destroy(&dist_matrix);
        return;
    }
    
    igraph_matrix_t layout;
    if (igraph_matrix_init(&layout, vcount, 3) != IGRAPH_SUCCESS) {
        fprintf(stderr, "[Wrapper] Error: Failed to initialize layout matrix\n");
        igraph_matrix_destroy(&dist_matrix);
        return;
    }
    
    result = igraph_layout_mds(
        ctx->current_graph, 
        &layout, 
        &dist_matrix,  /* distance matrix */
        3              /* dimension */
    );
    
    igraph_matrix_destroy(&dist_matrix);
    
    if (result != IGRAPH_SUCCESS) {
        fprintf(stderr, "[Wrapper] Error: igraph_layout_mds failed\n");
        igraph_matrix_destroy(&layout);
        return;
    }
    
    apply_layout_to_graph(ctx, &layout);
    igraph_matrix_destroy(&layout);
}

void wrapper_lay_bip_sug(ExecutionContext* ctx) {
    if (!ctx || !ctx->current_graph) {
        fprintf(stderr, "[Wrapper] Error: Invalid context for Bipartite Sugiyama\n");
        return;
    }
    
    igraph_integer_t vcount = igraph_vcount(ctx->current_graph);
    
    igraph_vector_bool_t types;
    igraph_vector_bool_init(&types, vcount);
    
    for (igraph_integer_t i = 0; i < vcount; i++) {
        igraph_vector_bool_set(&types, i, (i % 2 == 0));
    }
    
    igraph_matrix_t layout;
    if (igraph_matrix_init(&layout, vcount, 3) != IGRAPH_SUCCESS) {
        fprintf(stderr, "[Wrapper] Error: Failed to initialize layout matrix\n");
        igraph_vector_bool_destroy(&types);
        return;
    }
    
    igraph_error_t result = igraph_layout_bipartite(
        ctx->current_graph, 
        &types,  /* types vector */
        &layout, /* result */
        1.0,     /* hgap */
        1.0,     /* vgap */
        100      /* maxiter */
    );
    
    igraph_vector_bool_destroy(&types);
    
    if (result != IGRAPH_SUCCESS) {
        fprintf(stderr, "[Wrapper] Error: igraph_layout_bipartite failed\n");
        igraph_matrix_destroy(&layout);
        return;
    }
    
    // Ensure 3D with Z=0 if needed
    if (layout.ncol == 2) {
        igraph_matrix_resize(&layout, vcount, 3);
        for (igraph_integer_t i = 0; i < vcount; i++) {
            igraph_matrix_set(&layout, i, 2, 0.0);
        }
    }
    
    apply_layout_to_graph(ctx, &layout);
    igraph_matrix_destroy(&layout);
}
