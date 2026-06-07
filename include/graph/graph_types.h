#ifndef GRAPH_TYPES_H
#define GRAPH_TYPES_H

#include <cglm/cglm.h>
#include <igraph/igraph.h>
#include <stdbool.h>
#include <stdint.h>

/* ============================================================================
 * Enums (defined first as they're used by GraphData)
 * ============================================================================ */

/* Layout Type Enum */
typedef enum { LAYOUT_FR_3D, LAYOUT_KK_3D, LAYOUT_RANDOM_3D, LAYOUT_SPHERE, LAYOUT_GRID_3D, LAYOUT_UMAP_3D, LAYOUT_DRL_3D } LayoutType;

/* ============================================================================
 * Core Data Structures
 * ============================================================================ */

typedef struct
{
	vec3 position;
	vec3 color;
	float size;
	char *label;
	int degree;
	float selected;
} Node;

typedef struct
{
	float position[3];
} Hub;

typedef struct
{
	uint32_t from;
	uint32_t to;
	float selected;
	float weight;
} Edge;

typedef struct
{
	int node_count;
	int edge_count;
	int coreness_filter;
} GraphProperties;

typedef struct
{
	Node *nodes;
	uint32_t node_count;
	Edge *edges;
	uint32_t edge_count;

	igraph_t g;
	igraph_matrix_t current_layout;
	bool graph_initialized;
	char *node_attr_name;
	GraphProperties props;
	LayoutType active_layout;
	Hub *hubs;
	int hub_count;
} GraphData;

#endif // GRAPH_TYPES_H
