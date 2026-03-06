#pragma once

#include "app_state.h"
#include <stdbool.h>

/**
 * Perform ray-sphere intersection test.
 * @param ori Ray origin
 * @param dir Ray direction (normalized)
 * @param center Sphere center
 * @param radius Sphere radius
 * @param t Output parameter for intersection distance
 * @return true if intersection occurred
 */
bool picking_ray_sphere_intersection(float *ori, float *dir, float *center, float radius, float *t);

/**
 * Calculate distance from ray to line segment.
 * @param ori Ray origin
 * @param dir Ray direction
 * @param p1 Segment start point
 * @param p2 Segment end point
 * @param t_out Output parameter for closest point parameter on ray
 * @return Distance from ray to segment
 */
float picking_dist_ray_segment(float *ori, float *dir, float *p1, float *p2, float *t_out);

/**
 * Perform ray-quad intersection test for billboard picking.
 * @param ray_ori Ray origin
 * @param ray_dir Ray direction (normalized)
 * @param quad_center Center of the quad in world space
 * @param right Right vector of the billboard
 * @param up Up vector of the billboard
 * @param width Width of the quad
 * @param height Height of the quad
 * @param t_out Output parameter for intersection distance
 * @return true if intersection occurred
 */
bool picking_ray_quad_intersection(vec3 ray_ori, vec3 ray_dir, vec3 quad_center, vec3 right, vec3 up, float width, float height, float *t_out);

/**
 * Pick an object (node or edge) using raycasting from the camera.
 * @param state Pointer to the application state
 * @param is_double_click True if this is a double-click event
 */
void interaction_pick_object(AppState *state, bool is_double_click);
