#include "layered_sphere.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <omp.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// --- FAST THREAD-SAFE PRNG FOR OPENMP ---
// Standard rand() causes lock contention in OpenMP. This lock-free hash 
// generates a clean float between 0.0 and 1.0 based on node IDs and the iteration.
static inline double fast_rand_float(unsigned int u, unsigned int v, unsigned int iter) {
    unsigned int hash = u;
    hash = ((hash << 16) ^ hash) * 0x45d9f3b;
    hash = ((hash >> 16) ^ hash) ^ v;
    hash = ((hash << 16) ^ hash) * 0x45d9f3b;
    hash = ((hash >> 16) ^ hash) ^ iter;
    hash = ((hash << 16) ^ hash) * 0x45d9f3b;
    hash = (hash >> 16) ^ hash;
    return (double)(hash % 10000) / 10000.0;
}

// --- HILBERT CURVE LOGIC ---
void rot(int n, int *x, int *y, int rx, int ry) {
    if (ry == 0) {
        if (rx == 1) { *x = n - 1 - *x; *y = n - 1 - *y; }
        int t = *x; *x = *y; *y = t;
    }
}

int xy2d(int n, int x, int y) {
    int rx, ry, s, d = 0;
    for (s = n / 2; s > 0; s /= 2) {
        rx = (x & s) > 0; ry = (y & s) > 0;
        d += s * s * ((3 * rx) ^ ry);
        rot(s, &x, &y, rx, ry);
    }
    return d;
}

// --- STRUCTS & COMPARATORS ---
typedef struct { int id; double density; } NodeDensity;
int compare_nodes_density(const void *a, const void *b) {
    double diff = ((NodeDensity*)a)->density - ((NodeDensity*)b)->density;
    return (diff > 0) - (diff < 0);
}

typedef struct { double x, y, z; int hilbert_dist; } SpherePoint;
int compare_points(const void *a, const void *b) {
    return ((SpherePoint*)a)->hilbert_dist - ((SpherePoint*)b)->hilbert_dist;
}

typedef struct { int id; double composite_score; } NodeComposite;
int compare_composite_desc(const void *a, const void *b) {
    double diff = ((NodeComposite*)b)->composite_score - ((NodeComposite*)a)->composite_score;
    return (diff > 0) - (diff < 0);
}

// --- MATH & ENERGY HELPERS ---
double dist3d(double x1, double y1, double z1, double x2, double y2, double z2) {
    return sqrt((x1-x2)*(x1-x2) + (y1-y2)*(y1-y2) + (z1-z2)*(z1-z2));
}

double get_vector_max(const igraph_vector_t* v) {
    double max_val = 0;
    for (int i = 0; i < igraph_vector_size(v); i++) {
        if (VECTOR(*v)[i] > max_val && !isinf(VECTOR(*v)[i])) max_val = VECTOR(*v)[i];
    }
    return max_val == 0 ? 1.0 : max_val;
}

double get_vector_int_max(const igraph_vector_int_t* v) {
    int max_val = 0;
    for (int i = 0; i < igraph_vector_int_size(v); i++) {
        if (VECTOR(*v)[i] > max_val) max_val = VECTOR(*v)[i];
    }
    return max_val == 0 ? 1.0 : max_val;
}

