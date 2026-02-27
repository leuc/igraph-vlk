#ifndef LAYERED_SPHERE_H
#define LAYERED_SPHERE_H

#include <igraph.h>
#include <stdbool.h>

#include "cglm/vec3.h"
#include "graph/graph_types.h"

// Define a physical grid slot on a sphere
typedef struct {
	double x, y, z;
	int hilbert_dist;
} SpherePoint;

// Manages the sparse grid for a single sphere layer
typedef struct {
	int max_slots;
	int num_occupants;
	double radius;
	SpherePoint *slots;
	int *slot_occupant; // -1 if empty, Node ID if occupied
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

	int phase_iter;

	int num_spheres;
	SphereGrid *grids;

	int *node_to_sphere_id;
	int *node_to_slot_idx;
	int *node_to_comm_id;
	int inter_sphere_pass;
} LayeredSphereContext;

void layered_sphere_init(LayeredSphereContext *ctx, int node_count);
void layered_sphere_cleanup(LayeredSphereContext *ctx);
bool layered_sphere_step(LayeredSphereContext *ctx, GraphData *graph);
const char *layered_sphere_get_stage_name(LayeredSphereContext *ctx);

#endif // LAYERED_SPHERE_H
