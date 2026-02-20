#include "layered_sphere.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

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

// --- SORTING STRUCTS & COMPARATORS ---
typedef struct { 
    int id; 
    double density; 
} NodeDensity;

int compare_nodes(const void *a, const void *b) {
    double diff = ((NodeDensity*)a)->density - ((NodeDensity*)b)->density;
    return (diff > 0) - (diff < 0);
}

typedef struct { 
    double x, y, z; 
    int hilbert_dist; 
} SpherePoint;

int compare_points(const void *a, const void *b) {
    return ((SpherePoint*)a)->hilbert_dist - ((SpherePoint*)b)->hilbert_dist;
}

int compare_ints_desc(const void *a, const void *b) {
    igraph_integer_t val_a = *(const igraph_integer_t*)a;
    igraph_integer_t val_b = *(const igraph_integer_t*)b;
    return (val_b > val_a) - (val_b < val_a);
}

// --- MATH & ENERGY HELPERS ---
double dist3d(double x1, double y1, double z1, double x2, double y2, double z2) {
    return sqrt((x1-x2)*(x1-x2) + (y1-y2)*(y1-y2) + (z1-z2)*(z1-z2));
}

// Calculates the change in energy if u and v were to swap places.
double calculate_swap_delta(const igraph_t *ig, const igraph_matrix_t *layout, 
                            const igraph_vector_int_t *degrees, const bool* cut_edges,
                            int u, int v, bool intra_only, bool respect_cuts) {
    double old_energy = 0.0;
    double new_energy = 0.0;
    
    double ux = MATRIX(*layout, u, 0), uy = MATRIX(*layout, u, 1), uz = MATRIX(*layout, u, 2);
    double vx = MATRIX(*layout, v, 0), vy = MATRIX(*layout, v, 1), vz = MATRIX(*layout, v, 2);

    igraph_integer_t u_deg = VECTOR(*degrees)[u];

    igraph_vector_int_t u_edges, v_edges;
    igraph_vector_int_init(&u_edges, 0);
    igraph_vector_int_init(&v_edges, 0);
    
    igraph_incident(ig, &u_edges, u, IGRAPH_ALL);
    igraph_incident(ig, &v_edges, v, IGRAPH_ALL);

    // Calculate for U's neighbors
    for (int i = 0; i < igraph_vector_int_size(&u_edges); i++) {
        igraph_integer_t edge_id = VECTOR(u_edges)[i];
        
        if (respect_cuts && cut_edges && cut_edges[edge_id]) continue;
        
        igraph_integer_t from, to;
        igraph_edge(ig, edge_id, &from, &to);
        igraph_integer_t neighbor = (from == u) ? to : from;
        
        if (neighbor == v) continue;
        if (intra_only && VECTOR(*degrees)[neighbor] != u_deg) continue;
        
        double nx = MATRIX(*layout, neighbor, 0), ny = MATRIX(*layout, neighbor, 1), nz = MATRIX(*layout, neighbor, 2);
        old_energy += dist3d(ux, uy, uz, nx, ny, nz);
        new_energy += dist3d(vx, vy, vz, nx, ny, nz);
    }

    // Calculate for V's neighbors
    for (int i = 0; i < igraph_vector_int_size(&v_edges); i++) {
        igraph_integer_t edge_id = VECTOR(v_edges)[i];
        
        if (respect_cuts && cut_edges && cut_edges[edge_id]) continue;
        
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

void layered_sphere_init(LayeredSphereContext* ctx, int node_count) {
    memset(ctx, 0, sizeof(LayeredSphereContext));
    ctx->initialized = true;
    ctx->phase = PHASE_INIT;
    ctx->cut_edges = NULL;
    igraph_vector_int_init(&ctx->degrees, node_count);
    igraph_vector_int_init(&ctx->unique_degrees, 0);
}

void layered_sphere_cleanup(LayeredSphereContext* ctx) {
    if (ctx->initialized) {
        igraph_vector_int_destroy(&ctx->degrees);
        igraph_vector_int_destroy(&ctx->unique_degrees);
        if (ctx->cut_edges != NULL) {
            free(ctx->cut_edges);
            ctx->cut_edges = NULL;
        }
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
        // Upgrade: Using K-Coreness instead of raw degree for better internal spheres
        igraph_coreness(ig, &ctx->degrees, IGRAPH_ALL);
        
        for (int i = 0; i < vcount; i++) {
            igraph_integer_t deg = VECTOR(ctx->degrees)[i];
            if (!igraph_vector_int_contains(&ctx->unique_degrees, deg)) {
                igraph_vector_int_push_back(&ctx->unique_degrees, deg);
            }
        }
        
        qsort(VECTOR(ctx->unique_degrees), igraph_vector_int_size(&ctx->unique_degrees), 
              sizeof(igraph_integer_t), compare_ints_desc);

        // --- Edge Cutting (Jaccard Similarity) ---
        int ecount = igraph_ecount(ig);
        ctx->cut_edges = calloc(ecount, sizeof(bool));

        igraph_vector_int_t edge_pairs;
        igraph_vector_int_init(&edge_pairs, ecount * 2);
        for (int i = 0; i < ecount; i++) {
            igraph_integer_t u, v;
            igraph_edge(ig, i, &u, &v);
            VECTOR(edge_pairs)[i * 2] = u;
            VECTOR(edge_pairs)[i * 2 + 1] = v;
        }

        igraph_vector_t jaccard_sim;
        igraph_vector_init(&jaccard_sim, ecount);
        
        // Pass 0 (false) as the 5th argument to ignore self-loops
        igraph_similarity_jaccard_pairs(ig, &jaccard_sim, &edge_pairs, IGRAPH_ALL, 0); 

        for (int i = 0; i < ecount; i++) {
            igraph_integer_t u = VECTOR(edge_pairs)[i * 2];
            igraph_integer_t v = VECTOR(edge_pairs)[i * 2 + 1];

            // Flag intra-sphere weak ties for cutting
            if (VECTOR(ctx->degrees)[u] == VECTOR(ctx->degrees)[v]) {
                if (VECTOR(jaccard_sim)[i] < 0.05) { 
                    ctx->cut_edges[i] = true;
                }
            }
        }
        igraph_vector_int_destroy(&edge_pairs);
        igraph_vector_destroy(&jaccard_sim);
        // -----------------------------------------

        // Local Transitivity for Hilbert density sorting
        igraph_vector_t transitivity;
        igraph_vector_init(&transitivity, vcount);
        igraph_transitivity_local_undirected(ig, &transitivity, igraph_vss_all(), IGRAPH_TRANSITIVITY_ZERO);

        double base_radius = 5.0; 
        double layer_spacing = 10.0;
        int hilbert_resolution = 32768;
        int num_unique_degrees = igraph_vector_int_size(&ctx->unique_degrees);

        for (int d_idx = 0; d_idx < num_unique_degrees; d_idx++) {
            igraph_integer_t target_deg = VECTOR(ctx->unique_degrees)[d_idx];
            
            int n_in_group = 0;
            for (int i = 0; i < vcount; i++) {
                if (VECTOR(ctx->degrees)[i] == target_deg) n_in_group++;
            }
            if (n_in_group == 0) continue;

            NodeDensity *group_nodes = malloc(n_in_group * sizeof(NodeDensity));
            int idx = 0;
            for (int i = 0; i < vcount; i++) {
                if (VECTOR(ctx->degrees)[i] == target_deg) {
                    group_nodes[idx].id = i;
                    group_nodes[idx].density = VECTOR(transitivity)[i];
                    idx++;
                }
            }
            qsort(group_nodes, n_in_group, sizeof(NodeDensity), compare_nodes);

            SpherePoint *group_points = malloc(n_in_group * sizeof(SpherePoint));
            double radius = base_radius + (d_idx * layer_spacing);

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
                
                graph->nodes[node_id].position[0] = group_points[i].x;
                graph->nodes[node_id].position[1] = group_points[i].y;
                graph->nodes[node_id].position[2] = group_points[i].z;
            }

            free(group_nodes);
            free(group_points);
        }

        igraph_vector_destroy(&transitivity);
        
        ctx->phase = PHASE_INTRA_SPHERE;
        ctx->current_degree_idx = 0;
        return true; 
    }

    // ---------------------------------------------------------
    // PHASE 1: Intra-Sphere Optimization (Inside-Out)
    // ---------------------------------------------------------
    if (ctx->phase == PHASE_INTRA_SPHERE) {
        igraph_integer_t target_deg = VECTOR(ctx->unique_degrees)[ctx->current_degree_idx];
        
        for (int u = 0; u < vcount; u++) {
            if (VECTOR(ctx->degrees)[u] != target_deg) continue;
            
            for (int v = u + 1; v < vcount; v++) {
                if (VECTOR(ctx->degrees)[v] != target_deg) continue;
                
                // intra_only = true, respect_cuts = true
                double delta = calculate_swap_delta(ig, &graph->current_layout, &ctx->degrees, ctx->cut_edges, u, v, true, true);
                
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

        if (!swapped_this_frame) {
            ctx->current_degree_idx++;
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
                if (VECTOR(ctx->degrees)[u] == VECTOR(ctx->degrees)[v]) {
                    
                    // intra_only = false, respect_cuts = false ("un-cutting" ties)
                    double delta = calculate_swap_delta(ig, &graph->current_layout, &ctx->degrees, ctx->cut_edges, u, v, false, false);
                    
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
        
        if (!swapped_this_frame) {
            ctx->phase = PHASE_DONE;
            return false; 
        }
        
        return true;
    }

    return false;
}

const char* layered_sphere_get_stage_name(LayeredSphereContext* ctx) {
    static char stage_name[128];
    
    if (!ctx || !ctx->initialized) {
        return "Layered Sphere - Uninitialized";
    }

    switch (ctx->phase) {
        case PHASE_INIT:
            return "Layered Sphere - Phase 0: Hilbert Grid Placement";
            
        case PHASE_INTRA_SPHERE:
            if (ctx->unique_degrees.stor_begin != NULL) {
                int current_deg = VECTOR(ctx->unique_degrees)[ctx->current_degree_idx];
                snprintf(stage_name, sizeof(stage_name), 
                         "Layered Sphere - Phase 1: Intra-Sphere (K-Core %d)", current_deg);
                return stage_name;
            }
            return "Layered Sphere - Phase 1: Intra-Sphere Optimization";
            
        case PHASE_INTER_SPHERE:
            return "Layered Sphere - Phase 2: Global Inter-Sphere Alignment";
            
        case PHASE_DONE:
            return "Layered Sphere - Optimization Complete";
            
        default:
            return "Layered Sphere - Unknown Phase";
    }
}