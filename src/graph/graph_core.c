/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#define _GNU_SOURCE
#include "graph/graph_core.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "graph/wrappers_layout.h"

// ============================================================================
// Build the entire visualization arrays for a new graph.
// Caller intention: "I have a completely new graph — build everything."
// ============================================================================
bool graph_build_visualization(GraphData *data)
{
	uint32_t old_node_count = data->node_count;

	data->node_count = igraph_vcount(&data->g);
	data->edge_count = igraph_ecount(&data->g);
	data->props.node_count = (int)data->node_count;
	data->props.edge_count = (int)data->edge_count;

	// Free old arrays
	if (data->nodes) {
		for (uint32_t i = 0; i < old_node_count; i++)
			if (data->nodes[i].label)
				free(data->nodes[i].label);
		free(data->nodes);
	}
	if (data->edges)
		free(data->edges);

	// Build nodes
	data->nodes = malloc(sizeof(Node) * data->node_count);
	if (!data->nodes && data->node_count > 0)
		return false;
	bool has_node_attr = data->node_attr_name && igraph_cattribute_has_attr(&data->g, IGRAPH_ATTRIBUTE_VERTEX, data->node_attr_name);
	bool has_label = igraph_cattribute_has_attr(&data->g, IGRAPH_ATTRIBUTE_VERTEX, "label");
	float max_n_val = 0.0f;
	if (has_node_attr) {
		for (int i = 0; i < data->node_count; i++) {
			float val = (float)VAN(&data->g, data->node_attr_name, i);
			if (val > max_n_val)
				max_n_val = val;
		}
	}

	for (int i = 0; i < data->node_count; i++) {
		data->nodes[i].color[0] = (float)rand() / RAND_MAX;
		data->nodes[i].color[1] = (float)rand() / RAND_MAX;
		data->nodes[i].color[2] = (float)rand() / RAND_MAX;
		data->nodes[i].size = (has_node_attr && max_n_val > 0) ? (float)VAN(&data->g, data->node_attr_name, i) / max_n_val : 1.0f;
		data->nodes[i].label = has_label ? strdup(VAS(&data->g, "label", i)) : NULL;
		data->nodes[i].visible = 1.0f;
	}
	igraph_vector_int_t degrees;
	if (igraph_vector_int_init(&degrees, data->node_count) == IGRAPH_SUCCESS) {
		igraph_degree(&data->g, &degrees, igraph_vss_all(), IGRAPH_ALL, IGRAPH_LOOPS);
		for (int i = 0; i < data->node_count; i++) {
			data->nodes[i].degree = VECTOR(degrees)[i];
		}
		igraph_vector_int_destroy(&degrees);
	} else {
		for (int i = 0; i < data->node_count; i++)
			data->nodes[i].degree = 0;
	}

	// Sync node positions from layout matrix
	if (data->nodes) {
		for (int i = 0; i < data->node_count; i++) {
			data->nodes[i].position[0] = (float)MATRIX(data->current_layout, i, 0);
			data->nodes[i].position[1] = (float)MATRIX(data->current_layout, i, 1);
			data->nodes[i].position[2] = (igraph_matrix_ncol(&data->current_layout) > 2) ? (float)MATRIX(data->current_layout, i, 2) : 0.0f;
		}
	}

	// Analyze filterable vertex attributes (low-cardinality string/boolean only)
	if (data->filterable_attrs) {
		for (int a = 0; a < data->num_filterable_attrs; a++) {
			free(data->filterable_attrs[a].name);
			for (int v = 0; v < data->filterable_attrs[a].num_values; v++)
				free(data->filterable_attrs[a].values[v]);
			free(data->filterable_attrs[a].values);
		}
		free(data->filterable_attrs);
		data->filterable_attrs = NULL;
		data->num_filterable_attrs = 0;
	}
	{
		igraph_error_handler_t *prev_handler = igraph_set_error_handler(igraph_error_handler_printignore);
		igraph_strvector_t vnames;
		igraph_vector_int_t vtypes;
		bool vnames_ok = igraph_strvector_init(&vnames, 0) == IGRAPH_SUCCESS;
		bool vtypes_ok = igraph_vector_int_init(&vtypes, 0) == IGRAPH_SUCCESS;
		if (!vnames_ok || !vtypes_ok) {
			if (vnames_ok)
				igraph_strvector_destroy(&vnames);
			if (vtypes_ok)
				igraph_vector_int_destroy(&vtypes);
			igraph_set_error_handler(prev_handler);
		} else {
			igraph_error_t verr = igraph_cattribute_list(&data->g, NULL, NULL, &vnames, &vtypes, NULL, NULL);
			if (verr == IGRAPH_SUCCESS) {
				int n_attrs = igraph_strvector_size(&vnames);
				printf("[Filter] igraph_cattribute_list returned %d vertex attributes\n", n_attrs);
				for (int a = 0; a < n_attrs; a++) {
					const char *name = igraph_strvector_get(&vnames, a);
					if (!name)
						continue;
					igraph_attribute_type_t atype = (igraph_attribute_type_t)VECTOR(vtypes)[a];
					const char *type_str = (atype == IGRAPH_ATTRIBUTE_STRING) ? "string" : (atype == IGRAPH_ATTRIBUTE_BOOLEAN) ? "boolean" : "numeric";
					if (atype != IGRAPH_ATTRIBUTE_STRING && atype != IGRAPH_ATTRIBUTE_BOOLEAN) {
						printf("[Filter]   %s: %s (skipped - not string/boolean)\n", name, type_str);
						continue;
					}
					// Collect distinct values, check every node has a value
					char **values = NULL;
					int num_values = 0;
					int max_values = 20;
					bool all_present = true;
					for (int i = 0; i < data->node_count; i++) {
						const char *val = NULL;
						if (atype == IGRAPH_ATTRIBUTE_STRING) {
							val = igraph_cattribute_VAS(&data->g, name, i);
							if (!val)
								all_present = false;
						} else {
							// Boolean — skip if any node lacks the attribute
							if (!igraph_cattribute_has_attr(&data->g, IGRAPH_ATTRIBUTE_VERTEX, name)) {
								all_present = false;
								break;
							}
							bool bv = igraph_cattribute_VAB(&data->g, name, i);
							val = bv ? "true" : "false";
						}
						// Deduplicate
						bool found = false;
						for (int v = 0; v < num_values; v++) {
							if (strcmp(values[v], val) == 0) {
								found = true;
								break;
							}
						}
						if (!found) {
							if (num_values >= max_values) {
								all_present = false;
								break;
							}
							char **tmp = realloc(values, sizeof(char *) * (num_values + 1));
							if (!tmp) {
								all_present = false;
								break;
							}
							values = tmp;
							values[num_values] = strdup(val);
							num_values++;
						}
					}
					if (all_present && num_values > 1 && num_values <= max_values) {
						printf("[Filter]   %s: %d distinct values (accepted)\n", name, num_values);
						FilterableAttr *tmp = realloc(data->filterable_attrs, sizeof(FilterableAttr) * (data->num_filterable_attrs + 1));
						if (!tmp) {
							for (int v = 0; v < num_values; v++)
								free(values[v]);
							free(values);
							continue;
						}
						data->filterable_attrs = tmp;
						data->filterable_attrs[data->num_filterable_attrs].name = strdup(name);
						data->filterable_attrs[data->num_filterable_attrs].values = values;
						data->filterable_attrs[data->num_filterable_attrs].num_values = num_values;
						data->num_filterable_attrs++;
					} else {
						printf("[Filter]   %s: rejected (all_present=%d, num_values=%d)\n", name, all_present, num_values);
						for (int v = 0; v < num_values; v++)
							free(values[v]);
						free(values);
					}
				}
			}
			igraph_strvector_destroy(&vnames);
			igraph_vector_int_destroy(&vtypes);
			igraph_set_error_handler(prev_handler);
		}
	}

	if (data->num_filterable_attrs > 0) {
		printf("[Filter] %d filterable attribute(s) detected:\n", data->num_filterable_attrs);
		for (int a = 0; a < data->num_filterable_attrs; a++) {
			printf("[Filter]   %s (%d values):", data->filterable_attrs[a].name, data->filterable_attrs[a].num_values);
			for (int v = 0; v < data->filterable_attrs[a].num_values; v++)
				printf(" \"%s\"", data->filterable_attrs[a].values[v]);
			printf("\n");
		}
	} else {
		printf("[Filter] No filterable attributes found\n");
	}

	// Build edges
	data->edges = malloc(sizeof(Edge) * data->edge_count);
	if (!data->edges && data->edge_count > 0)
		return false;
	bool has_weight = igraph_cattribute_has_attr(&data->g, IGRAPH_ATTRIBUTE_EDGE, "weight");
	for (int i = 0; i < data->edge_count; i++) {
		igraph_integer_t from, to;
		igraph_edge(&data->g, i, &from, &to);
		data->edges[i].from = (uint32_t)from;
		data->edges[i].to = (uint32_t)to;
		data->edges[i].weight = has_weight ? (float)EAN(&data->g, "weight", i) : 0.0f;
	}
	return true;
}

