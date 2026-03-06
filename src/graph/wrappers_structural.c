#include "graph/wrappers_structural.h"
#include "app_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *compute_ana_glob_dens(igraph_t *graph)
{
	InfoCardData *data = (InfoCardData *)malloc(sizeof(InfoCardData));
	if (!data)
		return NULL;
	memset(data, 0, sizeof(InfoCardData));
	strncpy(data->title, "Density Results", sizeof(data->title) - 1);

	if (graph && igraph_vcount(graph) > 0) {
		igraph_real_t density = 0.0;

		if (igraph_density(graph, NULL, &density, false) != IGRAPH_SUCCESS) {
			free(data);
			return NULL;
		}

		data->num_pairs = 1;
		strncpy(data->pairs[0].key, "Density", 31);
		snprintf(data->pairs[0].value, 63, "%.4f", density);
	}

	return data;
}

void *compute_ana_glob_trans(igraph_t *graph)
{
	InfoCardData *data = (InfoCardData *)malloc(sizeof(InfoCardData));
	if (!data)
		return NULL;
	memset(data, 0, sizeof(InfoCardData));
	strncpy(data->title, "Transitivity Results", sizeof(data->title) - 1);

	if (graph && igraph_vcount(graph) > 0) {
		igraph_real_t trans = 0.0;

		if (igraph_transitivity_undirected(graph, &trans, IGRAPH_TRANSITIVITY_NAN) != IGRAPH_SUCCESS) {
			free(data);
			return NULL;
		}

		data->num_pairs = 1;
		strncpy(data->pairs[0].key, "Transitivity (undirected)", 31);
		snprintf(data->pairs[0].value, 63, "%.4f", trans);
	}

	return data;
}

void *compute_ana_glob_assort(igraph_t *graph)
{
	InfoCardData *data = (InfoCardData *)malloc(sizeof(InfoCardData));
	if (!data)
		return NULL;
	memset(data, 0, sizeof(InfoCardData));
	strncpy(data->title, "Assortativity Results", sizeof(data->title) - 1);

	if (graph && igraph_vcount(graph) > 0) {
		igraph_real_t assort = 0.0;
		igraph_bool_t directed = igraph_is_directed(graph);

		if (igraph_assortativity_degree(graph, &assort, directed) != IGRAPH_SUCCESS) {
			free(data);
			return NULL;
		}

		data->num_pairs = 1;
		strncpy(data->pairs[0].key, "Degree Assortativity", 31);
		snprintf(data->pairs[0].value, 63, "%.4f", assort);
	}

	return data;
}
