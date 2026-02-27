#include "interaction/camera.h"
#include <cglm/cglm.h>
#include <math.h>
#include <stdio.h>
#include <stdbool.h>

void camera_init(Camera *cam) {
    cam->pos[0] = 0.0f;
    cam->pos[1] = 0.0f;
    cam->pos[2] = 50.0f;
    cam->front[0] = 0.0f;
    cam->front[1] = 0.0f;
    cam->front[2] = -1.0f;
    cam->up[0] = 0.0f;
    cam->up[1] = 1.0f;
    cam->up[2] = 0.0f;
    cam->yaw = -90.0f;
    cam->pitch = 0.0f;
    cam->last_x = 1720.0f;
    cam->last_y = 720.0f;
    cam->first_mouse = true;
    cam->sensitivity = 0.1f;
    cam->move_speed = 20.0f;
}

void camera_process_keyboard(Camera *cam, int direction, float delta_time) {
    float speed = cam->move_speed * delta_time;
    vec3 temp;

    // Direction is one of: CAMERA_DIR_FORWARD, BACKWARD, LEFT, RIGHT, UP, DOWN
    // For simplicity, we handle forward/back and left/right via the front vector,
    // and up/down via the world up vector.
    
    if (direction == CAMERA_DIR_FORWARD) {
        glm_vec3_scale(cam->front, speed, temp);
        glm_vec3_add(cam->pos, temp, cam->pos);
    } else if (direction == CAMERA_DIR_BACKWARD) {
        glm_vec3_scale(cam->front, -speed, temp);
        glm_vec3_add(cam->pos, temp, cam->pos);
    } else if (direction == CAMERA_DIR_LEFT) {
        vec3 cross;
        glm_vec3_cross(cam->up, cam->front, cross);
        glm_vec3_normalize(cross);
        // up × front points left, so use +speed
        glm_vec3_scale(cross, speed, temp);
        glm_vec3_add(cam->pos, temp, cam->pos);
    } else if (direction == CAMERA_DIR_RIGHT) {
        vec3 cross;
        glm_vec3_cross(cam->up, cam->front, cross);
        glm_vec3_normalize(cross);
        // up × front points left, negate for right
        glm_vec3_scale(cross, -speed, temp);
        glm_vec3_add(cam->pos, temp, cam->pos);
    } else if (direction == CAMERA_DIR_UP) {
        glm_vec3_scale(cam->up, speed, temp);
        glm_vec3_add(cam->pos, temp, cam->pos);
    } else if (direction == CAMERA_DIR_DOWN) {
        glm_vec3_scale(cam->up, -speed, temp);
        glm_vec3_add(cam->pos, temp, cam->pos);
    }
}

void camera_process_mouse(Camera *cam, float xpos, float ypos) {
    if (cam->first_mouse) {
        cam->last_x = xpos;
        cam->last_y = ypos;
        cam->first_mouse = false;
    }

    float xoffset = (xpos - cam->last_x) * cam->sensitivity;
    float yoffset = (cam->last_y - ypos) * cam->sensitivity;

    cam->last_x = xpos;
    cam->last_y = ypos;

    cam->yaw += xoffset;
    cam->pitch += yoffset;

    if (cam->pitch > 89.0f)
        cam->pitch = 89.0f;
    if (cam->pitch < -89.0f)
        cam->pitch = -89.0f;

    camera_update_vectors(cam);
}

void camera_update_vectors(Camera *cam) {
    vec3 front;
    front[0] = cosf(glm_rad(cam->yaw)) * cosf(glm_rad(cam->pitch));
    front[1] = sinf(glm_rad(cam->pitch));
    front[2] = sinf(glm_rad(cam->yaw)) * cosf(glm_rad(cam->pitch));
    glm_vec3_normalize_to(front, cam->front);
}