// ============================================================================
// Rebuild only the edge array and node degrees after in-place edge changes.
// Caller intention: "Edges changed — update edges and degrees, leave nodes alone."
// ============================================================================
bool graph_rebuild_edges(GraphData *data)
{
	data->edge_count = igraph_ecount(&data->g);
	data->props.edge_count = (int)data->edge_count;

	if (data->edges)
		free(data->edges);

	data->edges = malloc(sizeof(Edge) * data->edge_count);
	if (!data->edges && data->edge_count > 0)
		return false;
	bool has_weight = igraph_cattribute_has_attr(&data->g, IGRAPH_ATTRIBUTE_EDGE, "weight");
	for (int i = 0; i < data->edge_count; i++) {
		igraph_integer_t from, to;
		igraph_edge(&data->g, i, &from, &to);
		data->edges[i].from = (uint32_t)from;
		data->edges[i].to = (uint32_t)to;
		data->edges[i].weight = has_weight ? (float)EAN(&data->g, "weight", i) : 0.0f;
	}

	// Recompute degree on existing nodes — it affects node shape rendering
	igraph_vector_int_t degrees;
	if (igraph_vector_int_init(&degrees, data->node_count) == IGRAPH_SUCCESS) {
		igraph_degree(&data->g, &degrees, igraph_vss_all(), IGRAPH_ALL, IGRAPH_LOOPS);
		for (uint32_t i = 0; i < data->node_count; i++) {
			data->nodes[i].degree = VECTOR(degrees)[i];
		}
		igraph_vector_int_destroy(&degrees);
	}
	return true;
}

