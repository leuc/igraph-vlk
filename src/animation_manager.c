#include "animation_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INITIAL_ANIMATION_CAPACITY 16

void
animation_manager_init (AnimationManager *am, Renderer *r, GraphData *gd)
{
  am->animations = (EdgeAnimation *)malloc (
      sizeof (EdgeAnimation) * INITIAL_ANIMATION_CAPACITY);
  if (!am->animations)
    {
      fprintf (stderr, "Failed to allocate memory for EdgeAnimation array.\n");
      am->num_animations = 0;
      am->max_animations = 0;
      return;
    }
  am->num_animations = 0;
  am->max_animations = INITIAL_ANIMATION_CAPACITY;
  am->renderer_ptr = r;
  am->graph_data_ptr = gd;
  am->animating_edge_idx = -1; // No edge is animating initially
}

void
animation_manager_cleanup (AnimationManager *am)
{
  if (am->animations)
    {
      free (am->animations);
      am->animations = NULL;
    }
  am->num_animations = 0;
  am->max_animations = 0;
  am->renderer_ptr = NULL;
  am->graph_data_ptr = NULL;
}

static void
resize_animations_array (AnimationManager *am)
{
  uint32_t new_capacity = am->max_animations * 2;
  EdgeAnimation *new_array = (EdgeAnimation *)realloc (
      am->animations, sizeof (EdgeAnimation) * new_capacity);
  if (!new_array)
    {
      fprintf (stderr, "Failed to reallocate memory for EdgeAnimation array.\n");
      return;
    }
  am->animations = new_array;
  am->max_animations = new_capacity;
}

void
animation_manager_add_edge (AnimationManager *am, uint32_t edge_id,
                            int direction)
{
  if (am->num_animations >= am->max_animations)
    {
      resize_animations_array (am);
    }

  // Check if this edge is already animating
  for (uint32_t i = 0; i < am->num_animations; ++i)
    {
      if (am->animations[i].edge_id == edge_id)
        {
          // Edge already animating, just change direction or reactivate
          am->animations[i].is_active = true;
          am->animations[i].direction = direction;
          am->animations[i].progress = (direction == 1) ? 0.0f : 1.0f;
          am->animating_edge_idx = i;
          return;
        }
    }

  EdgeAnimation *new_anim = &am->animations[am->num_animations];
  new_anim->edge_id = edge_id;
  new_anim->progress = (direction == 1) ? 0.0f : 1.0f;
  new_anim->direction = direction;
  new_anim->speed = 0.5f; // Default speed: traverse edge in 2 seconds
  new_anim->is_active = true;

  // Get edge's start and end node positions (scaled)
  const Edge *edge = &am->graph_data_ptr->edges[edge_id];
  const Node *from_node = &am->graph_data_ptr->nodes[edge->from];
  const Node *to_node = &am->graph_data_ptr->nodes[edge->to];

  // Cast const to non-const for glm_vec3_scale (which modifies dest)
  vec3 temp_from_pos, temp_to_pos;
  glm_vec3_copy(from_node->position, temp_from_pos);
  glm_vec3_copy(to_node->position, temp_to_pos);

  glm_vec3_scale (temp_from_pos, am->renderer_ptr->layoutScale,
                  new_anim->start_pos);
  glm_vec3_scale (temp_to_pos, am->renderer_ptr->layoutScale,
                  new_anim->end_pos);
  
  // For now, use a fixed color, could be derived from edge or node colors
  new_anim->particle_color[0] = 1.0f; // Red
  new_anim->particle_color[1] = 0.0f;
  new_anim->particle_color[2] = 0.0f;

  am->animating_edge_idx = am->num_animations;
  am->num_animations++;
}

void
animation_manager_remove_edge (AnimationManager *am, uint32_t edge_id)
{
  for (uint32_t i = 0; i < am->num_animations; ++i)
    {
      if (am->animations[i].edge_id == edge_id)
        {
          am->animations[i].is_active = false; // Just deactivate for now
          if (am->animating_edge_idx == (int)i) {
            am->animating_edge_idx = -1;
          }
          // Optional: compact array by swapping with last element and decrementing num_animations
          return;
        }
    }
}

void
animation_manager_toggle_edge (AnimationManager *am, uint32_t edge_id)
{
    // Find the edge in the active animations
    for (uint32_t i = 0; i < am->num_animations; ++i) {
        if (am->animations[i].edge_id == edge_id) {
            if (am->animations[i].is_active) {
                // If active, stop it.
                am->animations[i].is_active = false;
                am->animating_edge_idx = -1;
            } else {
                // If inactive, reactivate and reverse direction
                am->animations[i].is_active = true;
                am->animations[i].direction *= -1;
                // Reset progress to start or end based on new direction
                am->animations[i].progress = (am->animations[i].direction == 1) ? 0.0f : 1.0f;
                am->animating_edge_idx = i;
            }
            return;
        }
    }
    // If not found, add it, starting forward
    animation_manager_add_edge(am, edge_id, 1);
}

void
animation_manager_update (AnimationManager *am, float deltaTime)
{
  for (uint32_t i = 0; i < am->num_animations; ++i)
    {
      EdgeAnimation *anim = &am->animations[i];
      if (anim->is_active)
        {
          anim->progress += anim->direction * anim->speed * deltaTime;

          if (anim->progress < 0.0f)
            {
              anim->progress = 0.0f;
              anim->direction = 1; // Reverse
            }
          else if (anim->progress > 1.0f)
            {
              anim->progress = 1.0f;
              anim->direction = -1; // Reverse
            }
        }
    }
}

void
animation_manager_get_particle_data (AnimationManager *am, vec3 *out_positions, vec3 *out_colors,
                                     uint32_t *out_count)
{
  uint32_t count = 0;
  for (uint32_t i = 0; i < am->num_animations; ++i)
    {
      EdgeAnimation *anim = &am->animations[i];
      if (anim->is_active)
        {
          // Linear interpolation for particle position
          glm_vec3_lerp (anim->start_pos, anim->end_pos, anim->progress,
                         out_positions[count]);
          glm_vec3_copy(anim->particle_color, out_colors[count]);
          count++;
        }
    }
  *out_count = count;
}
