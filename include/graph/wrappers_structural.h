/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GRAPH_WRAPPERS_STRUCTURAL_H
#define GRAPH_WRAPPERS_STRUCTURAL_H

#include "interaction/state.h"
#include <igraph.h>

// Summary properties card
void *compute_graph_properties(ExecutionContext *ctx);

// Global network properties: density, transitivity, assortativity

void *compute_igraph_density(ExecutionContext *ctx);
void *compute_igraph_transitivity_undirected(ExecutionContext *ctx);
void *compute_igraph_assortativity_degree(ExecutionContext *ctx);

#endif
