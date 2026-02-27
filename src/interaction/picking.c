#include "picking.h"
#include "graph/graph_core.h"
#include "vulkan/animation_manager.h"
#include <cglm/cglm.h>
#include <float.h>
#include <stdio.h>

bool picking_ray_sphere_intersection(float *ori, float *dir, float *center,
                                     float radius, float *t) {
    vec3 oc;
    glm_vec3_sub(ori, center, oc);
    float b = glm_vec3_dot(oc, dir);
    float c = glm_vec3_dot(oc, oc) - radius * radius;
    float h = b * b - c;
    if (h < 0.0f)
        return false;
    h = sqrtf(h);
    *t = -b - h;
    return true;
}

float picking_dist_ray_segment(float *ori, float *dir, float *p1, float *p2,
                               float *t_out) {
    vec3 u, v, w;
    glm_vec3_sub(p2, p1, u);
    glm_vec3_copy(dir, v);
    glm_vec3_sub(p1, ori, w);
    float a = glm_vec3_dot(u, u);
    float b = glm_vec3_dot(u, v);
    float c = glm_vec3_dot(v, v);
    float d = glm_vec3_dot(u, w);
    float e = glm_vec3_dot(v, w);
    float D = a * c - b * b;
    float sc, tc;
    if (D < 0.000001f) {
        sc = 0.0f;
        tc = (b > c ? d / b : e / c);
    } else {
        sc = (b * e - c * d) / D;
        tc = (a * e - b * d) / D;
    }
    if (sc < 0.0f)
        sc = 0.0f;
    else if (sc > 1.0f)
        sc = 1.0f;
    vec3 p_seg, p_ray;
    glm_vec3_scale(u, sc, p_seg);
    glm_vec3_add(p1, p_seg, p_seg);
    glm_vec3_scale(v, tc, p_ray);
    glm_vec3_add(ori, p_ray, p_ray);
    *t_out = tc;
    return glm_vec3_distance(p_seg, p_ray);
}

void interaction_pick_object(AppState *state, bool is_double_click) {
    float min_t = FLT_MAX;
    int hit_node = -1;
    int hit_edge = -1;

    // Clear previous selection
    for (uint32_t i = 0; i < state->current_graph.node_count; i++)
        state->current_graph.nodes[i].selected = 0.0f;
    for (uint32_t i = 0; i < state->current_graph.edge_count; i++)
        state->current_graph.edges[i].selected = 0.0f;

    // Clear previous animations if a new object is picked or on double-click
    if (is_double_click ||
        (state->last_picked_node != -1 && hit_node != state->last_picked_node) ||
        (state->last_picked_edge != -1 && hit_edge != state->last_picked_edge)) {
        animation_manager_cleanup(&state->anim_manager);
        animation_manager_init(&state->anim_manager, &state->renderer,
                               &state->current_graph);
    }

    for (uint32_t i = 0; i < state->current_graph.node_count; i++) {
        vec3 pos;
        glm_vec3_scale(state->current_graph.nodes[i].position,
                       state->renderer.layoutScale, pos);
        float radius = state->current_graph.nodes[i].size * 0.5f;
        float t;
        if (picking_ray_sphere_intersection(state->camera.pos, state->camera.front,
                                            pos, radius, &t)) {
            if (t > 0 && t < min_t) {
                min_t = t;
                hit_node = i;
                hit_edge = -1;
            }
        }
    }
    for (uint32_t i = 0; i < state->current_graph.edge_count; i++) {
        vec3 p1, p2;
        glm_vec3_scale(state->current_graph.nodes[state->current_graph.edges[i].from].position,
                       state->renderer.layoutScale, p1);
        glm_vec3_scale(state->current_graph.nodes[state->current_graph.edges[i].to].position,
                       state->renderer.layoutScale, p2);
        float t, dist;
        dist = picking_dist_ray_segment(state->camera.pos, state->camera.front,
                                        p1, p2, &t);
        if (dist < 0.2f && t > 0 && t < min_t) {
            min_t = t;
            hit_node = -1;
            hit_edge = i;
        }
    }
    if (hit_node != -1) {
        state->current_graph.nodes[hit_node].selected = 1.0f;
        printf("%s Clicked Node %d: %s\n", is_double_click ? "Double" : "Single",
               hit_node,
               state->current_graph.nodes[hit_node].label
                   ? state->current_graph.nodes[hit_node].label
                   : "no label");
        state->last_picked_node = hit_node;

        if (is_double_click) {
            printf("Double-clicked node %d. Animating connected edges.\n",
                   hit_node);
            for (uint32_t i = 0; i < state->current_graph.edge_count; ++i) {
                uint32_t edge_from = state->current_graph.edges[i].from;
                uint32_t edge_to = state->current_graph.edges[i].to;
                int direction = 0;

                if (edge_from == (uint32_t)hit_node) {
                    direction = 1; // From hit_node to edge.to
                } else if (edge_to == (uint32_t)hit_node) {
                    direction = -1; // From hit_node to edge.from (i.e. backward)
                }

                if (direction != 0) {
                    animation_manager_toggle_edge(&state->anim_manager, i,
                                                  direction);
                    printf("From: %d To: %d \n", edge_from, edge_to);
                }
            }
        }
    } else if (hit_edge != -1) {
        state->current_graph.edges[hit_edge].selected = 1.0f;
        printf("%s Clicked Edge %d: %d -> %d\n",
               is_double_click ? "Double" : "Single", hit_edge,
               state->current_graph.edges[hit_edge].from,
               state->current_graph.edges[hit_edge].to);
        state->last_picked_node = -1; // Clear node selection
    } else {
        state->last_picked_node = -1;
    }

    renderer_update_graph(&state->renderer, &state->current_graph);
}

