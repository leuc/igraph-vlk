#include "graph/wrappers.h"
#include <stdio.h>

void wrapper_shortest_path(ExecutionContext* ctx) {
    if (ctx->num_params < 2) return;
    
    int source = ctx->params[0].value.selection_id;
    int target = ctx->params[1].value.selection_id;
    
    if (source == -1 || target == -1) {
        printf("[Wrapper] Error: Shortest Path requires 2 selected nodes (Source: %d, Target: %d)\n", source, target);
        return;
    }
    
    printf("[Wrapper] Calculating Shortest Path from %d to %d using igraph_get_shortest_paths\n", source, target);
    
    // TODO: Actually call igraph and update visuals (highlight edges)
    // This produces visual output - we'll highlight the path
    // For now, we'll set a flag that tells the FSM to show results
    // (In a full implementation, we'd store the path data in ctx->user_data or similar)
}

void wrapper_pagerank(ExecutionContext* ctx) {
    printf("[Wrapper] Calculating PageRank using igraph_pagerank\n");
    // igraph_pagerank(ctx->current_graph, IGRAPH_PAGERANK_ALGO_PRPACK, &vector, NULL, igraph_vss_all(), IGRAPH_DIRECTED, 0.85, NULL, NULL);
}

void wrapper_betweenness(ExecutionContext* ctx) {
    printf("[Wrapper] Calculating Betweenness Centrality using igraph_betweenness\n");
    // igraph_betweenness(ctx->current_graph, &vector, igraph_vss_all(), IGRAPH_DIRECTED, NULL);
}
