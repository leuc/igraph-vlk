#define _GNU_SOURCE
#include "graph_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

int graph_load_graphml(const char* filename, GraphData* data, LayoutType layout_type, int node_limit, const char* node_attr, const char* edge_attr) {
    igraph_set_attribute_table(&igraph_cattribute_table);
    igraph_t g_full; FILE* fp = fopen(filename, "r"); if (!fp) { perror("Error opening file"); return -1; }
    if (igraph_read_graph_graphml(&g_full, fp, 0) != IGRAPH_SUCCESS) { fprintf(stderr, "Error reading GraphML\n"); fclose(fp); return -1; }
    fclose(fp);
    igraph_t g;
    if (node_limit > 0 && igraph_vcount(&g_full) > node_limit) {
        igraph_vs_t vs; igraph_vector_int_t vids; igraph_vector_int_init(&vids, node_limit);
        for(int i=0; i<node_limit; i++) VECTOR(vids)[i] = i;
        igraph_vs_vector(&vs, &vids); igraph_induced_subgraph(&g_full, &g, vs, IGRAPH_SUBGRAPH_CREATE_FROM_SCRATCH);
        igraph_vs_destroy(&vs); igraph_vector_int_destroy(&vids); igraph_destroy(&g_full);
    } else { g = g_full; }
    igraph_simplify(&g, 1, 1, NULL);
    data->node_count = igraph_vcount(&g); data->edge_count = igraph_ecount(&g);
    igraph_matrix_t layout; igraph_matrix_init(&layout, 0, 0);
    switch (layout_type) {
        case LAYOUT_FR_3D: igraph_layout_fruchterman_reingold_3d(&g, &layout, 0, 50, (igraph_real_t)data->node_count, NULL, NULL, NULL, NULL, NULL, NULL, NULL); break;
        case LAYOUT_KK_3D: igraph_layout_kamada_kawai_3d(&g, &layout, 0, 100, 0.0, 1.0, NULL, NULL, NULL, NULL, NULL, NULL, NULL); break;
        case LAYOUT_RANDOM_3D: igraph_layout_random_3d(&g, &layout); break;
        case LAYOUT_SPHERE: igraph_layout_sphere(&g, &layout); break;
        case LAYOUT_GRID_3D: { int side = (int)ceil(pow(data->node_count, 1.0/3.0)); igraph_layout_grid_3d(&g, &layout, side, side); break; }
        case LAYOUT_UMAP_3D: igraph_layout_umap_3d(&g, &layout, 0, NULL, 0.1, 200, 0); break;
        case LAYOUT_DRL_3D: { igraph_layout_drl_options_t options; igraph_layout_drl_options_init(&options, IGRAPH_LAYOUT_DRL_DEFAULT); igraph_layout_drl_3d(&g, &layout, 0, &options, NULL); break; }
    }
    const char* n_attr = node_attr ? node_attr : "pagerank";
    bool has_node_attr = igraph_cattribute_has_attr(&g, IGRAPH_ATTRIBUTE_VERTEX, n_attr);
    bool has_label = igraph_cattribute_has_attr(&g, IGRAPH_ATTRIBUTE_VERTEX, "label");
    float max_n_val = 0.0f;
    if (has_node_attr) { for (int i = 0; i < data->node_count; i++) { float val = (float)VAN(&g, n_attr, i); if (val > max_n_val) max_n_val = val; } }
    data->nodes = malloc(sizeof(Node) * data->node_count);
    for (int i = 0; i < data->node_count; i++) {
        data->nodes[i].position[0] = MATRIX(layout, i, 0); data->nodes[i].position[1] = MATRIX(layout, i, 1); data->nodes[i].position[2] = (igraph_matrix_ncol(&layout) > 2) ? MATRIX(layout, i, 2) : 0.0f;
        data->nodes[i].color[0] = (float)rand()/RAND_MAX; data->nodes[i].color[1] = (float)rand()/RAND_MAX; data->nodes[i].color[2] = (float)rand()/RAND_MAX;
        data->nodes[i].size = (has_node_attr && max_n_val > 0) ? (float)VAN(&g, n_attr, i)/max_n_val : 1.0f;
        data->nodes[i].label = has_label ? strdup(VAS(&g, "label", i)) : NULL;
        igraph_vector_int_t neighbors; igraph_vector_int_init(&neighbors, 0); igraph_neighbors(&g, &neighbors, i, IGRAPH_ALL);
        data->nodes[i].degree = igraph_vector_int_size(&neighbors); igraph_vector_int_destroy(&neighbors);
    }
    const char* e_attr = edge_attr ? edge_attr : "betweenness";
    bool has_edge_attr = igraph_cattribute_has_attr(&g, IGRAPH_ATTRIBUTE_EDGE, e_attr);
    float max_e_val = 0.0f; if (has_edge_attr) { for (int i = 0; i < data->edge_count; i++) { float val = (float)EAN(&g, e_attr, i); if (val > max_e_val) max_e_val = val; } }
    data->edges = malloc(sizeof(Edge) * data->edge_count);
    for (int i = 0; i < data->edge_count; i++) {
        igraph_integer_t from, to; igraph_edge(&g, i, &from, &to); data->edges[i].from = (uint32_t)from; data->edges[i].to = (uint32_t)to;
        data->edges[i].size = (has_edge_attr && max_e_val > 0) ? (float)EAN(&g, e_attr, i)/max_e_val : 1.0f;
    }
    igraph_matrix_destroy(&layout); igraph_destroy(&g); return 0;
}

