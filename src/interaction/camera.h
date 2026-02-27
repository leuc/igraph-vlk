#pragma once

#include <cglm/cglm.h>

/**
 * Camera direction constants for keyboard input.
 */
#define CAMERA_DIR_FORWARD  0
#define CAMERA_DIR_BACKWARD 1
#define CAMERA_DIR_LEFT     2
#define CAMERA_DIR_RIGHT    3
#define CAMERA_DIR_UP       4
#define CAMERA_DIR_DOWN     5

/**
 * Camera state for 3D navigation.
 * Supports WASD movement and mouse look (yaw/pitch).
 */
typedef struct {
    vec3 pos;
    vec3 front;
    vec3 up;
    float yaw;
    float pitch;
    float last_x;
    float last_y;
    bool first_mouse;
    float sensitivity;
    float move_speed;
} Camera;

/**
 * Initialize camera with default position and orientation.
 */
void camera_init(Camera *cam);

/**
 * Process keyboard input for camera movement.
 * @param cam Pointer to camera
 * @param direction: CAMERA_DIR_FORWARD, BACKWARD, LEFT, RIGHT, UP, or DOWN
 * @param delta_time Time since last frame in seconds
 */
void camera_process_keyboard(Camera *cam, int direction, float delta_time);

/**
 * Process mouse movement for camera look.
 * @param cam Pointer to camera
 * @param xpos Mouse X position
 * @param ypos Mouse Y position
 */
void camera_process_mouse(Camera *cam, float xpos, float ypos);

/**
 * Update camera vectors based on current yaw/pitch.
 * Should be called after modifying yaw or pitch directly.
 */
void camera_update_vectors(Camera *cam);

