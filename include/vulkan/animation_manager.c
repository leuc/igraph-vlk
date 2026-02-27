#include "animation_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void animation_manager_init(AnimationManager *am, Renderer *r, GraphData *gd) {
	am->animations =
		(EdgeAnimation *)malloc(sizeof(EdgeAnimation) * MAX_ANIMATIONS);
	if (!am->animations) {
		fprintf(stderr, "Failed to allocate memory for EdgeAnimation array.\n");
		am->num_animations = 0;
		am->max_animations = 0;
		return;
	}
	am->num_animations = 0;
	am->max_animations = MAX_ANIMATIONS;
	am->renderer_ptr = r;
	am->graph_data_ptr = gd;
}

void animation_manager_cleanup(AnimationManager *am) {
	if (am->animations) {
		// Before freeing, ensure all edges are marked as not animating
		for (uint32_t i = 0; i < am->num_animations; ++i) {
			if (am->animations[i].is_active) {
				am->graph_data_ptr->edges[am->animations[i].edge_id]
					.is_animating = false;
			}
		}
		free(am->animations);
		am->animations = NULL;
	}
	am->num_animations = 0;
	am->max_animations = 0;
	am->renderer_ptr = NULL;
	am->graph_data_ptr = NULL;
}

static void resize_animations_array(AnimationManager *am) {
	uint32_t new_capacity = am->max_animations * 2;
	EdgeAnimation *new_array = (EdgeAnimation *)realloc(
		am->animations, sizeof(EdgeAnimation) * new_capacity);
	if (!new_array) {
		fprintf(stderr,
				"Failed to reallocate memory for EdgeAnimation array.\n");
		return;
	}
	am->animations = new_array;
	am->max_animations = new_capacity;
}

void animation_manager_add_edge(AnimationManager *am, uint32_t edge_id,
								int direction) {
	if (am->num_animations >= am->max_animations) {
		resize_animations_array(am);
	}

	// Set animation state directly on the edge in GraphData
	Edge *edge_to_animate = &am->graph_data_ptr->edges[edge_id];

	// Check if this edge is already animating via the manager
	for (uint32_t i = 0; i < am->num_animations; ++i) {
		if (am->animations[i].edge_id == edge_id) {
			// Edge already in manager, just update its active state and
			// direction
			am->animations[i].is_active = true;
			edge_to_animate->is_animating = true;
			edge_to_animate->animation_direction = direction;
			edge_to_animate->animation_progress =
				(direction == 1) ? 0.0f : 1.0f;
			return;
		}
	}

	// If not found in manager, add a new animation entry
	EdgeAnimation *new_anim = &am->animations[am->num_animations];
	new_anim->edge_id = edge_id;
	new_anim->speed = 4.2f; // Default speed: traverse edge in 2 seconds
	new_anim->is_active = true;

	edge_to_animate->is_animating = true;
	edge_to_animate->animation_direction = direction;
	edge_to_animate->animation_progress = (direction == 1) ? 0.0f : 1.0f;
	edge_to_animate->animation_speed = new_anim->speed;

	am->num_animations++;
}

void animation_manager_remove_edge(AnimationManager *am, uint32_t edge_id) {
	for (uint32_t i = 0; i < am->num_animations; ++i) {
		if (am->animations[i].edge_id == edge_id) {
			am->animations[i].is_active = false;
			// Mark the edge in GraphData as not animating
			am->graph_data_ptr->edges[edge_id].is_animating = false;
			// Optional: compact array by swapping with last element and
			// decrementing num_animations
			return;
		}
	}
}

void animation_manager_toggle_edge(AnimationManager *am, uint32_t edge_id,
								   int direction) {
	// Find the edge in the active animations
	for (uint32_t i = 0; i < am->num_animations; ++i) {
		if (am->animations[i].edge_id == edge_id) {
			Edge *animated_edge = &am->graph_data_ptr->edges[edge_id];
			if (am->animations[i].is_active) {
				// If active, stop it.
				am->animations[i].is_active = false;
				animated_edge->is_animating = false;
			} else {
				// If inactive, reactivate and reverse direction
				am->animations[i].is_active = true;
				animated_edge->is_animating = true;
				animated_edge->animation_direction *= -1;
				// Reset progress to start or end based on new direction
				animated_edge->animation_progress =
					(animated_edge->animation_direction == 1) ? 0.0f : 1.0f;
			}
			return;
		}
	}
	// If not found, add it, starting with the provided direction
	animation_manager_add_edge(am, edge_id, direction);
}

void animation_manager_update(AnimationManager *am, float deltaTime) {
	for (uint32_t i = 0; i < am->num_animations; ++i) {
		EdgeAnimation *anim = &am->animations[i];
		if (anim->is_active) {
			Edge *animated_edge = &am->graph_data_ptr->edges[anim->edge_id];

			// 1. Get the 3D positions of the start and end nodes
			vec3 *pos_from =
				&am->graph_data_ptr->nodes[animated_edge->from].position;
			vec3 *pos_to =
				&am->graph_data_ptr->nodes[animated_edge->to].position;

			// 2. Calculate the physical length of the edge
			float edge_length = glm_vec3_distance(*pos_from, *pos_to);

			// 3. Normalize the speed so it traverses 'animation_speed' world
			// units per second
			float normalized_speed = animated_edge->animation_speed;
			if (edge_length > 0.0001f) { // Prevent division by zero
				normalized_speed = animated_edge->animation_speed / edge_length;
			}

			// 4. Apply the normalized speed
			animated_edge->animation_progress +=
				animated_edge->animation_direction * normalized_speed *
				deltaTime;

			if (animated_edge->animation_progress < 0.0f) {
				animated_edge->animation_progress = 0.0f;
				animated_edge->animation_direction = 1; // Reverse
			} else if (animated_edge->animation_progress > 1.0f) {
				animated_edge->animation_progress = 1.0f;
				animated_edge->animation_direction = -1; // Reverse
			}
		}
	}
}
