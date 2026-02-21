#ifndef LAYERED_SPHERE_H
#define LAYERED_SPHERE_H

#include <stdbool.h>
#include <igraph.h>

// Assuming your GraphData struct is defined here
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
    
    // Dynamic Scaling Parameters
    int num_spheres;
    
    // Graph properties mapped to nodes
    igraph_vector_int_t sphere_ids; // Maps node_id -> sphere_id
    
    // Edge cutting
    bool* cut_edges; 
    
    // State tracking for Inter-Sphere Odd/Even passes
    int inter_sphere_pass; 
} LayeredSphereContext;

void layered_sphere_init(LayeredSphereContext* ctx, int node_count);
void layered_sphere_cleanup(LayeredSphereContext* ctx);
bool layered_sphere_step(LayeredSphereContext* ctx, GraphData* graph);
const char* layered_sphere_get_stage_name(LayeredSphereContext* ctx);

#endif // LAYERED_SPHERE_H
