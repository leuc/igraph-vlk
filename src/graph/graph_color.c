/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "graph/graph_color.h"

void graph_reset_emphasis(GraphData *data)
{
	if (!data)
		return;
	for (uint32_t i = 0; i < data->node_count; i++)
		data->nodes[i].emphasis = EMPHASIS_FULL;
	for (uint32_t i = 0; i < data->edge_count; i++)
		data->edges[i].emphasis = EMPHASIS_FULL;
}
