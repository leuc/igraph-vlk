#include "layered_sphere.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// --- HILBERT CURVE LOGIC ---
void rot(int n, int *x, int *y, int rx, int ry) {
    if (ry == 0) {
        if (rx == 1) {
            *x = n - 1 - *x;
            *y = n - 1 - *y;
        }
        int t = *x;
        *x = *y;
        *y = t;
    }
}

int xy2d(int n, int x, int y) {
    int rx, ry, s, d = 0;
    for (s = n / 2; s > 0; s /= 2) {
        rx = (x & s) > 0;
        ry = (y & s) > 0;
        d += s * s * ((3 * rx) ^ ry);
        rot(s, &x, &y, rx, ry);
    }
    return d;
}

// --- SORTING STRUCTS ---
typedef struct { int id; double density; } NodeDensity;
int compare_nodes(const void *a, const void *b) {
    double diff = ((NodeDensity*)a)->density - ((NodeDensity*)b)->density;
    return (diff > 0) - (diff < 0);
}

typedef struct { double x, y, z; int hilbert_dist; } SpherePoint;
int compare_points(const void *a, const void *b) {
    return ((SpherePoint*)a)->hilbert_dist - ((SpherePoint*)b)->hilbert_dist;
}

// --- MATH HELPERS ---
double dist3d(double x1, double y1, double z1, double x2, double y2, double z2) {
    return sqrt((x1-x2)*(x1-x2) + (y1-y2)*(y1-y2) + (z1-z2)*(z1-z2));
}

double calculate_swap_delta(const igraph_t *ig, const igraph_matrix_t *layout, 
                            const igraph_vector_int_t *degrees, int u, int v, 
                            bool intra_only) {
    double old_energy = 0.0;
    double new_energy = 0.0;
    
    double ux = MATRIX(*layout, u, 0), uy = MATRIX(*layout, u, 1), uz = MATRIX(*layout, u, 2);
    double vx = MATRIX(*layout, v, 0), vy = MATRIX(*layout, v, 1), vz = MATRIX(*layout, v, 2);

    int u_deg = VECTOR(*degrees)[u]; // Degree of the sphere we are on

    igraph_vector_int_t u_edges, v_edges;
    igraph_vector_int_init(&u_edges, 0);
    igraph_vector_int_init(&v_edges, 0);
    
    igraph_incident(ig, &u_edges, u, IGRAPH_ALL);
    igraph_incident(ig, &v_edges, v, IGRAPH_ALL);

    // Calculate for U's neighbors
    for (int i = 0; i < igraph_vector_int_size(&u_edges); i++) {
        igraph_integer_t edge_id = VECTOR(u_edges)[i];
        igraph_integer_t from, to;
        igraph_edge(ig, edge_id, &from, &to);
        igraph_integer_t neighbor = (from == u) ? to : from;
        
        if (neighbor == v) continue;
        // If we only care about intra-sphere edges, ignore connections to other spheres
        if (intra_only && VECTOR(*degrees)[neighbor] != u_deg) continue;
        
        double nx = MATRIX(*layout, neighbor, 0), ny = MATRIX(*layout, neighbor, 1), nz = MATRIX(*layout, neighbor, 2);
        old_energy += dist3d(ux, uy, uz, nx, ny, nz);
        new_energy += dist3d(vx, vy, vz, nx, ny, nz);
    }

    // Calculate for V's neighbors
    for (int i = 0; i < igraph_vector_int_size(&v_edges); i++) {
        igraph_integer_t edge_id = VECTOR(v_edges)[i];
        igraph_integer_t from, to;
        igraph_edge(ig, edge_id, &from, &to);
        igraph_integer_t neighbor = (from == v) ? to : from;
        
        if (neighbor == u) continue;
        if (intra_only && VECTOR(*degrees)[neighbor] != u_deg) continue;
        
        double nx = MATRIX(*layout, neighbor, 0), ny = MATRIX(*layout, neighbor, 1), nz = MATRIX(*layout, neighbor, 2);
        old_energy += dist3d(vx, vy, vz, nx, ny, nz);
        new_energy += dist3d(ux, uy, uz, nx, ny, nz);
    }

    igraph_vector_int_destroy(&u_edges);
    igraph_vector_int_destroy(&v_edges);

    return new_energy - old_energy;
}
// --- MAIN LIFECYCLE ---
// Helper for sorting unique degrees descending (inside-out)
int compare_ints_desc(const void *a, const void *b) {
    return (*(int*)b) - (*(int*)a);
}

void layered_sphere_init(LayeredSphereContext* ctx, int node_count) {
    memset(ctx, 0, sizeof(LayeredSphereContext));
    ctx->initialized = true;
    ctx->phase = PHASE_INIT;
    igraph_vector_int_init(&ctx->degrees, node_count);
    igraph_vector_int_init(&ctx->unique_degrees, 0);
}

void layered_sphere_cleanup(LayeredSphereContext* ctx) {
    if (ctx->initialized) {
        igraph_vector_int_destroy(&ctx->degrees);
        igraph_vector_int_destroy(&ctx->unique_degrees);
    }
    ctx->initialized = false;
}

