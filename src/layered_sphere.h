#ifndef LAYERED_SPHERE_H
#define LAYERED_SPHERE_H

#include <stdbool.h>
#include <igraph.h>
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
    
    // Tracks iterations within a specific phase for Simulated Annealing
    int phase_iter; 
    
    int num_spheres;
    igraph_vector_int_t sphere_ids;
    bool* cut_edges; 
    int inter_sphere_pass; 
} LayeredSphereContext;

void layered_sphere_init(LayeredSphereContext* ctx, int node_count);
void layered_sphere_cleanup(LayeredSphereContext* ctx);
bool layered_sphere_step(LayeredSphereContext* ctx, GraphData* graph);
const char* layered_sphere_get_stage_name(LayeredSphereContext* ctx);

#endif // LAYERED_SPHERE_H