void graph_cluster(const char* filename, GraphData* data, ClusterType type, int node_limit) {
    igraph_t g_full; FILE* fp = fopen(filename, "r"); if (!fp) return;
    if (igraph_read_graph_graphml(&g_full, fp, 0) != IGRAPH_SUCCESS) { fclose(fp); return; }
    fclose(fp); igraph_t g;
    if (node_limit > 0 && igraph_vcount(&g_full) > node_limit) {
        igraph_vs_t vs; igraph_vector_int_t vids; igraph_vector_int_init(&vids, node_limit);
        for(int i=0; i<node_limit; i++) VECTOR(vids)[i] = i;
        igraph_vs_vector(&vs, &vids); igraph_induced_subgraph(&g_full, &g, vs, IGRAPH_SUBGRAPH_CREATE_FROM_SCRATCH);
        igraph_vs_destroy(&vs); igraph_vector_int_destroy(&vids); igraph_destroy(&g_full);
    } else { g = g_full; }
    igraph_simplify(&g, 1, 1, NULL);
    igraph_vector_int_t membership; igraph_vector_int_init(&membership, igraph_vcount(&g));
    switch(type) {
        case CLUSTER_FASTGREEDY: { igraph_matrix_int_t m; igraph_vector_t mo; igraph_matrix_int_init(&m, 0, 0); igraph_vector_init(&mo, 0); igraph_community_fastgreedy(&g, NULL, &m, &mo, &membership); igraph_matrix_int_destroy(&m); igraph_vector_destroy(&mo); break; }
        case CLUSTER_WALKTRAP: { igraph_matrix_int_t m; igraph_vector_t mo; igraph_matrix_int_init(&m, 0, 0); igraph_vector_init(&mo, 0); igraph_community_walktrap(&g, NULL, 4, &m, &mo, &membership); igraph_matrix_int_destroy(&m); igraph_vector_destroy(&mo); break; }
        case CLUSTER_LABEL_PROP: igraph_community_label_propagation(&g, &membership, IGRAPH_ALL, NULL, NULL, NULL); break;
        case CLUSTER_MULTILEVEL: { igraph_vector_t mo; igraph_vector_init(&mo, 0); igraph_community_multilevel(&g, NULL, 1.0, &membership, NULL, &mo); igraph_vector_destroy(&mo); break; }
        case CLUSTER_LEIDEN: igraph_community_leiden(&g, NULL, NULL, 1.0, 0.01, false, 2, &membership, NULL, NULL); break;
        default: break;
    }
    int cluster_count = 0; for(int i=0; i<igraph_vector_int_size(&membership); i++) if(VECTOR(membership)[i] > cluster_count) cluster_count = VECTOR(membership)[i];
    cluster_count++; vec3* colors = malloc(sizeof(vec3) * cluster_count);
    for(int i=0; i<cluster_count; i++) { colors[i][0] = (float)rand()/RAND_MAX; colors[i][1] = (float)rand()/RAND_MAX; colors[i][2] = (float)rand()/RAND_MAX; }
    for(int i=0; i<data->node_count; i++) memcpy(data->nodes[i].color, colors[VECTOR(membership)[i]], 12);
    free(colors); igraph_vector_int_destroy(&membership); igraph_destroy(&g);
}

