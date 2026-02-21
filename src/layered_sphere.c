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
// Standard rand() causes lock contention in OpenMP. 
static inline double fast_rand_float(unsigned int u, unsigned int v, unsigned int iter) {
    unsigned int hash = u * 73856093 ^ v * 19349663 ^ iter * 83492791;
    hash = (hash ^ (hash >> 16)) * 0x85ebca6b;
    hash = (hash ^ (hash >> 13)) * 0xc2b2ae35;
    hash = hash ^ (hash >> 16);
    return (double)(hash % 100000) / 100000.0;
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

typedef struct { 
    int id; 
    int community_id;
    double density; 
} NodePlacement;

// Sorts primarily by Community, secondarily by Density
int compare_nodes_placement(const void *a, const void *b) {
    NodePlacement* nodeA = (NodePlacement*)a;
    NodePlacement* nodeB = (NodePlacement*)b;
    
    // Group by community first
    if (nodeA->community_id != nodeB->community_id) {
        return nodeA->community_id - nodeB->community_id;
    }
    
    // If in the same community, sort by density
    double diff = nodeA->density - nodeB->density;
    return (diff > 0) - (diff < 0);
}

typedef struct { int id; double density; } NodeDensity;
int compare_nodes_density(const void *a, const void *b) {
    double diff = ((NodeDensity*)a)->density - ((NodeDensity*)b)->density;
    return (diff > 0) - (diff < 0);
}

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

int get_vector_int_max(const igraph_vector_int_t* v) {
    int max_val = 0;
    for (int i = 0; i < igraph_vector_int_size(v); i++) {
        if (VECTOR(*v)[i] > max_val) max_val = VECTOR(*v)[i];
    }
    return max_val == 0 ? 1 : max_val;
}

// Evaluates the energy change if Node U moves to a new grid slot.
// Automatically handles "Sliding" (slot is empty) vs "Swapping" (slot is occupied by V).
double calculate_move_delta(const igraph_t *ig, const igraph_matrix_t *layout, 
                            LayeredSphereContext* ctx,
                            int u, int target_sphere_s, int target_slot_k, 
                            bool respect_cuts, int eval_sphere_1, int eval_sphere_2) {
                                
    int v = ctx->grids[target_sphere_s].slot_occupant[target_slot_k];
    
    double old_energy = 0.0, new_energy = 0.0;
    
    double ux = MATRIX(*layout, u, 0), uy = MATRIX(*layout, u, 1), uz = MATRIX(*layout, u, 2);
    double target_x = ctx->grids[target_sphere_s].slots[target_slot_k].x;
    double target_y = ctx->grids[target_sphere_s].slots[target_slot_k].y;
    double target_z = ctx->grids[target_sphere_s].slots[target_slot_k].z;

    igraph_vector_int_t u_edges, v_edges;
    igraph_vector_int_init(&u_edges, 0);
    igraph_vector_int_init(&v_edges, 0);
    
    igraph_incident(ig, &u_edges, u, IGRAPH_ALL);
    if (v != -1) igraph_incident(ig, &v_edges, v, IGRAPH_ALL);

    // Calculate delta for U's edges
    for (int i = 0; i < igraph_vector_int_size(&u_edges); i++) {
        igraph_integer_t edge_id = VECTOR(u_edges)[i];
        if (respect_cuts && ctx->cut_edges && ctx->cut_edges[edge_id]) continue;
        
        igraph_integer_t from, to;
        igraph_edge(ig, edge_id, &from, &to);
        igraph_integer_t neighbor = (from == u) ? to : from;
        
        if (neighbor == v) continue; // Distance between U and V doesn't change when they swap
        
        if (eval_sphere_1 != -1) {
            int n_sphere = ctx->node_to_sphere_id[neighbor];
            if (n_sphere != eval_sphere_1 && n_sphere != eval_sphere_2) continue;
        }
        
        double nx = MATRIX(*layout, neighbor, 0), ny = MATRIX(*layout, neighbor, 1), nz = MATRIX(*layout, neighbor, 2);
        old_energy += dist3d(ux, uy, uz, nx, ny, nz);
        new_energy += dist3d(target_x, target_y, target_z, nx, ny, nz); // U takes the target slot
    }

    // Calculate delta for V's edges (if target slot was occupied)
    if (v != -1) {
        double vx = target_x, vy = target_y, vz = target_z;
        for (int i = 0; i < igraph_vector_int_size(&v_edges); i++) {
            igraph_integer_t edge_id = VECTOR(v_edges)[i];
            if (respect_cuts && ctx->cut_edges && ctx->cut_edges[edge_id]) continue;
            
            igraph_integer_t from, to;
            igraph_edge(ig, edge_id, &from, &to);
            igraph_integer_t neighbor = (from == v) ? to : from;
            
            if (neighbor == u) continue;
            
            if (eval_sphere_1 != -1) {
                int n_sphere = ctx->node_to_sphere_id[neighbor];
                if (n_sphere != eval_sphere_1 && n_sphere != eval_sphere_2) continue;
            }
            
            double nx = MATRIX(*layout, neighbor, 0), ny = MATRIX(*layout, neighbor, 1), nz = MATRIX(*layout, neighbor, 2);
            old_energy += dist3d(vx, vy, vz, nx, ny, nz);
            new_energy += dist3d(ux, uy, uz, nx, ny, nz); // V takes U's old slot
        }
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
    ctx->grids = NULL;
    ctx->node_to_sphere_id = NULL;
    ctx->node_to_slot_idx = NULL;
    ctx->cut_edges = NULL;
}

void layered_sphere_cleanup(LayeredSphereContext* ctx) {
    if (ctx->initialized) {
        if (ctx->cut_edges) free(ctx->cut_edges);
        if (ctx->node_to_sphere_id) free(ctx->node_to_sphere_id);
        if (ctx->node_to_slot_idx) free(ctx->node_to_slot_idx);
        
        if (ctx->grids) {
            for (int s = 0; s < ctx->num_spheres; s++) {
                if (ctx->grids[s].slots) free(ctx->grids[s].slots);
                if (ctx->grids[s].slot_occupant) free(ctx->grids[s].slot_occupant);
            }
            free(ctx->grids);
        }
    }
    ctx->initialized = false;
}

bool layered_sphere_step(LayeredSphereContext* ctx, GraphData* graph) {
    if (!ctx->initialized || ctx->phase == PHASE_DONE) return false;
    
    igraph_t* ig = &graph->g; 
    int vcount = graph->node_count;
    int ecount = igraph_ecount(ig);

    // ---------------------------------------------------------
    // PHASE 0: Dynamic Scaling & Sparse Grid Placement
    // ---------------------------------------------------------
    if (ctx->phase == PHASE_INIT) {
        printf("[DEBUG] PHASE 0: Initializing Sparse Grids for %d nodes...\n", vcount);
        
        ctx->num_spheres = (int)fmax(3.0, sqrt(vcount) * 0.5);
        ctx->grids = calloc(ctx->num_spheres, sizeof(SphereGrid));
        ctx->node_to_sphere_id = malloc(vcount * sizeof(int));
        ctx->node_to_slot_idx = malloc(vcount * sizeof(int));

        igraph_vector_int_t coreness; igraph_vector_int_init(&coreness, vcount);
        igraph_coreness(ig, &coreness, IGRAPH_ALL);

        igraph_vector_t betweenness; igraph_vector_init(&betweenness, vcount);
        igraph_betweenness(ig, &betweenness, igraph_vss_all(), IGRAPH_UNDIRECTED, NULL);

        igraph_vector_t eccentricity; igraph_vector_init(&eccentricity, vcount);
        igraph_eccentricity(ig, &eccentricity, igraph_vss_all(), IGRAPH_ALL);

        double max_c = get_vector_int_max(&coreness);
        double max_b = get_vector_max(&betweenness);
        double max_e = get_vector_max(&eccentricity);



	// ---------------------------------------------------------
        // Calculate Communities (Leiden) with Dynamic Resolution
        // ---------------------------------------------------------
        igraph_vector_int_t membership;
        igraph_vector_int_init(&membership, vcount);
        
        // Calculate Average Degree
        double avg_degree = (vcount > 0) ? (double)(2 * ecount) / vcount : 0.0;
        
        // Dynamic Resolution Formula:
        // We floor it at 1.0 (standard modularity) so sparse graphs don't shatter.
        // For dense graphs, we increase the resolution to force smaller, localized clusters.
        // Dividing by 5.0 is a highly effective heuristic for 3D spatial chunking.
        double dynamic_resolution = fmax(1.0, avg_degree / 5.0);
        
        // Pass the dynamic_resolution into the Leiden algorithm
        igraph_community_leiden(ig, NULL, NULL, dynamic_resolution, 0.01, false, 2, &membership, NULL, NULL);
        
        printf("[DEBUG] Leiden Resolution set to %.2f (Avg Deg: %.2f). Found %d communities.\n", 
               dynamic_resolution, avg_degree, get_vector_int_max(&membership) + 1);

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
            ctx->node_to_sphere_id[composite_nodes[i].id] = assigned_sphere;
        }
        free(composite_nodes);

        // Edge Cutting
        ctx->cut_edges = calloc(ecount, sizeof(bool));
        igraph_vector_int_t edge_pairs; igraph_vector_int_init(&edge_pairs, ecount * 2);
        for (int i = 0; i < ecount; i++) {
            igraph_integer_t u, v; igraph_edge(ig, i, &u, &v);
            VECTOR(edge_pairs)[i*2] = u; VECTOR(edge_pairs)[i*2+1] = v;
        }
        igraph_vector_t jaccard_sim; igraph_vector_init(&jaccard_sim, ecount);
        igraph_similarity_jaccard_pairs(ig, &jaccard_sim, &edge_pairs, IGRAPH_ALL, 0); 

        for (int i = 0; i < ecount; i++) {
            igraph_integer_t u = VECTOR(edge_pairs)[i*2], v = VECTOR(edge_pairs)[i*2+1];
            if (ctx->node_to_sphere_id[u] == ctx->node_to_sphere_id[v]) {
                if (VECTOR(jaccard_sim)[i] < 0.05) ctx->cut_edges[i] = true;
            }
        }
        igraph_vector_int_destroy(&edge_pairs); igraph_vector_destroy(&jaccard_sim);

        // Density & Sparse Grid Calculation
        igraph_vector_t transitivity; igraph_vector_init(&transitivity, vcount);
        igraph_transitivity_local_undirected(ig, &transitivity, igraph_vss_all(), IGRAPH_TRANSITIVITY_ZERO);

        double base_radius = 5.0; 
        double layer_spacing = 15.0; 
        int hilbert_res = 32768;

        #pragma omp parallel for schedule(dynamic)
        for (int s = 0; s < ctx->num_spheres; s++) {
            int n_in_group = 0;
            for (int i = 0; i < vcount; i++) if (ctx->node_to_sphere_id[i] == s) n_in_group++;
            if (n_in_group == 0) continue;

            double radius = base_radius + (s * layer_spacing);
            
            // MATH: Surface Area Proportional Grid Scaling
            // We guarantee at least 3x empty space, scaling up quadratically for outer spheres
            int M_s = (int)fmax(n_in_group * 3.0, (4.0 * M_PI * radius * radius) / 20.0);
            if (M_s > 100000) M_s = 100000; // Hard cap to prevent memory exhaustion on massive graphs
            
            ctx->grids[s].max_slots = M_s;
            ctx->grids[s].num_occupants = n_in_group;
            ctx->grids[s].slots = malloc(M_s * sizeof(SpherePoint));
            ctx->grids[s].slot_occupant = malloc(M_s * sizeof(int));
            
            for(int k=0; k<M_s; k++) ctx->grids[s].slot_occupant[k] = -1; // -1 means empty

            // Generate dense Fibonacci grid
            for (int i = 0; i < M_s; i++) {
                double phi = acos(1.0 - 2.0 * (i + 0.5) / M_s);
                double theta = M_PI * (1.0 + sqrt(5.0)) * i;
                ctx->grids[s].slots[i].x = radius * cos(theta) * sin(phi);
                ctx->grids[s].slots[i].y = radius * sin(theta) * sin(phi);
                ctx->grids[s].slots[i].z = radius * cos(phi);

                double norm_x = fmod(theta, 2 * M_PI) / (2 * M_PI);
                if (norm_x < 0) norm_x += 1.0;
                ctx->grids[s].slots[i].hilbert_dist = xy2d(hilbert_res, (int)(norm_x*(hilbert_res-1)), (int)((phi/M_PI)*(hilbert_res-1)));
            }
            qsort(ctx->grids[s].slots, M_s, sizeof(SpherePoint), compare_points);

	    // Group nodes by Community and Density
            NodePlacement *group_nodes = malloc(n_in_group * sizeof(NodePlacement));
            int idx = 0;
            for (int i = 0; i < vcount; i++) {
                if (ctx->node_to_sphere_id[i] == s) {
                    group_nodes[idx].id = i;
                    group_nodes[idx].community_id = VECTOR(membership)[i];
                    group_nodes[idx].density = VECTOR(transitivity)[i];
                    idx++;
                }
            }
            
            // This is the magic step. The Hilbert curve will now draw physical
            // boundaries around these sorted communities on the sphere surface.
            qsort(group_nodes, n_in_group, sizeof(NodePlacement), compare_nodes_placement);


            // Sparse assignment across the grid
            int step = M_s / n_in_group;
            for (int i = 0; i < n_in_group; i++) {
                int node_id = group_nodes[i].id;
                int slot_idx = i * step;
                if (slot_idx >= M_s) slot_idx = M_s - 1; // Failsafe
                
                ctx->grids[s].slot_occupant[slot_idx] = node_id;
                ctx->node_to_slot_idx[node_id] = slot_idx;
                
                MATRIX(graph->current_layout, node_id, 0) = ctx->grids[s].slots[slot_idx].x;
                MATRIX(graph->current_layout, node_id, 1) = ctx->grids[s].slots[slot_idx].y;
                MATRIX(graph->current_layout, node_id, 2) = ctx->grids[s].slots[slot_idx].z;
            }
            free(group_nodes);
        }

        // Sync initial layout visually
        for (int i=0; i<vcount; i++) {
            graph->nodes[i].position[0] = MATRIX(graph->current_layout, i, 0);
            graph->nodes[i].position[1] = MATRIX(graph->current_layout, i, 1);
            graph->nodes[i].position[2] = MATRIX(graph->current_layout, i, 2);
        }

        igraph_vector_int_destroy(&coreness); igraph_vector_destroy(&betweenness); 
        igraph_vector_destroy(&eccentricity); igraph_vector_destroy(&transitivity);
        igraph_vector_int_destroy(&membership);
        
        ctx->phase = PHASE_INTRA_SPHERE;
        ctx->phase_iter = 0;
        return true; 
    }

    // ---------------------------------------------------------
    // PHASE 1: Parallel Intra-Sphere Random-Walk
    // ---------------------------------------------------------
    if (ctx->phase == PHASE_INTRA_SPHERE) {
        int local_moves = 0, evaluated = 0;
        double temperature = 50.0 * pow(0.95, ctx->phase_iter);
        
        #pragma omp parallel for schedule(dynamic) reduction(+:local_moves, evaluated)
        for (int s = 0; s < ctx->num_spheres; s++) {
            int max_slots = ctx->grids[s].max_slots;
            
            for (int u = 0; u < vcount; u++) {
                if (ctx->node_to_sphere_id[u] != s) continue;
                int current_slot = ctx->node_to_slot_idx[u];
                
                // Attempt 15 random moves/slides across the fine grid per node
                for (int attempt = 0; attempt < 15; attempt++) {
                    evaluated++;
                    
                    int target_slot = (int)(fast_rand_float(u, attempt, ctx->phase_iter) * max_slots);
                    if (target_slot >= max_slots) target_slot = max_slots - 1;
                    if (target_slot == current_slot) continue;
                    
                    double delta = calculate_move_delta(ig, &graph->current_layout, ctx, u, s, target_slot, true, s, s);
                    bool accept = false;
                    
                    if (delta < -0.001) accept = true;
                    else if (temperature > 0.01 && fast_rand_float(u, target_slot, ctx->phase_iter) < exp(-delta / temperature)) accept = true;
                    
                    if (accept) {
                        int v = ctx->grids[s].slot_occupant[target_slot];
                        
                        // U slides to target
                        MATRIX(graph->current_layout, u, 0) = ctx->grids[s].slots[target_slot].x;
                        MATRIX(graph->current_layout, u, 1) = ctx->grids[s].slots[target_slot].y;
                        MATRIX(graph->current_layout, u, 2) = ctx->grids[s].slots[target_slot].z;
                        ctx->grids[s].slot_occupant[target_slot] = u;
                        ctx->node_to_slot_idx[u] = target_slot;
                        
                        // If occupied, V slides to U's old spot
                        if (v != -1) {
                            MATRIX(graph->current_layout, v, 0) = ctx->grids[s].slots[current_slot].x;
                            MATRIX(graph->current_layout, v, 1) = ctx->grids[s].slots[current_slot].y;
                            MATRIX(graph->current_layout, v, 2) = ctx->grids[s].slots[current_slot].z;
                            ctx->grids[s].slot_occupant[current_slot] = v;
                            ctx->node_to_slot_idx[v] = current_slot;
                            
                            graph->nodes[v].position[0] = MATRIX(graph->current_layout, v, 0);
                            graph->nodes[v].position[1] = MATRIX(graph->current_layout, v, 1);
                            graph->nodes[v].position[2] = MATRIX(graph->current_layout, v, 2);
                        } else {
                            ctx->grids[s].slot_occupant[current_slot] = -1; // Slot is now empty
                        }
                        
                        graph->nodes[u].position[0] = MATRIX(graph->current_layout, u, 0);
                        graph->nodes[u].position[1] = MATRIX(graph->current_layout, u, 1);
                        graph->nodes[u].position[2] = MATRIX(graph->current_layout, u, 2);
                        
                        local_moves++;
                        break; // Stop evaluating this node if a move was successful
                    }
                }
            }
        }

        printf("[DEBUG] P1 Temp: %.2f | Eval: %d | Moves: %d\n", temperature, evaluated, local_moves);

        if (temperature <= 0.01 && local_moves == 0) {
            ctx->phase = PHASE_INTER_SPHERE;
            ctx->phase_iter = 0; 
        } else {
            ctx->phase_iter++;
        }
        ctx->current_iter++;
        return true;
    }

    // ---------------------------------------------------------
    // PHASE 2: Parallel Inter-Sphere Random-Walk
    // ---------------------------------------------------------
    if (ctx->phase == PHASE_INTER_SPHERE) {
        int local_moves = 0, evaluated = 0;
        double temperature = 20.0 * pow(0.95, ctx->phase_iter); 
        int start_s = ctx->inter_sphere_pass % 2;
        
        // HOGWILD Approach: We accept benign read-races on neighbor coords globally 
        // to ensure true global alignment without locking the entire matrix.
        #pragma omp parallel for schedule(dynamic) reduction(+:local_moves, evaluated)
        for (int s = start_s; s < ctx->num_spheres; s += 2) {
            int max_slots = ctx->grids[s].max_slots;
            
            for (int u = 0; u < vcount; u++) {
                if (ctx->node_to_sphere_id[u] != s) continue;
                int current_slot = ctx->node_to_slot_idx[u];
                
                for (int attempt = 0; attempt < 15; attempt++) {
                    evaluated++;
                    int target_slot = (int)(fast_rand_float(u, attempt, ctx->phase_iter) * max_slots);
                    if (target_slot >= max_slots) target_slot = max_slots - 1;
                    if (target_slot == current_slot) continue;
                    
                    // Eval -1 ensures we check distance to ALL connected spheres
                    double delta = calculate_move_delta(ig, &graph->current_layout, ctx, u, s, target_slot, false, -1, -1);
                    bool accept = false;
                    
                    if (delta < -0.001) accept = true;
                    else if (temperature > 0.01 && fast_rand_float(u, target_slot, ctx->phase_iter) < exp(-delta / temperature)) accept = true;

                    if (accept) {
                        int v = ctx->grids[s].slot_occupant[target_slot];
                        
                        MATRIX(graph->current_layout, u, 0) = ctx->grids[s].slots[target_slot].x;
                        MATRIX(graph->current_layout, u, 1) = ctx->grids[s].slots[target_slot].y;
                        MATRIX(graph->current_layout, u, 2) = ctx->grids[s].slots[target_slot].z;
                        ctx->grids[s].slot_occupant[target_slot] = u;
                        ctx->node_to_slot_idx[u] = target_slot;
                        
                        if (v != -1) {
                            MATRIX(graph->current_layout, v, 0) = ctx->grids[s].slots[current_slot].x;
                            MATRIX(graph->current_layout, v, 1) = ctx->grids[s].slots[current_slot].y;
                            MATRIX(graph->current_layout, v, 2) = ctx->grids[s].slots[current_slot].z;
                            ctx->grids[s].slot_occupant[current_slot] = v;
                            ctx->node_to_slot_idx[v] = current_slot;
                            
                            graph->nodes[v].position[0] = MATRIX(graph->current_layout, v, 0);
                            graph->nodes[v].position[1] = MATRIX(graph->current_layout, v, 1);
                            graph->nodes[v].position[2] = MATRIX(graph->current_layout, v, 2);
                        } else {
                            ctx->grids[s].slot_occupant[current_slot] = -1;
                        }
                        
                        graph->nodes[u].position[0] = MATRIX(graph->current_layout, u, 0);
                        graph->nodes[u].position[1] = MATRIX(graph->current_layout, u, 1);
                        graph->nodes[u].position[2] = MATRIX(graph->current_layout, u, 2);
                        
                        local_moves++;
                        break;
                    }
                }
            }
        }

        printf("[DEBUG] P2 Temp: %.2f | Eval: %d | Moves: %d\n", temperature, evaluated, local_moves);

        ctx->inter_sphere_pass++;
        if (temperature <= 0.01 && local_moves == 0 && (ctx->inter_sphere_pass % 2 == 0)) {
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
        case PHASE_INIT: return "Phase 0: Sparse Grid Initialization";
        case PHASE_INTRA_SPHERE: 
            snprintf(stage_name, sizeof(stage_name), "Phase 1: Intra-Sphere Sliding (%d Spheres)", ctx->num_spheres);
            return stage_name;
        case PHASE_INTER_SPHERE:
            snprintf(stage_name, sizeof(stage_name), "Phase 2: Global Align (%s Pairs)", 
                    (ctx->inter_sphere_pass % 2 == 0) ? "Even" : "Odd");
            return stage_name;
        case PHASE_DONE: return "Optimization Complete";
        default: return "Unknown Phase";
    }
}
