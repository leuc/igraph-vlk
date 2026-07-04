/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GRAPH_TYPES_H
#define GRAPH_TYPES_H

#include <cglm/cglm.h>
#include <igraph/igraph.h>
#include <stdbool.h>
#include <stdint.h>

/* ============================================================================
 * Enums (defined first as they're used by GraphData)
 * ============================================================================ */

/* ============================================================================
 * Attribute Filtering
 * ============================================================================ */

typedef struct
{
	char *name;
	int num_values;
	char **values;
} FilterableAttr;

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
	float visible;
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
	Hub *hubs;
	int hub_count;

	FilterableAttr *filterable_attrs;
	int num_filterable_attrs;

	bool use_as_seed;
} GraphData;

#endif // GRAPH_TYPES_H
