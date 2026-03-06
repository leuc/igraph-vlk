#ifndef WRAPPERS_PATHS_H
#define WRAPPERS_PATHS_H

#include <igraph.h>
#include <stddef.h>

void poll_info_diameter(igraph_t *graph, char *buffer, size_t max_len);

#endif // WRAPPERS_PATHS_H