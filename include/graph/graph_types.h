#ifndef GRAPH_TYPES_H
#define GRAPH_TYPES_H

#include <cglm/cglm.h>
#include <igraph/igraph.h>
#include <stdint.h>
#include <stdbool.h>

// Forward declare complex contexts from layout engines
typedef struct OpenOrdContext OpenOrdContext;
typedef struct LayeredSphereContext LayeredSphereContext;

/* ============================================================================
 * Enums (defined first as they're used by GraphData)
 * ============================================================================ */

/* Layout Type Enum */
typedef enum {
	LAYOUT_FR_3D,
	LAYOUT_KK_3D,
	LAYOUT_RANDOM_3D,
	LAYOUT_SPHERE,
	LAYOUT_GRID_3D,
	LAYOUT_UMAP_3D,
	LAYOUT_DRL_3D,
	LAYOUT_OPENORD_3D,
	LAYOUT_LAYERED_SPHERE,
	LAYOUT_COUNT
} LayoutType;

/* Cluster Type Enum */
typedef enum {
	CLUSTER_FASTGREEDY,
	CLUSTER_WALKTRAP,
	CLUSTER_LABEL_PROP,
	CLUSTER_MULTILEVEL,
	CLUSTER_LEIDEN,
	CLUSTER_COUNT
} ClusterType;

/* Centrality Type Enum */
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

/* Community Arrangement Mode Enum */
typedef enum {
    COMMUNITY_ARRANGEMENT_NONE,
    COMMUNITY_ARRANGEMENT_KECECI_2D,
    COMMUNITY_ARRANGEMENT_KECECI_TETRA_3D,
    COMMUNITY_ARRANGEMENT_COMPACT_ORTHO_2D,
    COMMUNITY_ARRANGEMENT_COMPACT_ORTHO_3D,
    COMMUNITY_ARRANGEMENT_COUNT
} CommunityArrangementMode;

/* ============================================================================
 * Core Data Structures
 * ============================================================================ */

typedef struct {
	vec3 position;
	vec3 color;
	float size;
	char *label;
	int degree;
	int coreness;
	float glow;
	float selected;
} Node;

typedef struct {
	float position[3];
} Hub;

typedef struct {
	uint32_t from;
	uint32_t to;
	float size;
	float selected;
	// Animation specific fields
	float animation_progress; // 0.0 to 1.0
	int animation_direction;  // 1 for forward, -1 for backward
	bool is_animating;
	float animation_speed; // speed multiplier for this edge
} Edge;

typedef struct {
	int node_count;
	int edge_count;
	int coreness_filter;
} GraphProperties;

typedef struct {
	Node *nodes;
	uint32_t node_count;
	Edge *edges;
	uint32_t edge_count;

	igraph_t g;
	igraph_matrix_t current_layout;
	bool graph_initialized;
	char *node_attr_name;
	char *edge_attr_name;
	GraphProperties props;
	LayoutType active_layout;
	OpenOrdContext *openord;
	LayeredSphereContext *layered_sphere;
	Hub *hubs;
	int hub_count;
} GraphData;

#endif // GRAPH_TYPES_H
