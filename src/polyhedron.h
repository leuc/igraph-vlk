#ifndef POLYHEDRON_H
#define POLYHEDRON_H

#include <cglm/cglm.h>
#include <vulkan/vulkan.h>

typedef struct {
	vec3 pos;
	vec3 normal;
	vec2 texCoord;
} Vertex;

typedef enum {
	PLATONIC_TETRAHEDRON = 0,
	PLATONIC_CUBE,
	PLATONIC_OCTAHEDRON,
	PLATONIC_DODECAHEDRON,
	PLATONIC_ICOSAHEDRON,
	PLATONIC_COUNT
} PlatonicType;

void polyhedron_generate_platonic(PlatonicType type, Vertex **vertices,
								  uint32_t *vertexCount, uint32_t **indices,
								  uint32_t *indexCount);

#endif
