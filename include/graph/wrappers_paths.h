#ifndef WRAPPERS_PATHS_H
#define WRAPPERS_PATHS_H

#include "interaction/state.h"
#include <igraph.h>

void *compute_ana_diameter(igraph_t *graph);
void *compute_ana_glob_rad(igraph_t *graph);
void *compute_ana_glob_apl(igraph_t *graph);
void apply_info_card(ExecutionContext *ctx, void *result_data);
void free_info_card(void *result_data);

#endif
