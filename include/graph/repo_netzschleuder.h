/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GRAPH_REPO_NETZSCHLEUDER_H
#define GRAPH_REPO_NETZSCHLEUDER_H

#include "graph/repo.h"
#include "interaction/state.h"
#include <igraph.h>

// Download worker (runs on background thread)
void *netzschleuder_refresh(igraph_t *graph);
void netzschleuder_refresh_apply(ExecutionContext *ctx, void *result_data);
void netzschleuder_refresh_free(void *result_data);

// Catalog parsing
NetzschleuderCatalog *netzschleuder_catalog_load(void);
void netzschleuder_catalog_free(NetzschleuderCatalog *cat);

#endif
