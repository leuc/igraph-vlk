#include "sphere.h"
#include <math.h>
#include <stdlib.h>

void sphere_generate(float radius, int sectors, int stacks, Vertex** vertices, uint32_t* vertexCount, uint32_t** indices, uint32_t* indexCount) {
    float x, y, z, xy;                              // vertex position
    float nx, ny, nz, lengthInv = 1.0f / radius;    // vertex normal
    float s, t;                                     // vertex texCoord

    float sectorStep = 2 * M_PI / sectors;
    float stackStep = M_PI / stacks;
    float sectorAngle, stackAngle;

    *vertexCount = (sectors + 1) * (stacks + 1);
    *vertices = malloc(sizeof(Vertex) * (*vertexCount));

    int k = 0;
    for(int i = 0; i <= stacks; ++i) {
        stackAngle = M_PI / 2 - i * stackStep;        // starting from pi/2 to -pi/2
        xy = radius * cosf(stackAngle);             // r * cos(u)
        z = radius * sinf(stackAngle);              // r * sin(u)

        for(int j = 0; j <= sectors; ++j) {
            sectorAngle = j * sectorStep;           // starting from 0 to 2pi

            x = xy * cosf(sectorAngle);             // r * cos(u) * cos(v)
            y = xy * sinf(sectorAngle);             // r * cos(u) * sin(v)
            
            (*vertices)[k].pos[0] = x;
            (*vertices)[k].pos[1] = y;
            (*vertices)[k].pos[2] = z;

            (*vertices)[k].normal[0] = x * lengthInv;
            (*vertices)[k].normal[1] = y * lengthInv;
            (*vertices)[k].normal[2] = z * lengthInv;

            s = (float)j / sectors;
            t = (float)i / stacks;
            (*vertices)[k].texCoord[0] = s;
            (*vertices)[k].texCoord[1] = t;
            k++;
        }
    }

    *indexCount = sectors * stacks * 6;
    *indices = malloc(sizeof(uint32_t) * (*indexCount));
    int m = 0;
    for(int i = 0; i < stacks; ++i) {
        int k1 = i * (sectors + 1);     // beginning of current stack
        int k2 = k1 + sectors + 1;      // beginning of next stack

        for(int j = 0; j < sectors; ++j, ++k1, ++k2) {
            if(i != 0) {
                (*indices)[m++] = k1;
                (*indices)[m++] = k2;
                (*indices)[m++] = k1 + 1;
            }

            if(i != (stacks-1)) {
                (*indices)[m++] = k1 + 1;
                (*indices)[m++] = k2;
                (*indices)[m++] = k2 + 1;
            }
        }
    }
}
