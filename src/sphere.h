#ifndef SPHERE_H
#define SPHERE_H

#include <vulkan/vulkan.h>
#include <cglm/cglm.h>

typedef struct {
    vec3 pos;
    vec3 normal;
    vec2 texCoord;
} Vertex;

void sphere_generate(float radius, int sectors, int stacks, Vertex** vertices, uint32_t* vertexCount, uint32_t** indices, uint32_t* indexCount);

#endif
