#define _GNU_SOURCE
#include "graph/graph_analysis.h"

#include <stdio.h>
#include <stdlib.h>

#include "graph/graph_core.h"

void graph_calculate_centrality(GraphData *data, CentralityType type) {
	igraph_vector_t result;
	igraph_vector_init(&result, igraph_vcount(&data->g));
	switch (type) {
	case CENTRALITY_PAGERANK:
		igraph_pagerank(&data->g, IGRAPH_PAGERANK_ALGO_PRPACK, &result, NULL,
						igraph_vss_all(), IGRAPH_DIRECTED, 0.85, NULL, NULL);
		break;
	case CENTRALITY_HUB: {
		igraph_vector_t a;
		igraph_vector_init(&a, 0);
		igraph_hub_and_authority_scores(&data->g, &result, &a, NULL, true, NULL,
										NULL);
		igraph_vector_destroy(&a);
		break;
	}
	case CENTRALITY_AUTHORITY: {
		igraph_vector_t h;
		igraph_vector_init(&h, 0);
		igraph_hub_and_authority_scores(&data->g, &h, &result, NULL, true, NULL,
										NULL);
		igraph_vector_destroy(&h);
		break;
	}
	case CENTRALITY_BETWEENNESS:
		igraph_betweenness(&data->g, &result, igraph_vss_all(), IGRAPH_DIRECTED,
						   NULL);
		break;
	case CENTRALITY_DEGREE: {
		igraph_vector_int_t d;
		igraph_vector_int_init(&d, 0);
		igraph_degree(&data->g, &d, igraph_vss_all(), IGRAPH_ALL, IGRAPH_LOOPS);
		for (int i = 0; i < igraph_vector_int_size(&d); i++)
			VECTOR(result)[i] = (float)VECTOR(d)[i];
		igraph_vector_int_destroy(&d);
		break;
	}
	case CENTRALITY_CLOSENESS: {
		igraph_bool_t all_reach;
		igraph_closeness(&data->g, &result, NULL, &all_reach, igraph_vss_all(),
						 IGRAPH_ALL, NULL, true);
		break;
	}
	case CENTRALITY_HARMONIC:
		igraph_harmonic_centrality(&data->g, &result, igraph_vss_all(),
								   IGRAPH_ALL, NULL, true);
		break;
	case CENTRALITY_EIGENVECTOR:
		igraph_eigenvector_centrality(&data->g, &result, NULL, IGRAPH_DIRECTED,
									  true, NULL, NULL);
		break;
	case CENTRALITY_STRENGTH:
		igraph_strength(&data->g, &result, igraph_vss_all(), IGRAPH_ALL,
						IGRAPH_LOOPS, NULL);
		break;
	case CENTRALITY_CONSTRAINT:
		igraph_constraint(&data->g, &result, igraph_vss_all(), NULL);
		break;
	default:
		break;
	}
	igraph_real_t min_v, max_v;
	igraph_vector_minmax(&result, &min_v, &max_v);
	for (int i = 0; i < data->node_count; i++) {
		float val = (float)VECTOR(result)[i];
		data->nodes[i].size = (max_v > 0) ? (val / (float)max_v) : 1.0f;
		data->nodes[i].degree = (int)(data->nodes[i].size * 20.0f);
		data->nodes[i].glow = (max_v > 0) ? (val / (float)max_v) : 0.0f;
	}
	igraph_vector_destroy(&result);
}
