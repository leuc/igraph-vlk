#ifndef LAYERED_SPHERE_H
#define LAYERED_SPHERE_H

#include "graph_loader.h"
#include <stdbool.h>

typedef struct LayeredSphereContext {
    bool initialized;
    bool first_pass_done;     // Tracks if initial placement is complete
    int current_iter;
    int total_iters;
    
    // State persistence for the algorithm
    double current_energy;
    igraph_vector_int_t degrees; // Cache degrees so we don't recalculate every frame
} LayeredSphereContext;

void layered_sphere_init(LayeredSphereContext* ctx, int node_count);
void layered_sphere_cleanup(LayeredSphereContext* ctx);
bool layered_sphere_step(LayeredSphereContext* ctx, GraphData* graph);
const char* layered_sphere_get_stage_name(int stage_id);

#endif