// ============================================================================
// Free all graph data
// ============================================================================
void graph_free_data(GraphData *data)
{
	if (data->graph_initialized) {
		igraph_destroy(&data->g);
		igraph_matrix_destroy(&data->current_layout);
		data->graph_initialized = false;
	}
	if (data->node_attr_name) {
		free(data->node_attr_name);
		data->node_attr_name = NULL;
	}
	if (data->hubs) {
		free(data->hubs);
		data->hubs = NULL;
	}
	if (data->filterable_attrs) {
		for (int a = 0; a < data->num_filterable_attrs; a++) {
			free(data->filterable_attrs[a].name);
			for (int v = 0; v < data->filterable_attrs[a].num_values; v++)
				free(data->filterable_attrs[a].values[v]);
			free(data->filterable_attrs[a].values);
		}
		free(data->filterable_attrs);
		data->filterable_attrs = NULL;
		data->num_filterable_attrs = 0;
	}
	if (data->nodes) {
		for (uint32_t i = 0; i < data->node_count; i++)
			if (data->nodes[i].label)
				free(data->nodes[i].label);
		free(data->nodes);
		data->nodes = NULL;
	}
	if (data->edges) {
		free(data->edges);
		data->edges = NULL;
	}
}

// ============================================================================
// Import node positions from _pos vertex attribute (GraphML/GML convention)
// Returns true if _pos was found and current_layout was populated.
// ============================================================================
bool graph_import_layout_pos(GraphData *data)
{
	if (!igraph_cattribute_has_attr(&data->g, IGRAPH_ATTRIBUTE_VERTEX, "_pos"))
		return false;

	igraph_integer_t n = igraph_vcount(&data->g);
	if (n == 0)
		return false;

	igraph_matrix_init(&data->current_layout, n, 3);
	for (igraph_integer_t i = 0; i < n; i++) {
		const char *s = VAS(&data->g, "_pos", i);
		double x = 0.0, y = 0.0, z = 0.0;
		if (s)
			sscanf(s, "%lf, %lf, %lf", &x, &y, &z);
		MATRIX(data->current_layout, i, 0) = x;
		MATRIX(data->current_layout, i, 1) = y;
		MATRIX(data->current_layout, i, 2) = z;
	}
	layout_center_and_autoscale(&data->current_layout);
	return true;
}

// ============================================================================
// Build an edge-id-ordered weight vector from the igraph "weight" edge attribute
// ============================================================================
bool graph_build_edge_weights(const igraph_t *graph, igraph_vector_t *out_weights)
{
	if (!igraph_cattribute_has_attr(graph, IGRAPH_ATTRIBUTE_EDGE, "weight"))
		return false;
	if (igraph_vector_init(out_weights, igraph_ecount(graph)) != IGRAPH_SUCCESS)
		return false;
	if (igraph_cattribute_EANV(graph, "weight", igraph_ess_all(IGRAPH_EDGEORDER_ID), out_weights) != IGRAPH_SUCCESS) {
		igraph_vector_destroy(out_weights);
		return false;
	}
	return true;
}
