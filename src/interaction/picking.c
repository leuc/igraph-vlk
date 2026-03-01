#include "interaction/picking.h"
#include "graph/graph_core.h"
#include "vulkan/animation_manager.h"
#include "vulkan/text.h"
#include <cglm/cglm.h>
#include <float.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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

extern FontAtlas globalAtlas;

static bool picking_ray_quad_intersection(vec3 ray_ori, vec3 ray_dir, 
                                         vec3 quad_center, vec3 right, vec3 up, 
                                         float width, float height, float* t_out) {
    // 1. Ray-plane intersection
    // Normal of the billboard plane is cross(right, up) -> roughly -front
    vec3 normal;
    glm_vec3_cross(right, up, normal);
    glm_vec3_normalize(normal);

    float denom = glm_vec3_dot(normal, ray_dir);
    if (fabsf(denom) < 0.0001f) return false; // Parallel

    vec3 center_to_ori;
    glm_vec3_sub(quad_center, ray_ori, center_to_ori);
    float t = glm_vec3_dot(center_to_ori, normal) / denom;

    if (t < 0.0f) return false;

    // 2. Point in quad check
    vec3 hit_p;
    glm_vec3_scale(ray_dir, t, hit_p);
    glm_vec3_add(ray_ori, hit_p, hit_p);

    vec3 hit_local;
    glm_vec3_sub(hit_p, quad_center, hit_local);

    float u = glm_vec3_dot(hit_local, right) / glm_vec3_norm(right);
    float v = glm_vec3_dot(hit_local, up) / glm_vec3_norm(up);

    // Quad bounds are [-width/2, width/2] and [-height/2, height/2]
    // BUT our menu boxes are centered around the TEXT ANCHOR, which has an offset in renderer
    // In renderer: 
    //   if (current->label) {
    //     glm_vec3_scale(right, box_width * 0.5f, offset);
    //     glm_vec3_add(instances[instance_count].worldPos, offset, instances[instance_count].worldPos);
    //   }
    // This means world_pos passed here IS already the center of the box.
    
    if (fabsf(u) <= width * 0.5f && fabsf(v) <= height * 0.5f) {
        *t_out = t;
        return true;
    }

    return false;
}

static MenuNode* pick_menu_recursive(AppState* state, MenuNode* node, float* ray_ori, float* ray_dir, float* min_t) {
    if (node == NULL || node->current_radius < 0.1f) return NULL;
    
    MenuNode* best_hit = NULL;
    
    // Captured orientation at spawn
    vec3 spawn_pos, spawn_front, spawn_up;
    glm_vec3_copy(state->app_ctx.menu_spawn_pos, spawn_pos);
    glm_vec3_copy(state->app_ctx.menu_spawn_front, spawn_front);
    glm_vec3_copy(state->app_ctx.menu_spawn_up, spawn_up);

    // Basis vectors (must match generate_vulkan_menu_buffers)
    vec3 right, up;
    glm_vec3_cross(spawn_front, spawn_up, right);
    glm_vec3_normalize(right);
    glm_vec3_cross(right, spawn_front, up);
    glm_vec3_normalize(up);

    // Billboard position calculation (matches renderer precisely)
    float x_off = node->target_phi - 0.8f;
    float y_off = node->target_theta;
    
    vec3 world_pos;
    glm_vec3_copy(spawn_pos, world_pos);
    
    vec3 f_part, r_part, u_part;
    glm_vec3_scale(spawn_front, 2.5f, f_part); // 2.5m away
    glm_vec3_scale(right, x_off, r_part);
    glm_vec3_scale(up, y_off, u_part);
    
    glm_vec3_add(world_pos, f_part, world_pos);
    glm_vec3_add(world_pos, r_part, world_pos);
    glm_vec3_add(world_pos, u_part, world_pos);

    // Calculate dimensions (matches renderer)
    float world_text_scale = 0.003f;
    float padding_x = 0.08f;
    float fixed_height = 0.09f;
    float total_w = 0.0f;
    if (node->label) {
        int label_len = strlen(node->label);
        for (int i = 0; i < label_len; i++) {
            unsigned char c = node->label[i];
            CharInfo *ci = (c < 128) ? &globalAtlas.chars[c] : &globalAtlas.chars[32];
            total_w += ci->xadvance;
        }
    }
    float box_width = (total_w * world_text_scale) + padding_x;
    float box_height = fixed_height;

    // Apply the same centering offset as renderer
    if (node->label) {
        vec3 offset;
        glm_vec3_scale(right, box_width * 0.5f, offset);
        glm_vec3_add(world_pos, offset, world_pos);
    }

    // Adjust for current expansion radius
    box_width *= node->current_radius;
    box_height *= node->current_radius;
    
    float t;
    if (picking_ray_quad_intersection(ray_ori, ray_dir, world_pos, right, up, box_width, box_height, &t)) {
        if (t > 0 && t < *min_t) {
            *min_t = t;
            best_hit = node;
        }
    }
    
    for (int i = 0; i < node->num_children; i++) {
        MenuNode* child_hit = pick_menu_recursive(state, node->children[i], ray_ori, ray_dir, min_t);
        if (child_hit) {
            best_hit = child_hit;
        }
    }
    
    return best_hit;
}

void clear_menu_hover_recursive(MenuNode* node) {
    if (!node) return;
    node->hovered = false;
    for (int i = 0; i < node->num_children; i++) {
        clear_menu_hover_recursive(node->children[i]);
    }
}

/**
 * Raycast from the crosshair (center of screen) to pick a menu node.
 * Uses camera position as origin and camera front as direction.
 */
MenuNode* raycast_menu_crosshair(AppState* state) {
    clear_menu_hover_recursive(state->app_ctx.root_menu);
    float min_t = FLT_MAX;
    MenuNode* hit = pick_menu_recursive(state, state->app_ctx.root_menu, state->camera.pos, state->camera.front, &min_t);
    if (hit) hit->hovered = true;
    return hit;
}

MenuNode* interaction_pick_menu_node(AppState* state, double mouse_x, double mouse_y) {
    float x = (2.0f * (float)mouse_x) / state->win_w - 1.0f;
    float y = 1.0f - (2.0f * (float)mouse_y) / state->win_h;
    
    vec3 ray_dir;
    vec3 right, up;
    glm_vec3_cross(state->camera.front, state->camera.up, right);
    glm_vec3_normalize(right);
    glm_vec3_cross(right, state->camera.front, up);
    glm_vec3_normalize(up);
    
    glm_vec3_copy(state->camera.front, ray_dir);
    vec3 right_offset, up_offset;
    glm_vec3_scale(right, x * 0.5f, right_offset); 
    glm_vec3_scale(up, y * 0.5f, up_offset);
    glm_vec3_add(ray_dir, right_offset, ray_dir);
    glm_vec3_add(ray_dir, up_offset, ray_dir);
    glm_vec3_normalize(ray_dir);
    
    float min_t = FLT_MAX;
    return pick_menu_recursive(state, state->app_ctx.root_menu, state->camera.pos, ray_dir, &min_t);
}
