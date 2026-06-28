/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GRAPH_REPO_NETZSCHLEUDER_H
#define GRAPH_REPO_NETZSCHLEUDER_H

#include "interaction/state.h"
#include <igraph.h>

void *netzschleuder_refresh(igraph_t *graph);
void netzschleuder_refresh_apply(ExecutionContext *ctx, void *result_data);
void netzschleuder_refresh_free(void *result_data);

#endif
