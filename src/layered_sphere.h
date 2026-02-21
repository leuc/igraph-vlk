#ifndef LAYERED_SPHERE_H
#define LAYERED_SPHERE_H

#include <stdbool.h>
#include <igraph.h>
#include "graph_loader.h" 

// Define a physical grid slot on a sphere
typedef struct {
    double x, y, z;
    int hilbert_dist;
} SpherePoint;

// Manages the sparse grid for a single sphere layer
typedef struct {
    int max_slots;
    int num_occupants;
    SpherePoint* slots;      // The physical coordinates of all available slots
    int* slot_occupant;      // Array mapped to slots: -1 if empty, Node ID if occupied
} SphereGrid;

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
    
    int phase_iter; 
    
    int num_spheres;
    SphereGrid* grids;            // Array of size num_spheres
    
    int* node_to_sphere_id;       // Fast lookup: node_id -> sphere_id
    int* node_to_slot_idx;        // Fast lookup: node_id -> slot_idx (where is the node?)
    
    bool* cut_edges; 
    int inter_sphere_pass; 
} LayeredSphereContext;

void layered_sphere_init(LayeredSphereContext* ctx, int node_count);
void layered_sphere_cleanup(LayeredSphereContext* ctx);
bool layered_sphere_step(LayeredSphereContext* ctx, GraphData* graph);
const char* layered_sphere_get_stage_name(LayeredSphereContext* ctx);

#endif // LAYERED_SPHERE_H
