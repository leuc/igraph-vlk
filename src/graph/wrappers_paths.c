#include "graph/wrappers_paths.h"
#include <stdio.h>

void poll_info_diameter(igraph_t *graph, char *buffer, size_t max_len)
{
	if (!graph || !buffer || max_len == 0)
		return;

	if (igraph_vcount(graph) == 0) {
		snprintf(buffer, max_len, "Diameter: N/A (empty graph)");
		return;
	}

	igraph_real_t diam = 0.0;
	igraph_diameter(graph, NULL, &diam, NULL, NULL, NULL, NULL, 0, 1);

	snprintf(buffer, max_len, "Diameter: %d", (int)diam);
}