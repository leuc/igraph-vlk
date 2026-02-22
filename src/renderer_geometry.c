#include "renderer_geometry.h"
#include "vulkan_utils.h"
#include "layered_sphere.h"
#include "text.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

extern FontAtlas globalAtlas;

void renderer_update_ui(Renderer* r, const char* text) {
    int len = strlen(text); if (len > 1024) len = 1024;
    UIInstance instances[1024]; 
    float xoff = -0.98f;
    float scale = 0.65f;
    for (int i = 0; i < len; i++) {
        unsigned char c = text[i]; CharInfo* ci = (c < 128) ? &globalAtlas.chars[c] : &globalAtlas.chars[32];
        instances[i].screenPos[0] = xoff; instances[i].screenPos[1] = 0.95f;
        instances[i].charRect[0] = ci->x0 * scale; instances[i].charRect[1] = ci->y0 * scale; 
        instances[i].charRect[2] = ci->x1 * scale; instances[i].charRect[3] = ci->y1 * scale;
        instances[i].charUV[0] = ci->u0; instances[i].charUV[1] = ci->v0; instances[i].charUV[2] = ci->u1; instances[i].charUV[3] = ci->v1;
        instances[i].color[0] = 1; instances[i].color[1] = 1; instances[i].color[2] = 1; instances[i].color[3] = 1;
        xoff += (ci->xadvance * scale) / 1720.0f; 
    }
    r->uiTextCharCount = len;
    updateBuffer(r->device, r->uiTextInstanceBufferMemory, sizeof(UIInstance)*len, instances);
}

