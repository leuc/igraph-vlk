#ifndef RENDERER_GEOMETRY_H
#define RENDERER_GEOMETRY_H

#include "renderer.h"

typedef struct {
	vec3 pos;
	vec3 color;
	float size;
	float selected;
	// Animation specific fields
	float animation_progress;
	int animation_direction;
	int is_animating;	  // Use int for GLSL bool
	float normalized_pos; // 0.0 to 1.0 along the edge
} EdgeVertex;
typedef struct {
	vec3 pos;
	vec2 tex;
} LabelVertex;
typedef struct {
	vec3 nodePos;
	vec4 charRect;
	vec4 charUV;
} LabelInstance;
typedef struct {
	vec3 pos;
	vec2 tex;
} UIVertex;
typedef struct {
	vec2 screenPos;
	vec4 charRect;
	vec4 charUV;
	vec4 color;
} UIInstance;

#endif