// target_sphere_1 == -1 means evaluate ALL connections globally
double calculate_swap_delta(const igraph_t *ig, const igraph_matrix_t *layout, 
                            const igraph_vector_int_t *sphere_ids, const bool* cut_edges,
                            int u, int v, int target_sphere_1, int target_sphere_2) {
    double old_energy = 0.0, new_energy = 0.0;
    double ux = MATRIX(*layout, u, 0), uy = MATRIX(*layout, u, 1), uz = MATRIX(*layout, u, 2);
    double vx = MATRIX(*layout, v, 0), vy = MATRIX(*layout, v, 1), vz = MATRIX(*layout, v, 2);

    igraph_vector_int_t u_edges, v_edges;
    igraph_vector_int_init(&u_edges, 0);
    igraph_vector_int_init(&v_edges, 0);
    
    igraph_incident(ig, &u_edges, u, IGRAPH_ALL);
    igraph_incident(ig, &v_edges, v, IGRAPH_ALL);

    for (int i = 0; i < igraph_vector_int_size(&u_edges); i++) {
        igraph_integer_t edge_id = VECTOR(u_edges)[i];
        if (cut_edges && cut_edges[edge_id]) continue;
        
        igraph_integer_t from, to;
        igraph_edge(ig, edge_id, &from, &to);
        igraph_integer_t neighbor = (from == u) ? to : from;
        
        if (neighbor == v) continue;
        
        if (target_sphere_1 != -1) {
            int n_sphere = VECTOR(*sphere_ids)[neighbor];
            if (n_sphere != target_sphere_1 && n_sphere != target_sphere_2) continue;
        }
        
        double nx = MATRIX(*layout, neighbor, 0), ny = MATRIX(*layout, neighbor, 1), nz = MATRIX(*layout, neighbor, 2);
        old_energy += dist3d(ux, uy, uz, nx, ny, nz);
        new_energy += dist3d(vx, vy, vz, nx, ny, nz);
    }

    for (int i = 0; i < igraph_vector_int_size(&v_edges); i++) {
        igraph_integer_t edge_id = VECTOR(v_edges)[i];
        if (cut_edges && cut_edges[edge_id]) continue;
        
        igraph_integer_t from, to;
        igraph_edge(ig, edge_id, &from, &to);
        igraph_integer_t neighbor = (from == v) ? to : from;
        
        if (neighbor == u) continue;
        
        if (target_sphere_1 != -1) {
            int n_sphere = VECTOR(*sphere_ids)[neighbor];
            if (n_sphere != target_sphere_1 && n_sphere != target_sphere_2) continue;
        }
        
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
    igraph_vector_int_init(&ctx->sphere_ids, node_count);
}

void layered_sphere_cleanup(LayeredSphereContext* ctx) {
    if (ctx->initialized) {
        igraph_vector_int_destroy(&ctx->sphere_ids);
        if (ctx->cut_edges) { free(ctx->cut_edges); ctx->cut_edges = NULL; }
    }
    ctx->initialized = false;
}

bool layered_sphere_step(LayeredSphereContext* ctx, GraphData* graph) {
    if (!ctx->initialized || ctx->phase == PHASE_DONE) return false;
    
    igraph_t* ig = &graph->g; 
    int vcount = graph->node_count;
    int ecount = igraph_ecount(ig);

    // ---------------------------------------------------------
    // PHASE 0: Init
    // ---------------------------------------------------------
    if (ctx->phase == PHASE_INIT) {
        printf("[DEBUG] PHASE 0: Starting initialization for %d nodes and %d edges...\n", vcount, ecount);
        
        ctx->num_spheres = (int)fmax(3.0, sqrt(vcount) * 0.5);
        printf("[DEBUG] Dynamically calculated target spheres: %d\n", ctx->num_spheres);

        igraph_vector_int_t coreness; igraph_vector_int_init(&coreness, vcount);
        igraph_coreness(ig, &coreness, IGRAPH_ALL);

        igraph_vector_t betweenness; igraph_vector_init(&betweenness, vcount);
        igraph_betweenness(ig, &betweenness, igraph_vss_all(), IGRAPH_UNDIRECTED, NULL);

        igraph_vector_t eccentricity; igraph_vector_init(&eccentricity, vcount);
        igraph_eccentricity(ig, &eccentricity, igraph_vss_all(), IGRAPH_ALL);

        double max_c = get_vector_int_max(&coreness);
        double max_b = get_vector_max(&betweenness);
        double max_e = get_vector_max(&eccentricity);

        NodeComposite *composite_nodes = malloc(vcount * sizeof(NodeComposite));
        for (int i = 0; i < vcount; i++) {
            double c_norm = VECTOR(coreness)[i] / max_c;
            double b_norm = VECTOR(betweenness)[i] / max_b;
            double e_norm = isinf(VECTOR(eccentricity)[i]) ? 1.0 : (VECTOR(eccentricity)[i] / max_e);
            composite_nodes[i].id = i;
            composite_nodes[i].composite_score = c_norm + (b_norm * 0.5) - e_norm;
        }

        qsort(composite_nodes, vcount, sizeof(NodeComposite), compare_composite_desc);

        int nodes_per_sphere = (int)ceil((double)vcount / ctx->num_spheres);
        for (int i = 0; i < vcount; i++) {
            int assigned_sphere = i / nodes_per_sphere;
            if (assigned_sphere >= ctx->num_spheres) assigned_sphere = ctx->num_spheres - 1;
            VECTOR(ctx->sphere_ids)[composite_nodes[i].id] = assigned_sphere;
        }
        free(composite_nodes);

        ctx->cut_edges = calloc(ecount, sizeof(bool));
        igraph_vector_int_t edge_pairs; igraph_vector_int_init(&edge_pairs, ecount * 2);
        for (int i = 0; i < ecount; i++) {
            igraph_integer_t u, v; igraph_edge(ig, i, &u, &v);
            VECTOR(edge_pairs)[i*2] = u; VECTOR(edge_pairs)[i*2+1] = v;
        }
        igraph_vector_t jaccard_sim; igraph_vector_init(&jaccard_sim, ecount);
        igraph_similarity_jaccard_pairs(ig, &jaccard_sim, &edge_pairs, IGRAPH_ALL, 0); 

        int cut_count = 0;
        for (int i = 0; i < ecount; i++) {
            igraph_integer_t u = VECTOR(edge_pairs)[i*2], v = VECTOR(edge_pairs)[i*2+1];
            if (VECTOR(ctx->sphere_ids)[u] == VECTOR(ctx->sphere_ids)[v]) {
                if (VECTOR(jaccard_sim)[i] < 0.05) {
                    ctx->cut_edges[i] = true;
                    cut_count++;
                }
            }
        }
        printf("[DEBUG] Cut %d weak intra-sphere ties based on Jaccard similarity.\n", cut_count);
        
        igraph_vector_int_destroy(&edge_pairs); igraph_vector_destroy(&jaccard_sim);

        igraph_vector_t transitivity; igraph_vector_init(&transitivity, vcount);
        igraph_transitivity_local_undirected(ig, &transitivity, igraph_vss_all(), IGRAPH_TRANSITIVITY_ZERO);

        double base_radius = 5.0; 
        double layer_spacing = 15.0; 
        int hilbert_res = 32768;

        #pragma omp parallel for schedule(dynamic)
        for (int s = 0; s < ctx->num_spheres; s++) {
            int n_in_group = 0;
            for (int i = 0; i < vcount; i++) if (VECTOR(ctx->sphere_ids)[i] == s) n_in_group++;
            if (n_in_group == 0) continue;

            NodeDensity *group_nodes = malloc(n_in_group * sizeof(NodeDensity));
            int idx = 0;
            for (int i = 0; i < vcount; i++) {
                if (VECTOR(ctx->sphere_ids)[i] == s) {
                    group_nodes[idx].id = i;
                    group_nodes[idx].density = VECTOR(transitivity)[i];
                    idx++;
                }
            }
            qsort(group_nodes, n_in_group, sizeof(NodeDensity), compare_nodes_density);

            SpherePoint *group_pts = malloc(n_in_group * sizeof(SpherePoint));
            double radius = base_radius + (s * layer_spacing);

            for (int i = 0; i < n_in_group; i++) {
                double phi = acos(1.0 - 2.0 * (i + 0.5) / n_in_group);
                double theta = M_PI * (1.0 + sqrt(5.0)) * i;
                group_pts[i].x = radius * cos(theta) * sin(phi);
                group_pts[i].y = radius * sin(theta) * sin(phi);
                group_pts[i].z = radius * cos(phi);

                double norm_x = fmod(theta, 2 * M_PI) / (2 * M_PI);
                if (norm_x < 0) norm_x += 1.0;
                group_pts[i].hilbert_dist = xy2d(hilbert_res, (int)(norm_x*(hilbert_res-1)), (int)((phi/M_PI)*(hilbert_res-1)));
            }
            qsort(group_pts, n_in_group, sizeof(SpherePoint), compare_points);

            for (int i = 0; i < n_in_group; i++) {
                int node_id = group_nodes[i].id;
                MATRIX(graph->current_layout, node_id, 0) = group_pts[i].x;
                MATRIX(graph->current_layout, node_id, 1) = group_pts[i].y;
                MATRIX(graph->current_layout, node_id, 2) = group_pts[i].z;
            }
            free(group_nodes); free(group_pts);
        }

        for (int i=0; i<vcount; i++) {
            graph->nodes[i].position[0] = MATRIX(graph->current_layout, i, 0);
            graph->nodes[i].position[1] = MATRIX(graph->current_layout, i, 1);
            graph->nodes[i].position[2] = MATRIX(graph->current_layout, i, 2);
        }

        igraph_vector_int_destroy(&coreness); igraph_vector_destroy(&betweenness); 
        igraph_vector_destroy(&eccentricity); igraph_vector_destroy(&transitivity);
        
        ctx->phase = PHASE_INTRA_SPHERE;
        ctx->phase_iter = 0;
        printf("[DEBUG] PHASE 0 COMPLETE. Moving to Phase 1.\n");
        return true; 
    }

    // ---------------------------------------------------------
    // PHASE 1: Parallel Intra-Sphere (With Simulated Annealing)
    // ---------------------------------------------------------
    if (ctx->phase == PHASE_INTRA_SPHERE) {
        int local_swaps = 0;
        int evaluated_pairs = 0;
        
        // Dynamic Temperature: Starts hot, cools down by 5% every frame
        double temperature = 50.0 * pow(0.95, ctx->phase_iter);
        
        #pragma omp parallel for schedule(dynamic) reduction(+:local_swaps, evaluated_pairs)
        for (int s = 0; s < ctx->num_spheres; s++) {
            for (int u = 0; u < vcount; u++) {
                if (VECTOR(ctx->sphere_ids)[u] != s) continue;
                for (int v = u + 1; v < vcount; v++) {
                    if (VECTOR(ctx->sphere_ids)[v] != s) continue;
                    
                    evaluated_pairs++;
                    double delta = calculate_swap_delta(ig, &graph->current_layout, &ctx->sphere_ids, ctx->cut_edges, u, v, s, s);
                    bool accept_swap = false;
                    
                    if (delta < -0.001) {
                        accept_swap = true; // Pure improvement
                    } else if (temperature > 0.01) {
                        // Accept worse layouts probabilistically to escape local minima
                        double prob = exp(-delta / temperature);
                        if (fast_rand_float(u, v, ctx->phase_iter) < prob) {
                            accept_swap = true;
                        }
                    }
                    
                    if (accept_swap) {
                        for (int dim = 0; dim < 3; dim++) {
                            double temp = MATRIX(graph->current_layout, u, dim);
                            MATRIX(graph->current_layout, u, dim) = MATRIX(graph->current_layout, v, dim);
                            MATRIX(graph->current_layout, v, dim) = temp;
                            
                            graph->nodes[u].position[dim] = MATRIX(graph->current_layout, u, dim);
                            graph->nodes[v].position[dim] = MATRIX(graph->current_layout, v, dim);
                        }
                        local_swaps++;
                    }
                }
            }
        }

        printf("[DEBUG] P1 (Iter %d) Temp: %.2f | Evaluated: %d | Swaps: %d\n", ctx->phase_iter, temperature, evaluated_pairs, local_swaps);

        // Only move to Phase 2 when it has fully cooled AND no strict improvements can be made
        if (temperature <= 0.01 && local_swaps == 0) {
            printf("[DEBUG] Phase 1 Settled. Moving to Phase 2.\n");
            ctx->phase = PHASE_INTER_SPHERE;
            ctx->phase_iter = 0; // Reset iterator for Phase 2
        } else {
            ctx->phase_iter++;
        }
        ctx->current_iter++;
        return true;
    }

    // ---------------------------------------------------------
    // PHASE 2: Parallel Inter-Sphere (With Simulated Annealing)
    // ---------------------------------------------------------
    if (ctx->phase == PHASE_INTER_SPHERE) {
        int local_swaps = 0;
        int evaluated_pairs = 0;
        
        // Start slightly cooler since Phase 1 already built a solid foundation
        double temperature = 20.0 * pow(0.95, ctx->phase_iter); 
        int start_s = ctx->inter_sphere_pass % 2;
        
        #pragma omp parallel for schedule(dynamic) reduction(+:local_swaps, evaluated_pairs)
        for (int s = start_s; s < ctx->num_spheres - 1; s += 2) {
            for (int u = 0; u < vcount; u++) {
                if (VECTOR(ctx->sphere_ids)[u] != s) continue;
                for (int v = u + 1; v < vcount; v++) {
                    if (VECTOR(ctx->sphere_ids)[v] != s) continue;
                    
                    evaluated_pairs++;
                    
                    // target_1 = -1 means we calculate distance to ALL connected spheres to ensure true global alignment
                    double delta = calculate_swap_delta(ig, &graph->current_layout, &ctx->sphere_ids, NULL, u, v, -1, -1);
                    bool accept_swap = false;
                    
                    if (delta < -0.001) {
                        accept_swap = true;
                    } else if (temperature > 0.01) {
                        double prob = exp(-delta / temperature);
                        if (fast_rand_float(u, v, ctx->phase_iter) < prob) {
                            accept_swap = true;
                        }
                    }

                    if (accept_swap) {
                        for (int dim=0; dim<3; dim++) {
                            double t = MATRIX(graph->current_layout, u, dim);
                            MATRIX(graph->current_layout, u, dim) = MATRIX(graph->current_layout, v, dim);
                            MATRIX(graph->current_layout, v, dim) = t;
                            graph->nodes[u].position[dim] = MATRIX(graph->current_layout, u, dim);
                            graph->nodes[v].position[dim] = MATRIX(graph->current_layout, v, dim);
                        }
                        local_swaps++;
                    }
                }
            }
        }

        printf("[DEBUG] P2 (Iter %d) %s Pass | Temp: %.2f | Evaluated: %d | Swaps: %d\n", 
               ctx->phase_iter, (ctx->inter_sphere_pass % 2 == 0) ? "Even" : "Odd", temperature, evaluated_pairs, local_swaps);

        ctx->inter_sphere_pass++;
        
        if (temperature <= 0.01 && local_swaps == 0 && (ctx->inter_sphere_pass % 2 == 0)) {
            printf("[DEBUG] Phase 2 Settled. Layout Complete!\n");
            ctx->phase = PHASE_DONE;
            return false; 
        }
        
        ctx->phase_iter++;
        ctx->current_iter++;
        return true;
    }

    return false;
}

const char* layered_sphere_get_stage_name(LayeredSphereContext* ctx) {
    static char stage_name[128];
    if (!ctx || !ctx->initialized) return "Layered Sphere - Uninitialized";

    switch (ctx->phase) {
        case PHASE_INIT: return "Phase 0: Metric Calculation & Grid Placement";
        case PHASE_INTRA_SPHERE: 
            snprintf(stage_name, sizeof(stage_name), "Phase 1: Parallel Intra-Sphere (%d Spheres)", ctx->num_spheres);
            return stage_name;
        case PHASE_INTER_SPHERE:
            snprintf(stage_name, sizeof(stage_name), "Phase 2: Parallel Inter-Sphere (%s Pairs)", 
                    (ctx->inter_sphere_pass % 2 == 0) ? "Even" : "Odd");
            return stage_name;
        case PHASE_DONE: return "Optimization Complete";
        default: return "Unknown Phase";
    }
}
