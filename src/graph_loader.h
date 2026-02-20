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
    LAYOUT_DRL_3D,
    LAYOUT_OPENORD_3D,
    LAYOUT_COUNT
} LayoutType;

typedef struct OpenOrdContext OpenOrdContext; // Fixed typedef

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
    int coreness;
    float glow;
} Node;

typedef struct {
    uint32_t from;
    uint32_t to;
    float size;
} Edge;

typedef struct {
    int node_count;
    int edge_count;
    int coreness_filter;
} GraphProperties;

typedef struct {
    Node* nodes;
    uint32_t node_count;
    Edge* edges;
    uint32_t edge_count;
    
    igraph_t g;
    igraph_matrix_t current_layout;
    bool graph_initialized;
    char* node_attr_name;
    char* edge_attr_name;
    GraphProperties props;
    struct OpenOrdContext* openord;
} GraphData;

int graph_load_graphml(const char* filename, GraphData* data, LayoutType layout_type, const char* node_attr, const char* edge_attr);
void graph_cluster(GraphData* data, ClusterType type);
void graph_calculate_centrality(GraphData* data, CentralityType type);
void graph_layout_step(GraphData* data, LayoutType type, int iterations);
void graph_remove_overlaps(GraphData* data, float layoutScale);
void graph_filter_degree(GraphData* data, int min_degree);
void graph_filter_coreness(GraphData* data, int min_coreness);
void graph_highlight_infrastructure(GraphData* data);
void graph_free_data(GraphData* data);

#endif
