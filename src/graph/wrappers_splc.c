#include "graph/wrappers_splc.h"

#include <stdio.h>
#include <stdlib.h>

igraph_integer_t calculate_dag_levels(const igraph_t *graph, igraph_vector_int_t *levels)
{
	igraph_integer_t n = igraph_vcount(graph);
	if (n == 0)
		return -1;

	if (!igraph_is_directed(graph)) {
		fprintf(stderr, "SPLC requires a directed graph\n");
		return -1;
	}

	igraph_bool_t is_dag = false;
	igraph_is_dag(graph, &is_dag);
	if (!is_dag) {
		return -1;
	}

	igraph_vector_int_t topo_order;
	igraph_vector_int_init(&topo_order, 0);

	if (igraph_topological_sorting(graph, &topo_order, IGRAPH_OUT) != IGRAPH_SUCCESS) {
		fprintf(stderr, "Graph is not a DAG (topological sort failed)\n");
		igraph_vector_int_destroy(&topo_order);
		return -1;
	}

	igraph_vector_int_resize(levels, n);
	igraph_vector_int_null(levels);

	igraph_vector_int_t out_neis;
	igraph_vector_int_init(&out_neis, 0);

	igraph_integer_t max_level = 0;

	for (igraph_integer_t i = 0; i < igraph_vector_int_size(&topo_order); i++) {
		igraph_integer_t node = VECTOR(topo_order)[i];
		igraph_integer_t node_level = VECTOR(*levels)[node];

		igraph_vector_int_clear(&out_neis);
		igraph_neighbors(graph, &out_neis, node, IGRAPH_OUT, IGRAPH_LOOPS, IGRAPH_NO_MULTIPLE);

		for (igraph_integer_t j = 0; j < igraph_vector_int_size(&out_neis); j++) {
			igraph_integer_t neighbor = VECTOR(out_neis)[j];
			igraph_integer_t candidate = node_level + 1;
			if (VECTOR(*levels)[neighbor] < candidate) {
				VECTOR(*levels)[neighbor] = candidate;
				if (candidate > max_level)
					max_level = candidate;
			}
		}
	}

	igraph_vector_int_destroy(&out_neis);
	igraph_vector_int_destroy(&topo_order);

	return max_level;
}
