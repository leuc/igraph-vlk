/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "graph/graph_filter_visibility.h"

#include <igraph.h>
#include <stdio.h>
#include <string.h>

void graph_filter_reset_visibility(GraphData *data)
{
	if (!data || !data->nodes)
		return;
	for (uint32_t i = 0; i < data->node_count; i++) {
		data->nodes[i].visible = 1.0f;
	}
}

void graph_filter_by_attribute(GraphData *data, const char *attr_name, const char *attr_value)
{
	if (!data || !data->nodes || !attr_name || !attr_value)
		return;

	// Check that the attribute exists
	if (!igraph_cattribute_has_attr(&data->g, IGRAPH_ATTRIBUTE_VERTEX, attr_name)) {
		fprintf(stderr, "graph_filter_by_attribute: attribute '%s' not found\n", attr_name);
		return;
	}

	// Determine attribute type
	igraph_strvector_t vnames;
	igraph_vector_int_t vtypes;
	igraph_strvector_init(&vnames, 0);
	igraph_vector_int_init(&vtypes, 0);
	igraph_attribute_type_t attr_type = IGRAPH_ATTRIBUTE_NUMERIC;
	igraph_error_handler_t *prev = igraph_set_error_handler(igraph_error_handler_printignore);
	igraph_error_t err = igraph_cattribute_list(&data->g, NULL, NULL, &vnames, &vtypes, NULL, NULL);
	if (err == IGRAPH_SUCCESS) {
		for (int i = 0; i < igraph_strvector_size(&vnames); i++) {
			if (strcmp(igraph_strvector_get(&vnames, i), attr_name) == 0) {
				attr_type = (igraph_attribute_type_t)VECTOR(vtypes)[i];
				break;
			}
		}
	}
	igraph_set_error_handler(prev);
	igraph_strvector_destroy(&vnames);
	igraph_vector_int_destroy(&vtypes);

	// Set all nodes hidden first, then reveal matches
	for (uint32_t i = 0; i < data->node_count; i++) {
		data->nodes[i].visible = 0.0f;
	}

	if (attr_type == IGRAPH_ATTRIBUTE_STRING) {
		for (uint32_t i = 0; i < data->node_count; i++) {
			const char *val = igraph_cattribute_VAS(&data->g, attr_name, i);
			if (val && strcmp(val, attr_value) == 0) {
				data->nodes[i].visible = 1.0f;
			}
		}
	} else if (attr_type == IGRAPH_ATTRIBUTE_BOOLEAN) {
		bool match = (strcmp(attr_value, "true") == 0);
		for (uint32_t i = 0; i < data->node_count; i++) {
			bool val = igraph_cattribute_VAB(&data->g, attr_name, i);
			if (val == match) {
				data->nodes[i].visible = 1.0f;
			}
		}
	}
}
