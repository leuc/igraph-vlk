/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "interaction/picking.h"
#include "graph/graph_core.h"
#include <cglm/cglm.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

bool picking_ray_sphere_intersection(float *ori, float *dir, float *center, float radius, float *t)
{
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

float picking_dist_ray_segment(float *ori, float *dir, float *p1, float *p2, float *t_out)
{
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

void interaction_pick_object(AppState *state, bool is_double_click)
{
	float min_t = FLT_MAX;
	int hit_node = -1;
	int hit_edge = -1;

	// Clear previous selection
	for (uint32_t i = 0; i < state->current_graph.node_count; i++)
		state->current_graph.nodes[i].selected = 0.0f;
	for (uint32_t i = 0; i < state->current_graph.edge_count; i++)
		state->current_graph.edges[i].selected = 0.0f;

	for (uint32_t i = 0; i < state->current_graph.node_count; i++) {
		vec3 pos;
		glm_vec3_scale(state->current_graph.nodes[i].position, state->renderer.layoutScale, pos);
		float radius = state->current_graph.nodes[i].size * 0.5f;
		float t;
		if (picking_ray_sphere_intersection(state->camera.pos, state->camera.front, pos, radius, &t)) {
			if (t > 0 && t < min_t) {
				min_t = t;
				hit_node = i;
				hit_edge = -1;
			}
		}
	}
	for (uint32_t i = 0; i < state->current_graph.edge_count; i++) {
		vec3 p1, p2;
		glm_vec3_scale(state->current_graph.nodes[state->current_graph.edges[i].from].position, state->renderer.layoutScale, p1);
		glm_vec3_scale(state->current_graph.nodes[state->current_graph.edges[i].to].position, state->renderer.layoutScale, p2);
		float t, dist;
		dist = picking_dist_ray_segment(state->camera.pos, state->camera.front, p1, p2, &t);
		if (dist < 0.2f && t > 0 && t < min_t) {
			min_t = t;
			hit_node = -1;
			hit_edge = i;
		}
	}
	if (hit_node != -1) {
		state->current_graph.nodes[hit_node].selected = 1.0f;
		printf("%s Clicked Node %d: %s\n", is_double_click ? "Double" : "Single", hit_node, state->current_graph.nodes[hit_node].label ? state->current_graph.nodes[hit_node].label : "no label");
		state->last_picked_node = hit_node;
	} else if (hit_edge != -1) {
		state->current_graph.edges[hit_edge].selected = 1.0f;
		Edge *e = &state->current_graph.edges[hit_edge];
		printf("%s Clicked Edge %d: %d -> %d\n", is_double_click ? "Double" : "Single", hit_edge, e->from, e->to);
		printf("  selected=%.0f weight=%g\n", e->selected, e->weight);

		igraph_strvector_t enames;
		igraph_vector_int_t etypes;
		igraph_strvector_init(&enames, 0);
		igraph_vector_int_init(&etypes, 0);
		if (igraph_cattribute_list(&state->current_graph.g, NULL, NULL, NULL, NULL, &enames, &etypes) == IGRAPH_SUCCESS) {
			for (igraph_integer_t a = 0; a < igraph_strvector_size(&enames); a++) {
				const char *name = igraph_strvector_get(&enames, a);
				switch ((igraph_attribute_type_t)VECTOR(etypes)[a]) {
				case IGRAPH_ATTRIBUTE_NUMERIC:
					printf("  %s=%g\n", name, EAN(&state->current_graph.g, name, hit_edge));
					break;
				case IGRAPH_ATTRIBUTE_BOOLEAN:
					printf("  %s=%s\n", name, EAB(&state->current_graph.g, name, hit_edge) ? "true" : "false");
					break;
				case IGRAPH_ATTRIBUTE_STRING:
					printf("  %s=%s\n", name, EAS(&state->current_graph.g, name, hit_edge));
					break;
				default:
					break;
				}
			}
		}
		igraph_vector_int_destroy(&etypes);
		igraph_strvector_destroy(&enames);

		state->last_picked_node = -1; // Clear node selection
	} else {
		state->last_picked_node = -1;
	}

	state->renderer.needsAttributeUpload = VK_TRUE;
	renderer_update_graph(&state->renderer, &state->current_graph);
}

bool picking_ray_quad_intersection(vec3 ray_ori, vec3 ray_dir, vec3 quad_center, vec3 right, vec3 up, float width, float height, float *t_out)
{
	// 1. Ray-plane intersection
	// Normal of the billboard plane is cross(right, up) -> roughly -front
	vec3 normal;
	glm_vec3_cross(right, up, normal);
	glm_vec3_normalize(normal);

	float denom = glm_vec3_dot(normal, ray_dir);
	if (fabsf(denom) < 0.0001f)
		return false; // Parallel

	vec3 center_to_ori;
	glm_vec3_sub(quad_center, ray_ori, center_to_ori);
	float t = glm_vec3_dot(center_to_ori, normal) / denom;

	if (t < 0.0f)
		return false;

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
	// This means world_pos passed here IS already the center of the box.

	if (fabsf(u) <= width * 0.5f && fabsf(v) <= height * 0.5f) {
		*t_out = t;
		return true;
	}

	return false;
}
