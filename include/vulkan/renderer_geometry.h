#ifndef RENDERER_GEOMETRY_H
#define RENDERER_GEOMETRY_H

#include "vulkan/renderer.h"

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
} QuadVertex;  // Generic quad vertex for UI/menu instancing
typedef struct {
	vec3 pos;
	vec2 tex;
} LabelVertex;
typedef struct {
	vec3 nodePos;
	vec4 charRect;
	vec4 charUV;
	vec3 right;   // Fixed orientation vector for the menu plane
	vec3 up;      // Fixed orientation vector for the menu plane
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

// 3D Spherical Menu Instanced Rendering
typedef struct {
	vec3 worldPos;   // Cartesian world position (x, y, z)
	vec2 texCoord;   // Icon texture region (sprite sheet) base UV
	float texId;     // Texture ID / icon index
	vec3 scale;      // Non-uniform scale (x, y, z)
	vec4 rotation;   // Quaternion rotation (w, x, y, z) - optional
    float hovered;   // 1.0 for hovered, 0.0 otherwise
} MenuInstance;

#endif
