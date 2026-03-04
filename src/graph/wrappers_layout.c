#include "graph/wrappers_layout.h"
#include "app_state.h"
#include "interaction/state.h"
#include "vulkan/renderer.h"
#include <stdio.h>
#include <stdlib.h>
#include <igraph.h>
#include <igraph_progress.h>

// Pure worker function - no UI or state dependencies
void* compute_lay_force_fr(igraph_t* graph) {
    igraph_integer_t vcount = igraph_vcount(graph);
    igraph_matrix_t* result = malloc(sizeof(igraph_matrix_t));
    if (igraph_matrix_init(result, vcount, 3) != IGRAPH_SUCCESS) {
        free(result);
        return NULL;
    }
    
    igraph_error_t code = igraph_layout_fruchterman_reingold_3d(
        graph, result, 1, 300, (igraph_real_t)vcount, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        
    if (code != IGRAPH_SUCCESS) {
        igraph_matrix_destroy(result);
        free(result);
        return NULL;
    }
    return result;
}

void free_layout_matrix(void* result_data) {
    if (result_data) {
        igraph_matrix_destroy((igraph_matrix_t*)result_data);
        free(result_data);
    }
}

// Apply function - bridge to update graph state
void apply_layout_matrix(ExecutionContext* ctx, void* result_data) {
    if (!ctx || !ctx->app_state || !ctx->current_graph || !result_data) {
        fprintf(stderr, "[apply_layout_matrix] Error: Invalid parameters\n");
        return;
    }
    
    AppState* state = ctx->app_state;
    GraphData* data = &state->current_graph;
    Renderer* renderer = &state->renderer;
    igraph_matrix_t* layout = (igraph_matrix_t*)result_data;
    
    if (!data->graph_initialized) {
        fprintf(stderr, "[apply_layout_matrix] Error: Graph not initialized\n");
        return;
    }
    
    // Check if layout dimensions match
    if (layout->nrow != data->node_count || layout->ncol < 2) {
        fprintf(stderr, "[apply_layout_matrix] Error: Layout dimensions don't match node count\n");
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
    
    printf("[apply_layout_matrix] Layout applied and renderer refreshed\n");
}