void graph_calculate_centrality(const char* filename, GraphData* data, CentralityType type, int node_limit) {
    igraph_t g_full; FILE* fp = fopen(filename, "r"); if (!fp) return;
    if (igraph_read_graph_graphml(&g_full, fp, 0) != IGRAPH_SUCCESS) { fclose(fp); return; }
    fclose(fp); igraph_t g;
    if (node_limit > 0 && igraph_vcount(&g_full) > node_limit) {
        igraph_vs_t vs; igraph_vector_int_t vids; igraph_vector_int_init(&vids, node_limit);
        for(int i=0; i<node_limit; i++) VECTOR(vids)[i] = i;
        igraph_vs_vector(&vs, &vids); igraph_induced_subgraph(&g_full, &g, vs, IGRAPH_SUBGRAPH_CREATE_FROM_SCRATCH);
        igraph_vs_destroy(&vs); igraph_vector_int_destroy(&vids); igraph_destroy(&g_full);
    } else { g = g_full; }
    igraph_simplify(&g, 1, 1, NULL);
    igraph_vector_t result; igraph_vector_init(&result, igraph_vcount(&g));
    switch(type) {
        case CENTRALITY_PAGERANK: igraph_pagerank(&g, IGRAPH_PAGERANK_ALGO_PRPACK, &result, NULL, igraph_vss_all(), IGRAPH_DIRECTED, 0.85, NULL, NULL); break;
        case CENTRALITY_HUB: { igraph_vector_t a; igraph_vector_init(&a, 0); igraph_hub_and_authority_scores(&g, &result, &a, NULL, true, NULL, NULL); igraph_vector_destroy(&a); break; }
        case CENTRALITY_AUTHORITY: { igraph_vector_t h; igraph_vector_init(&h, 0); igraph_hub_and_authority_scores(&g, &h, &result, NULL, true, NULL, NULL); igraph_vector_destroy(&h); break; }
        case CENTRALITY_BETWEENNESS: igraph_betweenness(&g, &result, igraph_vss_all(), IGRAPH_DIRECTED, NULL); break;
        case CENTRALITY_DEGREE: { igraph_vector_int_t d; igraph_vector_int_init(&d, 0); igraph_degree(&g, &d, igraph_vss_all(), IGRAPH_ALL, IGRAPH_LOOPS); for(int i=0; i<igraph_vector_int_size(&d); i++) VECTOR(result)[i] = (float)VECTOR(d)[i]; igraph_vector_int_destroy(&d); break; }
        case CENTRALITY_CLOSENESS: {
            igraph_bool_t all_reach;
            igraph_closeness(&g, &result, NULL, &all_reach, igraph_vss_all(), IGRAPH_ALL, NULL, true);
            break;
        }
        case CENTRALITY_HARMONIC: igraph_harmonic_centrality(&g, &result, igraph_vss_all(), IGRAPH_ALL, NULL, true); break;
        case CENTRALITY_EIGENVECTOR: igraph_eigenvector_centrality(&g, &result, NULL, IGRAPH_DIRECTED, true, NULL, NULL); break;
        case CENTRALITY_STRENGTH: igraph_strength(&g, &result, igraph_vss_all(), IGRAPH_ALL, IGRAPH_LOOPS, NULL); break;
        case CENTRALITY_CONSTRAINT: igraph_constraint(&g, &result, igraph_vss_all(), NULL); break;
        default: break;
    }
    igraph_real_t min_v, max_v; igraph_vector_minmax(&result, &min_v, &max_v);
    for(int i=0; i<data->node_count; i++) {
        float val = (float)VECTOR(result)[i];
        data->nodes[i].size = (max_v > 0) ? (val / (float)max_v) : 1.0f;
        data->nodes[i].degree = (int)(data->nodes[i].size * 20.0f);
    }
    igraph_vector_destroy(&result); igraph_destroy(&g);
}

void graph_free_data(GraphData* data) {
    for (uint32_t i = 0; i < data->node_count; i++) if (data->nodes[i].label) free(data->nodes[i].label);
    free(data->nodes); free(data->edges);
}
