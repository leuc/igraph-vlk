#include "polyhedron.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static void add_tri(Vertex* vs, uint32_t* is, int* v, vec3 p1, vec3 p2, vec3 p3) {
    vec3 e1, e2, n;
    glm_vec3_sub(p2, p1, e1);
    glm_vec3_sub(p3, p1, e2);
    glm_vec3_cross(e1, e2, n);
    glm_vec3_normalize(n);
    for(int i=0; i<3; i++) {
        memcpy(vs[*v+i].normal, n, 12);
        vs[*v+i].texCoord[0] = 0; vs[*v+i].texCoord[1] = 0;
    }
    memcpy(vs[*v+0].pos, p1, 12);
    memcpy(vs[*v+1].pos, p2, 12);
    memcpy(vs[*v+2].pos, p3, 12);
    is[*v+0] = *v+0; is[*v+1] = *v+1; is[*v+2] = *v+2;
    *v += 3;
}

void polyhedron_generate_platonic(PlatonicType type, Vertex** vertices, uint32_t* vertexCount, uint32_t** indices, uint32_t* indexCount) {
    float phi = (1.0f + sqrtf(5.0f)) / 2.0f;
    switch(type) {
        case PLATONIC_TETRAHEDRON: {
            *vertexCount = 12; *indexCount = 12;
            *vertices = calloc(*vertexCount, sizeof(Vertex)); *indices = malloc(12 * 4);
            int v=0;
            vec3 pts[4] = {{1,1,1}, {-1,-1,1}, {-1,1,-1}, {1,-1,-1}};
            add_tri(*vertices, *indices, &v, pts[0], pts[1], pts[2]);
            add_tri(*vertices, *indices, &v, pts[0], pts[3], pts[1]);
            add_tri(*vertices, *indices, &v, pts[0], pts[2], pts[3]);
            add_tri(*vertices, *indices, &v, pts[1], pts[3], pts[2]);
            break;
        }
        case PLATONIC_CUBE: {
            *vertexCount = 36; *indexCount = 36;
            *vertices = calloc(*vertexCount, sizeof(Vertex)); *indices = malloc(36 * 4);
            int v=0;
            vec3 p[8] = {{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}};
            int f[6][4] = {{0,1,2,3},{5,4,7,6},{4,0,3,7},{1,5,6,2},{4,5,1,0},{3,2,6,7}};
            for(int i=0; i<6; i++) {
                add_tri(*vertices, *indices, &v, p[f[i][0]], p[f[i][1]], p[f[i][2]]);
                add_tri(*vertices, *indices, &v, p[f[i][0]], p[f[i][2]], p[f[i][3]]);
            }
            break;
        }
        case PLATONIC_OCTAHEDRON: {
            *vertexCount = 24; *indexCount = 24;
            *vertices = calloc(*vertexCount, sizeof(Vertex)); *indices = malloc(24 * 4);
            int v=0;
            vec3 p[6] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
            int f[8][3] = {{0,2,4},{0,4,3},{0,3,5},{0,5,2},{1,2,5},{1,5,3},{1,3,4},{1,4,2}};
            for(int i=0; i<8; i++) add_tri(*vertices, *indices, &v, p[f[i][0]], p[f[i][1]], p[f[i][2]]);
            break;
        }
        case PLATONIC_ICOSAHEDRON: {
            *vertexCount = 60; *indexCount = 60;
            *vertices = calloc(*vertexCount, sizeof(Vertex)); *indices = malloc(60 * 4);
            int v=0;
            vec3 p[12] = {{0,1,phi},{0,1,-phi},{0,-1,phi},{0,-1,-phi},{1,phi,0},{1,-phi,0},{-1,phi,0},{-1,-phi,0},{phi,0,1},{-phi,0,1},{phi,0,-1},{-phi,0,-1}};
            int f[20][3] = {{0,8,4},{0,4,6},{0,6,9},{0,9,2},{0,2,8},{1,4,10},{1,10,11},{1,11,7},{1,7,6},{1,6,4},{2,9,7},{2,7,5},{2,5,8},{3,10,5},{3,5,7},{3,7,11},{3,11,10},{3,10,5},{4,8,10},{5,10,8},{6,7,9},{11,9,7},{3,5,8},{3,8,10}};
            // For simplicity in bootstrap, using standard face list
            int idx[20][3] = {{0,4,1},{0,9,4},{9,5,4},{4,5,8},{4,8,1},{8,10,1},{8,3,10},{5,3,8},{5,2,3},{2,7,3},{7,10,3},{7,6,10},{7,11,6},{11,0,6},{0,1,6},{6,1,10},{9,0,11},{9,11,2},{9,2,5},{7,2,11}};
            for(int i=0; i<20; i++) add_tri(*vertices, *indices, &v, p[idx[i][0]], p[idx[i][1]], p[idx[i][2]]);
            break;
        }
        case PLATONIC_DODECAHEDRON: {
            *vertexCount = 108; *indexCount = 108; // 12 faces * 3 triangles/face * 3 verts
            *vertices = calloc(*vertexCount, sizeof(Vertex)); *indices = malloc(108 * 4);
            int v=0;
            vec3 p[20] = {{1,1,1},{1,1,-1},{1,-1,1},{1,-1,-1},{-1,1,1},{-1,1,-1},{-1,-1,1},{-1,-1,-1},{0,1/phi,phi},{0,1/phi,-phi},{0,-1/phi,phi},{0,-1/phi,-phi},{1/phi,phi,0},{1/phi,-phi,0},{-1/phi,phi,0},{-1/phi,-phi,0},{phi,0,1/phi},{phi,0,-1/phi},{-phi,0,1/phi},{-phi,0,-1/phi}};
            int f[12][5] = {{0,16,2,10,8},{0,8,4,14,12},{16,17,1,12,0},{1,9,11,3,17},{1,12,14,5,9},{2,13,15,6,10},{13,3,11,7,15},{3,17,16,2,13},{4,8,10,6,18},{14,5,19,18,4},{5,19,7,11,9},{15,7,19,18,6}};
            for(int i=0; i<12; i++) {
                add_tri(*vertices, *indices, &v, p[f[i][0]], p[f[i][1]], p[f[i][2]]);
                add_tri(*vertices, *indices, &v, p[f[i][0]], p[f[i][2]], p[f[i][3]]);
                add_tri(*vertices, *indices, &v, p[f[i][0]], p[f[i][3]], p[f[i][4]]);
            }
            break;
        }
        default: break;
    }
}