void renderer_update_graph(Renderer* r, GraphData* graph) {
    vkDeviceWaitIdle(r->device);
    r->nodeCount = graph->node_count; r->edgeCount = graph->edge_count;
    if (r->instanceBuffer != VK_NULL_HANDLE) { 
        vkDestroyBuffer(r->device, r->instanceBuffer, NULL); vkFreeMemory(r->device, r->instanceBufferMemory, NULL); 
        vkDestroyBuffer(r->device, r->edgeVertexBuffer, NULL); vkFreeMemory(r->device, r->edgeVertexBufferMemory, NULL); 

        if (r->labelInstanceBuffer != VK_NULL_HANDLE) { vkDestroyBuffer(r->device, r->labelInstanceBuffer, NULL); vkFreeMemory(r->device, r->labelInstanceBufferMemory, NULL); r->labelInstanceBuffer = VK_NULL_HANDLE; }
        if (r->sphereVertexBuffer != VK_NULL_HANDLE) { vkDestroyBuffer(r->device, r->sphereVertexBuffer, NULL); vkFreeMemory(r->device, r->sphereVertexBufferMemory, NULL); r->sphereVertexBuffer = VK_NULL_HANDLE; }
        if (r->sphereIndexBuffer != VK_NULL_HANDLE) { vkDestroyBuffer(r->device, r->sphereIndexBuffer, NULL); vkFreeMemory(r->device, r->sphereIndexBufferMemory, NULL); r->sphereIndexBuffer = VK_NULL_HANDLE; }
        if (r->sphereIndexCounts) { free(r->sphereIndexCounts); r->sphereIndexCounts = NULL; }
        if (r->sphereIndexOffsets) { free(r->sphereIndexOffsets); r->sphereIndexOffsets = NULL; }
    }

    // Generate Sphere Backgrounds if Layered Layout
    if (graph->active_layout == LAYOUT_LAYERED_SPHERE && graph->layered_sphere && graph->layered_sphere->initialized) {
        LayeredSphereContext* ctx = graph->layered_sphere;
        r->numSpheres = ctx->num_spheres;
        r->sphereIndexCounts = malloc(sizeof(uint32_t) * r->numSpheres);
        r->sphereIndexOffsets = malloc(sizeof(uint32_t) * r->numSpheres);
        
        uint32_t totalVertices = 0;
        uint32_t totalIndices = 0;
        
        // Pass 1: Calculate sizes
        for (int s = 0; s < ctx->num_spheres; s++) {
            int target_faces = ctx->grids[s].max_slots;
            int rings = (int)sqrt(target_faces / 8.0);
            if (rings < 4) rings = 4;
            if (rings > 32) rings = 32; // FIX: Cap sphere resolution to save FPS
            int sectors = rings * 2;
            
            totalVertices += (rings + 1) * (sectors + 1);
            totalIndices += rings * sectors * 6;
        }
        
        // Create buffers
        createBuffer(r->device, r->physicalDevice, sizeof(Vertex)*totalVertices, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->sphereVertexBuffer, &r->sphereVertexBufferMemory);
        createBuffer(r->device, r->physicalDevice, sizeof(uint32_t)*totalIndices, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->sphereIndexBuffer, &r->sphereIndexBufferMemory);
        
        Vertex* allVerts = malloc(sizeof(Vertex) * totalVertices);
        uint32_t* allIndices = malloc(sizeof(uint32_t) * totalIndices);
        
        uint32_t vOffset = 0;
        uint32_t iOffset = 0;
        
        // Pass 2: Generate geometry
        for (int s = 0; s < ctx->num_spheres; s++) {
            double radius = 5.0; // Default
            if (ctx->grids[s].max_slots > 0) {
                double x = ctx->grids[s].slots[0].x;
                double y = ctx->grids[s].slots[0].y;
                double z = ctx->grids[s].slots[0].z;
                radius = sqrt(x*x + y*y + z*z);
            }
            float R = (float)radius * r->layoutScale;

            int target_faces = ctx->grids[s].max_slots;
            int rings = (int)sqrt(target_faces / 8.0);
            if (rings < 4) rings = 4;
            if (rings > 32) rings = 32; // FIX: Cap sphere resolution to save FPS
            int sectors = rings * 2;
            
            r->sphereIndexOffsets[s] = iOffset;
            uint32_t startIndex = vOffset;
            
            float const R_inv = 1.f / R;
            float const S = 1.f / (float)sectors;
            float const T = 1.f / (float)rings;

            for(int r_idx = 0; r_idx <= rings; r_idx++) {
                float const y = sin( -M_PI_2 + M_PI * r_idx * T );
                float const xz = cos( -M_PI_2 + M_PI * r_idx * T );
                
                for(int s_idx = 0; s_idx <= sectors; s_idx++) {
                    float const x = xz * cos(2*M_PI * s_idx * S);
                    float const z = xz * sin(2*M_PI * s_idx * S);
                    
                    vec3 n = {x, y, z};
                    vec3 p; glm_vec3_scale(n, R, p);
                    
                    memcpy(allVerts[vOffset].pos, p, 12);
                    memcpy(allVerts[vOffset].normal, n, 12); // Use normal for shading
                    allVerts[vOffset].texCoord[0] = s_idx*S;
                    allVerts[vOffset].texCoord[1] = r_idx*T;
                    vOffset++;
                }
            }
            
            uint32_t indexCount = 0;
            for(int r_idx = 0; r_idx < rings; r_idx++) {
                for(int s_idx = 0; s_idx < sectors; s_idx++) {
                    uint32_t curRow = startIndex + r_idx * (sectors + 1);
                    uint32_t nextRow = startIndex + (r_idx + 1) * (sectors + 1);
                    
                    allIndices[iOffset++] = curRow + s_idx;
                    allIndices[iOffset++] = nextRow + s_idx;
                    allIndices[iOffset++] = nextRow + (s_idx + 1);
                    
                    allIndices[iOffset++] = curRow + s_idx;
                    allIndices[iOffset++] = nextRow + (s_idx + 1);
                    allIndices[iOffset++] = curRow + (s_idx + 1);
                    
                    indexCount += 6;
                }
            }
            r->sphereIndexCounts[s] = indexCount;
        }
        
        updateBuffer(r->device, r->sphereVertexBufferMemory, sizeof(Vertex)*totalVertices, allVerts);
        updateBuffer(r->device, r->sphereIndexBufferMemory, sizeof(uint32_t)*totalIndices, allIndices);
        free(allVerts); free(allIndices);
    } else {
        r->numSpheres = 0;
    }

    createBuffer(r->device, r->physicalDevice, sizeof(Node)*r->nodeCount, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->instanceBuffer, &r->instanceBufferMemory);
    int segments = (graph->active_layout == LAYOUT_LAYERED_SPHERE) ? 20 : 1; r->edgeVertexCount = r->edgeCount * segments * 2;
    createBuffer(r->device, r->physicalDevice, sizeof(EdgeVertex)*r->edgeVertexCount, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->edgeVertexBuffer, &r->edgeVertexBufferMemory);
    Node* sorted = malloc(sizeof(Node) * graph->node_count); uint32_t currentOffset = 0;
    for (int t = 0; t < PLATONIC_COUNT; t++) {
        r->platonicDrawCalls[t].firstInstance = currentOffset; uint32_t count = 0;
        for (uint32_t i = 0; i < graph->node_count; i++) {
            int deg = graph->nodes[i].degree; PlatonicType pt;
            if (deg < 4) pt = PLATONIC_TETRAHEDRON; else if (deg < 6) pt = PLATONIC_CUBE; else if (deg < 8) pt = PLATONIC_OCTAHEDRON; else if (deg < 12) pt = PLATONIC_DODECAHEDRON; else pt = PLATONIC_ICOSAHEDRON;
            if (pt == (PlatonicType)t) { sorted[currentOffset + count] = graph->nodes[i]; glm_vec3_scale(sorted[currentOffset + count].position, r->layoutScale, sorted[currentOffset + count].position); if (sorted[currentOffset + count].size < 0.1f) sorted[currentOffset + count].size = 0.1f; count++; }
        }
        r->platonicDrawCalls[t].count = count; currentOffset += count;
    }
    updateBuffer(r->device, r->instanceBufferMemory, sizeof(Node)*graph->node_count, sorted);
    EdgeVertex* evs = malloc(sizeof(EdgeVertex)*r->edgeVertexCount);
    bool use_curves = (graph->active_layout == LAYOUT_LAYERED_SPHERE);
    uint32_t idx = 0;

    for(uint32_t i=0; i<graph->edge_count; i++) {
        vec3 p1, p2; 
        glm_vec3_scale(graph->nodes[graph->edges[i].from].position, r->layoutScale, p1); 
        glm_vec3_scale(graph->nodes[graph->edges[i].to].position, r->layoutScale, p2);

        if (use_curves) {
            float r1 = glm_vec3_norm(p1);
            float r2 = glm_vec3_norm(p2);
            
            if (r1 < 0.001f || r2 < 0.001f) {
                for(int s=0; s<segments; s++) {
                   float t1 = (float)s / segments; float t2 = (float)(s + 1) / segments;
                   vec3 v1, v2; glm_vec3_lerp(p1, p2, t1, v1); glm_vec3_lerp(p1, p2, t2, v2);
                   memcpy(evs[idx].pos, v1, 12); memcpy(evs[idx].color, graph->nodes[graph->edges[i].from].color, 12); evs[idx].size = graph->edges[i].size; idx++;
                   memcpy(evs[idx].pos, v2, 12); memcpy(evs[idx].color, graph->nodes[graph->edges[i].to].color, 12); evs[idx].size = graph->edges[i].size; idx++;
                }
                continue;
            }

            vec3 n1, n2;
            glm_vec3_divs(p1, r1, n1);
            glm_vec3_divs(p2, r2, n2);
            
            float dot = glm_vec3_dot(n1, n2);
            if (dot > 0.999f || dot < -0.999f) {
                for(int s=0; s<segments; s++) {
                   float t1 = (float)s / segments; float t2 = (float)(s + 1) / segments;
                   vec3 v1, v2; glm_vec3_lerp(p1, p2, t1, v1); glm_vec3_lerp(p1, p2, t2, v2);
                   memcpy(evs[idx].pos, v1, 12); memcpy(evs[idx].color, graph->nodes[graph->edges[i].from].color, 12); evs[idx].size = graph->edges[i].size; idx++;
                   memcpy(evs[idx].pos, v2, 12); memcpy(evs[idx].color, graph->nodes[graph->edges[i].to].color, 12); evs[idx].size = graph->edges[i].size; idx++;
                }
                continue;
            }
            
            float theta = acosf(dot);
            float sin_theta = sinf(theta);

            for (int s = 0; s < segments; s++) {
                float t1 = (float)s / segments;
                float t2 = (float)(s + 1) / segments;
                
                float w1_start = sinf((1.0f - t1) * theta) / sin_theta;
                float w2_start = sinf(t1 * theta) / sin_theta;
                vec3 dir1; glm_vec3_scale(n1, w1_start, dir1); glm_vec3_muladds(n2, w2_start, dir1);
                
                float w1_end = sinf((1.0f - t2) * theta) / sin_theta;
                float w2_end = sinf(t2 * theta) / sin_theta;
                vec3 dir2; glm_vec3_scale(n1, w1_end, dir2); glm_vec3_muladds(n2, w2_end, dir2);

                float rad1 = r1 * (1.0f - t1) + r2 * t1;
                float rad2 = r1 * (1.0f - t2) + r2 * t2;
                
                vec3 v1, v2;
                glm_vec3_scale(dir1, rad1, v1);
                glm_vec3_scale(dir2, rad2, v2);

                memcpy(evs[idx].pos, v1, 12); memcpy(evs[idx].color, graph->nodes[graph->edges[i].from].color, 12); evs[idx].size = graph->edges[i].size; idx++;
                memcpy(evs[idx].pos, v2, 12); memcpy(evs[idx].color, graph->nodes[graph->edges[i].to].color, 12); evs[idx].size = graph->edges[i].size; idx++;
            }
        } else {
            memcpy(evs[idx].pos, p1, 12); memcpy(evs[idx].color, graph->nodes[graph->edges[i].from].color, 12); evs[idx].size = graph->edges[i].size; idx++;
            memcpy(evs[idx].pos, p2, 12); memcpy(evs[idx].color, graph->nodes[graph->edges[i].to].color, 12); evs[idx].size = graph->edges[i].size; idx++;
        }
    }
    updateBuffer(r->device, r->edgeVertexBufferMemory, sizeof(EdgeVertex)*r->edgeVertexCount, evs); free(evs);
    uint32_t tc = 0; for(uint32_t i=0; i<r->nodeCount; i++) if(graph->nodes[i].label) tc += strlen(graph->nodes[i].label);
    r->labelCharCount = tc;
    if(tc > 0) {
        createBuffer(r->device, r->physicalDevice, sizeof(LabelInstance)*tc, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &r->labelInstanceBuffer, &r->labelInstanceBufferMemory);
        LabelInstance* li = malloc(sizeof(LabelInstance)*r->labelCharCount); uint32_t k = 0;
        for(uint32_t i=0; i<graph->node_count; i++) {
            if(!graph->nodes[i].label) continue;
            int len = strlen(graph->nodes[i].label); float xoff = 0; vec3 pos; glm_vec3_scale(graph->nodes[i].position, r->layoutScale, pos);
            for(int j=0; j<len; j++) {
                unsigned char c = graph->nodes[i].label[j]; CharInfo* ci = (c < 128) ? &globalAtlas.chars[c] : &globalAtlas.chars[32];
                memcpy(li[k].nodePos, pos, 12); li[k].nodePos[1] += (0.5f * graph->nodes[i].size) + 0.3f;
                li[k].charRect[0] = xoff + ci->x0; li[k].charRect[1] = ci->y0; li[k].charRect[2] = xoff + ci->x1; li[k].charRect[3] = ci->y1;
                li[k].charUV[0] = ci->u0; li[k].charUV[1] = ci->v0; li[k].charUV[2] = ci->u1; li[k].charUV[3] = ci->v1;
                xoff += ci->xadvance; k++;
            }
        }
        updateBuffer(r->device, r->labelInstanceBufferMemory, sizeof(LabelInstance)*r->labelCharCount, li); free(li);
    } else { r->labelInstanceBuffer = VK_NULL_HANDLE; }
    free(sorted);
}