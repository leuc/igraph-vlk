#ifndef LAYOUT_OPENORD_H
#define LAYOUT_OPENORD_H

#include "graph_loader.h"
#include <stdbool.h>

// OpenOrd parameters
typedef struct
{
  int iterations;
  float temperature;
  float attraction;
  float damping_mult;
  // Stage-specific
  float cut_end;
  float cut_length_end;
  float cut_off_length;
  float cut_rate;
  float min_edges;
} OpenOrdStage;

typedef struct OpenOrdContext
{
  int stage_id; // 0:Liquid, 1:Expansion, 2:Cooldown, 3:Crunch, 4:Simmer,
                // 5:Done
  int current_iter;
  int total_iters;

  // Grid settings
  int grid_size;
  float view_size;
  float radius;
  float *density_grid; // Flattened 3D array [grid_size^3]

  // Node state
  vec3 *positions; // Current positions
  float *energies; // Node energies

  // Schedule
  OpenOrdStage stages[5];

  // Cutting parameters
  float cut_end;
  float cut_length_end;
  float cut_off_length;
  float cut_rate;
  float min_edges;

  bool initialized;
  int num_threads;
} OpenOrdContext;

void openord_init (OpenOrdContext *ctx, int node_count, int grid_size);
void openord_cleanup (OpenOrdContext *ctx);
bool openord_step (OpenOrdContext *ctx,
                   GraphData *graph); // Returns true if running, false if done
const char *openord_get_stage_name (int stage_id);

#endif
