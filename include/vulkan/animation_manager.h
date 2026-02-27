#ifndef ANIMATION_MANAGER_H
#define ANIMATION_MANAGER_H

#include <cglm/cglm.h>
#include <stdbool.h>
#include <stdint.h>

#include "graph/graph_types.h"
#include "vulkan/renderer.h"

#define MAX_ANIMATIONS 256 // Max number of concurrent animations

// Represents a single edge animation. This struct holds animation state for an
// edge. The actual visual effect is driven by the edge shaders.
typedef struct EdgeAnimation {
	uint32_t edge_id;
	float speed; // units per second (e.g., progress per second)
	bool is_active;
} EdgeAnimation;

// Manages all active animations
typedef struct AnimationManager {
	EdgeAnimation *animations;
	uint32_t num_animations;
	uint32_t max_animations; // Current capacity

	// Pointers to external data
	Renderer *renderer_ptr;
	GraphData *graph_data_ptr;
} AnimationManager;

// Function prototypes
void animation_manager_init(AnimationManager *am, Renderer *r, GraphData *gd);
void animation_manager_cleanup(AnimationManager *am);
void animation_manager_update(AnimationManager *am, float deltaTime);
void animation_manager_toggle_edge(AnimationManager *am, uint32_t edge_id,
								   int direction);
void animation_manager_remove_edge(AnimationManager *am, uint32_t edge_id);

#endif // ANIMATION_MANAGER_H
