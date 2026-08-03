/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GRAPH_COLOR_H
#define GRAPH_COLOR_H

#include "graph/graph_types.h"

#define EMPHASIS_FULL 1.0f
#define EMPHASIS_DIMMED 0.25f

/**
 * Reset node/edge emphasis to full brightness (emphasis = 1.0f).
 * Every apply function that does not itself want to de-emphasize a subset
 * of the graph must call this first, so it never inherits dimming left by
 * a previous Follow command (e.g. Criticality Basket, Maximum Antichain).
 * @param data Pointer to GraphData
 */
void graph_reset_emphasis(GraphData *data);

#endif // GRAPH_COLOR_H
