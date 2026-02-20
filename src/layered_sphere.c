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

// Calculates the change in energy if u and v were to swap places.
// This is critical for maintaining 60fps+ rendering.
double calculate_swap_delta(const igraph_t *ig, const igraph_matrix_t *layout, int u, int v) {
    double old_energy = 0.0;
    double new_energy = 0.0;
    
    // Get coordinates
    double ux = MATRIX(*layout, u, 0), uy = MATRIX(*layout, u, 1), uz = MATRIX(*layout, u, 2);
    double vx = MATRIX(*layout, v, 0), vy = MATRIX(*layout, v, 1), vz = MATRIX(*layout, v, 2);

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


        
        if (neighbor == v) continue; // Distance between u and v doesn't change on swap
        
        double nx = MATRIX(*layout, neighbor, 0);
        double ny = MATRIX(*layout, neighbor, 1);
        double nz = MATRIX(*layout, neighbor, 2);
        
        old_energy += dist3d(ux, uy, uz, nx, ny, nz);
        new_energy += dist3d(vx, vy, vz, nx, ny, nz); // U takes V's position
    }

    // Calculate for V's neighbors
    for (int i = 0; i < igraph_vector_int_size(&v_edges); i++) {
        igraph_integer_t edge_id = VECTOR(u_edges)[i];
        igraph_integer_t from, to;
        igraph_edge(ig, edge_id, &from, &to);
        igraph_integer_t neighbor = (from == u) ? to : from;
        
        if (neighbor == u) continue;
        
        double nx = MATRIX(*layout, neighbor, 0);
        double ny = MATRIX(*layout, neighbor, 1);
        double nz = MATRIX(*layout, neighbor, 2);
        
        old_energy += dist3d(vx, vy, vz, nx, ny, nz);
        new_energy += dist3d(ux, uy, uz, nx, ny, nz); // V takes U's position
    }

    igraph_vector_int_destroy(&u_edges);
    igraph_vector_int_destroy(&v_edges);

    return new_energy - old_energy;
}

// --- MAIN LIFECYCLE ---

void layered_sphere_init(LayeredSphereContext* ctx, int node_count) {
    memset(ctx, 0, sizeof(LayeredSphereContext));
    ctx->initialized = true;
    ctx->first_pass_done = false;
    igraph_vector_int_init(&ctx->degrees, node_count);
}

void layered_sphere_cleanup(LayeredSphereContext* ctx) {
    if (ctx->initialized) {
        igraph_vector_int_destroy(&ctx->degrees);
    }
    ctx->initialized = false;
}

