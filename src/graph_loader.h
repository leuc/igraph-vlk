#ifndef GRAPH_LOADER_H
#define GRAPH_LOADER_H

#include <igraph/igraph.h>
#include <cglm/cglm.h>

typedef enum {
    LAYOUT_FR_3D,
    LAYOUT_KK_3D,
    LAYOUT_RANDOM_3D,
    LAYOUT_SPHERE,
    LAYOUT_GRID_3D,
    LAYOUT_UMAP_3D,
    LAYOUT_DRL_3D
} LayoutType;

typedef enum {
    CLUSTER_FASTGREEDY,
    CLUSTER_WALKTRAP,
    CLUSTER_LABEL_PROP,
    CLUSTER_MULTILEVEL,
    CLUSTER_LEIDEN,
    CLUSTER_COUNT
} ClusterType;

typedef enum {
    CENTRALITY_PAGERANK,
    CENTRALITY_HUB,
    CENTRALITY_AUTHORITY,
    CENTRALITY_BETWEENNESS,
    CENTRALITY_DEGREE,
    CENTRALITY_CLOSENESS,
    CENTRALITY_HARMONIC,
    CENTRALITY_EIGENVECTOR,
    CENTRALITY_STRENGTH,
    CENTRALITY_CONSTRAINT,
    CENTRALITY_COUNT
} CentralityType;

typedef struct {
    vec3 position;
    vec3 color;
    float size;
    char* label;
    int degree;
} Node;

typedef struct {
    uint32_t from;
    uint32_t to;
    float size;
} Edge;

typedef struct {
    Node* nodes;
    uint32_t node_count;
    Edge* edges;
    uint32_t edge_count;
} GraphData;

int graph_load_graphml(const char* filename, GraphData* data, LayoutType layout_type, int node_limit, const char* node_attr, const char* edge_attr);
void graph_cluster(const char* filename, GraphData* data, ClusterType type, int node_limit);
void graph_calculate_centrality(const char* filename, GraphData* data, CentralityType type, int node_limit);
void graph_free_data(GraphData* data);

#endif
