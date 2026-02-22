#ifndef RENDERER_GEOMETRY_H
#define RENDERER_GEOMETRY_H

#include "renderer.h"

typedef struct { vec3 pos; vec3 color; float size; } EdgeVertex;
typedef struct { vec3 pos; vec2 tex; } LabelVertex;
typedef struct { vec3 nodePos; vec4 charRect; vec4 charUV; } LabelInstance;
typedef struct { vec3 pos; vec2 tex; } UIVertex;
typedef struct { vec2 screenPos; vec4 charRect; vec4 charUV; vec4 color; } UIInstance;

#endif