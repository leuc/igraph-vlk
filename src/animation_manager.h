#ifndef ANIMATION_MANAGER_H
#define ANIMATION_MANAGER_H

#include <cglm/cglm.h>
#include <stdbool.h>
#include <stdint.h>

#include "graph_loader.h"
#include "renderer.h"

#define MAX_ANIMATIONS 256 // Max number of concurrent animations

// Represents a single edge animation
typedef struct EdgeAnimation
{
  uint32_t edge_id;
  float progress; // 0.0 to 1.0 along the edge
  int direction;  // 1 for forward, -1 for backward
  float speed;    // units per second (e.g., progress per second)
  bool is_active;
  
  // For linear interpolation between two points
  vec3 start_pos;
  vec3 end_pos;
  vec3 particle_color; // Color of the animating particle
  float particle_size;
  float particle_glow;
} EdgeAnimation;

// Manages all active animations
typedef struct AnimationManager
{
  EdgeAnimation *animations;
  uint32_t num_animations;
  uint32_t max_animations; // Current capacity
  
  // Pointers to external data
  Renderer *renderer_ptr;
  GraphData *graph_data_ptr;

  // Store ID of the currently animating edge (if any) to prevent re-adding
  int animating_edge_idx; // -1 if no edge is animating or newly added
} AnimationManager;

// Function prototypes
void animation_manager_init (AnimationManager *am, Renderer *r, GraphData *gd);
void animation_manager_cleanup (AnimationManager *am);
void animation_manager_update (AnimationManager *am, float deltaTime);
void animation_manager_toggle_edge (AnimationManager *am, uint32_t edge_id);
void animation_manager_add_edge (AnimationManager *am, uint32_t edge_id, int direction);
void animation_manager_remove_edge (AnimationManager *am, uint32_t edge_id);
void animation_manager_get_particle_data (AnimationManager *am, vec3 *out_positions, vec3 *out_colors, uint32_t *out_count);

#endif // ANIMATION_MANAGER_H
