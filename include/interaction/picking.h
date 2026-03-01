#pragma once

#include <stdbool.h>
#include "app_state.h"

/**
 * Perform ray-sphere intersection test.
 * @param ori Ray origin
 * @param dir Ray direction (normalized)
 * @param center Sphere center
 * @param radius Sphere radius
 * @param t Output parameter for intersection distance
 * @return true if intersection occurred
 */
bool picking_ray_sphere_intersection(float *ori, float *dir, float *center,
                                     float radius, float *t);

/**
 * Calculate distance from ray to line segment.
 * @param ori Ray origin
 * @param dir Ray direction
 * @param p1 Segment start point
 * @param p2 Segment end point
 * @param t_out Output parameter for closest point parameter on ray
 * @return Distance from ray to segment
 */
float picking_dist_ray_segment(float *ori, float *dir, float *p1, float *p2,
                               float *t_out);

/**
 * Pick an object (node or edge) using raycasting from the camera.
 * @param state Pointer to the application state
 * @param is_double_click True if this is a double-click event
 */
void interaction_pick_object(AppState *state, bool is_double_click);

/**
 * Pick a menu node when the sphere menu is open.
 */
MenuNode* interaction_pick_menu_node(AppState* state, double mouse_x, double mouse_y);

/**
 * Raycast from the crosshair (center of screen) to pick a menu node.
 * Used for immersive 3D menu interaction without releasing mouse lock.
 */
MenuNode* raycast_menu_crosshair(AppState* state);


