#ifndef LAYERED_SPHERE_H
#define LAYERED_SPHERE_H

#include <stdbool.h>
#include <igraph.h>

// Assuming your GraphData struct is defined in this header, 
// or adjust the include to match your project structure.
#include "graph_loader.h" 

typedef enum {
    PHASE_INIT = 0,
    PHASE_INTRA_SPHERE = 1,
    PHASE_INTER_SPHERE = 2,
    PHASE_DONE = 3
} LayoutPhase;

typedef struct LayeredSphereContext {
    bool initialized;
    LayoutPhase phase;
    int current_iter;
    int total_iters;
    
    // Graph properties
    igraph_vector_int_t degrees;        // Now storing K-Coreness
    
    // For inside-out processing
    igraph_vector_int_t unique_degrees; // Sorted highest to lowest
    int current_degree_idx;             // Which sphere are we currently optimizing?
    
    // Edge cutting based on Jaccard Similarity
    bool* cut_edges;                    // Flat array tracking weak ties
} LayeredSphereContext;

void layered_sphere_init(LayeredSphereContext* ctx, int node_count);
void layered_sphere_cleanup(LayeredSphereContext* ctx);
bool layered_sphere_step(LayeredSphereContext* ctx, GraphData* graph);
const char* layered_sphere_get_stage_name(LayeredSphereContext* ctx);

#endif // LAYERED_SPHERE_H