bool layered_sphere_step(LayeredSphereContext* ctx, GraphData* graph) {
    if (!ctx->initialized) return false;
    if (ctx->phase == PHASE_DONE) return false;
    
    igraph_t* ig = &graph->g; 
    int vcount = graph->node_count;
    bool swapped_this_frame = false;

    // ---------------------------------------------------------
    // PHASE 0: Init & Hilbert Placement
    // ---------------------------------------------------------
    if (ctx->phase == PHASE_INIT) {
        igraph_degree(ig, &ctx->degrees, igraph_vss_all(), IGRAPH_ALL, IGRAPH_LOOPS);
        
        // Extract unique degrees to process spheres one by one
        for (int i = 0; i < vcount; i++) {
            int deg = VECTOR(ctx->degrees)[i];
            if (!igraph_vector_int_contains(&ctx->unique_degrees, deg)) {
                igraph_vector_int_push_back(&ctx->unique_degrees, deg);
            }
        }
        // Sort descending so we go inside-out (highest degree = innermost)
        qsort(VECTOR(ctx->unique_degrees), igraph_vector_int_size(&ctx->unique_degrees), sizeof(igraph_integer_t), compare_ints_desc);

        // [ ... KEEP ALL YOUR EXISTING HILBERT PLACEMENT LOGIC HERE ... ]
        // (Calculate transitivity, allocate points, xy2d, map coordinates to MATRIX)
        
        ctx->phase = PHASE_INTRA_SPHERE;
        ctx->current_degree_idx = 0;
        return true; 
    }

    // ---------------------------------------------------------
    // PHASE 1: Intra-Sphere Optimization (Inside-Out)
    // ---------------------------------------------------------
    if (ctx->phase == PHASE_INTRA_SPHERE) {
        int target_deg = VECTOR(ctx->unique_degrees)[ctx->current_degree_idx];
        
        // Only loop over nodes that belong to the current sphere
        for (int u = 0; u < vcount; u++) {
            if (VECTOR(ctx->degrees)[u] != target_deg) continue;
            
            for (int v = u + 1; v < vcount; v++) {
                if (VECTOR(ctx->degrees)[v] != target_deg) continue;
                
                // true = intra_only. Ignore edges connecting to other spheres.
                double delta = calculate_swap_delta(ig, &graph->current_layout, &ctx->degrees, u, v, true);
                
                if (delta < -0.001) {
                    // Swap positions
                    for (int dim = 0; dim < 3; dim++) {
                        double temp = MATRIX(graph->current_layout, u, dim);
                        MATRIX(graph->current_layout, u, dim) = MATRIX(graph->current_layout, v, dim);
                        MATRIX(graph->current_layout, v, dim) = temp;
                        
                        graph->nodes[u].position[dim] = MATRIX(graph->current_layout, u, dim);
                        graph->nodes[v].position[dim] = MATRIX(graph->current_layout, v, dim);
                    }
                    swapped_this_frame = true;
                }
            }
        }

        // If we did a full sweep of this sphere and found no improvements, move outward
        if (!swapped_this_frame) {
            ctx->current_degree_idx++;
            // If we've processed all spheres, move to global alignment phase
            if (ctx->current_degree_idx >= igraph_vector_int_size(&ctx->unique_degrees)) {
                ctx->phase = PHASE_INTER_SPHERE;
            }
        }
        
        ctx->current_iter++;
        return true;
    }

    // ---------------------------------------------------------
    // PHASE 2: Inter-Sphere Optimization (Global Alignment)
    // ---------------------------------------------------------
    if (ctx->phase == PHASE_INTER_SPHERE) {
        for (int u = 0; u < vcount; u++) {
            for (int v = u + 1; v < vcount; v++) {
                // We STILL only swap nodes on the same sphere (don't break the rules!)
                if (VECTOR(ctx->degrees)[u] == VECTOR(ctx->degrees)[v]) {
                    
                    // false = intra_only. Now we calculate energy using ALL edges
                    // This aligns the locally-solved spheres with one another.
                    double delta = calculate_swap_delta(ig, &graph->current_layout, &ctx->degrees, u, v, false);
                    
                    if (delta < -0.001) {
                        for (int dim = 0; dim < 3; dim++) {
                            double temp = MATRIX(graph->current_layout, u, dim);
                            MATRIX(graph->current_layout, u, dim) = MATRIX(graph->current_layout, v, dim);
                            MATRIX(graph->current_layout, v, dim) = temp;
                            
                            graph->nodes[u].position[dim] = MATRIX(graph->current_layout, u, dim);
                            graph->nodes[v].position[dim] = MATRIX(graph->current_layout, v, dim);
                        }
                        swapped_this_frame = true;
                    }
                }
            }
        }

        ctx->current_iter++;
        
        // If no nodes on any sphere swapped to improve global layout, we are entirely done.
        if (!swapped_this_frame) {
            ctx->phase = PHASE_DONE;
            return false; 
        }
        
        return true;
    }

    return false;
}

const char* layered_sphere_get_stage_name(int stage_id) {
    return "Layered Sphere - Hilbert Swap Optimization";
}