bool layered_sphere_step(LayeredSphereContext* ctx, GraphData* graph) {
    if (!ctx->initialized) return false;
    
    // We assume graph->igraph points to the actual igraph_t object
    igraph_t* ig = &graph->g;
    int vcount = graph->node_count;

    // ---------------------------------------------------------
    // STATE 0: First Frame - Initial Placement (Hilbert/Density)
    // ---------------------------------------------------------
    if (!ctx->first_pass_done) {
        igraph_degree(ig, &ctx->degrees, igraph_vss_all(), IGRAPH_ALL, IGRAPH_LOOPS);
        int max_degree = igraph_vector_int_max(&ctx->degrees);

        igraph_vector_t transitivity;
        igraph_vector_init(&transitivity, vcount);
        igraph_transitivity_local_undirected(ig, &transitivity, igraph_vss_all(), IGRAPH_TRANSITIVITY_ZERO);

        double base_radius = 5.0; 
        double layer_spacing = 10.0;
        int hilbert_resolution = 32768;

        for (int deg = 0; deg <= max_degree; deg++) {
            int n_in_group = 0;
            for (int i = 0; i < vcount; i++) {
                if (VECTOR(ctx->degrees)[i] == deg) n_in_group++;
            }
            if (n_in_group == 0) continue;

            NodeDensity *group_nodes = malloc(n_in_group * sizeof(NodeDensity));
            int idx = 0;
            for (int i = 0; i < vcount; i++) {
                if (VECTOR(ctx->degrees)[i] == deg) {
                    group_nodes[idx].id = i;
                    group_nodes[idx].density = VECTOR(transitivity)[i];
                    idx++;
                }
            }
            qsort(group_nodes, n_in_group, sizeof(NodeDensity), compare_nodes);

            SpherePoint *group_points = malloc(n_in_group * sizeof(SpherePoint));
            double radius = base_radius + (max_degree - deg) * layer_spacing;

            for (int i = 0; i < n_in_group; i++) {
                double phi = acos(1.0 - 2.0 * (i + 0.5) / n_in_group);
                double theta = M_PI * (1.0 + sqrt(5.0)) * i;
                
                group_points[i].x = radius * cos(theta) * sin(phi);
                group_points[i].y = radius * sin(theta) * sin(phi);
                group_points[i].z = radius * cos(phi);

                double norm_x = fmod(theta, 2 * M_PI) / (2 * M_PI);
                if (norm_x < 0) norm_x += 1.0;
                double norm_y = phi / M_PI;

                int h_x = (int)(norm_x * (hilbert_resolution - 1));
                int h_y = (int)(norm_y * (hilbert_resolution - 1));
                group_points[i].hilbert_dist = xy2d(hilbert_resolution, h_x, h_y);
            }
            
            qsort(group_points, n_in_group, sizeof(SpherePoint), compare_points);

            for (int i = 0; i < n_in_group; i++) {
                int node_id = group_nodes[i].id;
                MATRIX(graph->current_layout, node_id, 0) = group_points[i].x;
                MATRIX(graph->current_layout, node_id, 1) = group_points[i].y;
                MATRIX(graph->current_layout, node_id, 2) = group_points[i].z;
            }

            free(group_nodes);
            free(group_points);
        }

        igraph_vector_destroy(&transitivity);
        
        // Sync to render nodes
        for (int i = 0; i < vcount; i++) {
            graph->nodes[i].position[0] = MATRIX(graph->current_layout, i, 0);
            graph->nodes[i].position[1] = MATRIX(graph->current_layout, i, 1);
            graph->nodes[i].position[2] = MATRIX(graph->current_layout, i, 2);
        }

        ctx->first_pass_done = true;
        ctx->current_iter = 0;
        
        // Return true so UI renders the initial state before swapping begins
        return true; 
    }

    // ---------------------------------------------------------
    // STATE 1: Subsequent Frames - One Sweep of Swapping
    // ---------------------------------------------------------
    bool improved_this_frame = false;

    for (int u = 0; u < vcount; u++) {
        for (int v = u + 1; v < vcount; v++) {
            if (VECTOR(ctx->degrees)[u] == VECTOR(ctx->degrees)[v]) {
                
                // Check if swap improves energy
                double delta = calculate_swap_delta(ig, &graph->current_layout, u, v);
                
                if (delta < -0.001) {
                    // Perform Swap in layout matrix
                    double temp_x = MATRIX(graph->current_layout, u, 0);
                    double temp_y = MATRIX(graph->current_layout, u, 1);
                    double temp_z = MATRIX(graph->current_layout, u, 2);
                    
                    MATRIX(graph->current_layout, u, 0) = MATRIX(graph->current_layout, v, 0);
                    MATRIX(graph->current_layout, u, 1) = MATRIX(graph->current_layout, v, 1);
                    MATRIX(graph->current_layout, u, 2) = MATRIX(graph->current_layout, v, 2);
                    
                    MATRIX(graph->current_layout, v, 0) = temp_x;
                    MATRIX(graph->current_layout, v, 1) = temp_y;
                    MATRIX(graph->current_layout, v, 2) = temp_z;

                    // Sync immediately to render nodes so the UI updates
                    graph->nodes[u].position[0] = MATRIX(graph->current_layout, u, 0);
                    graph->nodes[u].position[1] = MATRIX(graph->current_layout, u, 1);
                    graph->nodes[u].position[2] = MATRIX(graph->current_layout, u, 2);
                    
                    graph->nodes[v].position[0] = MATRIX(graph->current_layout, v, 0);
                    graph->nodes[v].position[1] = MATRIX(graph->current_layout, v, 1);
                    graph->nodes[v].position[2] = MATRIX(graph->current_layout, v, 2);

                    improved_this_frame = true;
                }
            }
        }
    }

    ctx->current_iter++;
    ctx->total_iters++;

    // If a full sweep resulted in 0 improvements, the layout is finalized.
    // Returning false tells the rendering loop the algorithm is done.
    return improved_this_frame;
}

const char* layered_sphere_get_stage_name(int stage_id) {
    return "Layered Sphere - Hilbert Swap Optimization";
}