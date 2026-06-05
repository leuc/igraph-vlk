#define _GNU_SOURCE
#include "graph/graph_clustering.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "graph/graph_core.h"
#include "graph/graph_types.h"

// Helper structure for community arrangement
typedef struct
{
	int start_idx;
	int end_idx;
} CommBlock;

void graph_cluster(GraphData *data, ClusterType type)
{
	igraph_vector_int_t membership;
	igraph_vector_int_init(&membership, igraph_vcount(&data->g));
	igraph_vector_t weights_vec;
	igraph_vector_t node_weights_vec;
	bool use_weights = (data->edges != NULL && data->edge_attr_name != NULL && strcmp(data->edge_attr_name, "") != 0);
	if (use_weights) {
		igraph_vector_init(&weights_vec, data->edge_count);
		for (int i = 0; i < data->edge_count; i++) {
			VECTOR(weights_vec)[i] = data->edges[i].size;
		}

		igraph_vector_init(&node_weights_vec, data->node_count);
		igraph_vector_int_t degrees;
		igraph_vector_int_init(&degrees, data->node_count);
		igraph_degree(&data->g, &degrees, igraph_vss_all(), IGRAPH_ALL, IGRAPH_LOOPS);
		for (int i = 0; i < data->node_count; i++) {
			VECTOR(node_weights_vec)[i] = (igraph_real_t)VECTOR(degrees)[i];
		}
		igraph_vector_int_destroy(&degrees);
	}
	const igraph_vector_t *weights_ptr = use_weights ? &weights_vec : NULL;
	const igraph_vector_t *node_weights_ptr = use_weights ? &node_weights_vec : NULL;

	switch (type) {
	case CLUSTER_FASTGREEDY: {
		igraph_matrix_int_t m;
		igraph_vector_t mo;
		igraph_matrix_int_init(&m, 0, 0);
		igraph_vector_init(&mo, 0);
		igraph_community_fastgreedy(&data->g, weights_ptr, &m, &mo, &membership);
		igraph_matrix_int_destroy(&m);
		igraph_vector_destroy(&mo);
		break;
	}
	case CLUSTER_WALKTRAP: {
		igraph_matrix_int_t m;
		igraph_vector_t mo;
		igraph_matrix_int_init(&m, 0, 0);
		igraph_vector_init(&mo, 0);
		igraph_community_walktrap(&data->g, weights_ptr, 4, &m, &mo, &membership);
		igraph_matrix_int_destroy(&m);
		igraph_vector_destroy(&mo);
		break;
	}
	case CLUSTER_LABEL_PROP:
		igraph_community_label_propagation(&data->g, &membership, IGRAPH_ALL, weights_ptr, NULL, NULL, IGRAPH_LPA_FAST);
		break;
	case CLUSTER_MULTILEVEL: {
		igraph_vector_t mo;
		igraph_vector_init(&mo, 0);
		igraph_community_multilevel(&data->g, weights_ptr, 1.0, &membership, NULL, &mo);
		igraph_vector_destroy(&mo);
		break;
	}
	case CLUSTER_LEIDEN: {
		// Use leiden_simple for undirected graphs to avoid vertex weight errors
		igraph_int_t nb_clusters;
		igraph_real_t quality;
		if (igraph_is_directed(&data->g)) {
			// For directed graphs, use full leiden with weights
			igraph_community_leiden(&data->g, weights_ptr, node_weights_ptr, node_weights_ptr, 1.0 / (2.0 * data->edge_count), 0.01, 1, 100, &membership, &nb_clusters, &quality);
		} else {
			// For undirected graphs, use leiden_simple without vertex weights
			igraph_community_leiden_simple(&data->g, weights_ptr, IGRAPH_LEIDEN_OBJECTIVE_CPM, 1.0 / (2.0 * data->edge_count), 0.01, 1, 100, &membership, &nb_clusters, &quality);
		}
	} break;
	default:
		break;
	}
	int cluster_count = 0;
	for (int i = 0; i < igraph_vector_int_size(&membership); i++)
		if (VECTOR(membership)[i] > cluster_count)
			cluster_count = VECTOR(membership)[i];
	cluster_count++;
	int *cluster_sizes = calloc(cluster_count, sizeof(int));
	for (int i = 0; i < data->node_count; i++)
		cluster_sizes[VECTOR(membership)[i]]++;
	int max_cluster_size = 0;
	for (int i = 0; i < cluster_count; i++)
		if (cluster_sizes[i] > max_cluster_size)
			max_cluster_size = cluster_sizes[i];

	vec3 *colors = malloc(sizeof(vec3) * cluster_count);
	for (int i = 0; i < cluster_count; i++) {
		colors[i][0] = (float)rand() / RAND_MAX;
		colors[i][1] = (float)rand() / RAND_MAX;
		colors[i][2] = (float)rand() / RAND_MAX;
	}
	for (int i = 0; i < data->node_count; i++) {
		int c_idx = VECTOR(membership)[i];
		memcpy(data->nodes[i].color, colors[c_idx], 12);
		data->nodes[i].glow = (max_cluster_size > 0) ? (float)cluster_sizes[c_idx] / (float)max_cluster_size : 0.0f;
	}
	if (use_weights) {
		igraph_vector_destroy(&node_weights_vec);
		igraph_vector_destroy(&weights_vec);
	}
	free(colors);
	free(cluster_sizes);
	igraph_vector_int_destroy(&membership);
}
