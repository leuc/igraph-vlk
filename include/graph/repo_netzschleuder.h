/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GRAPH_REPO_NETSCHLEUDER_H
#define GRAPH_REPO_NETSCHLEUDER_H

#include "interaction/state.h"

typedef struct
{
	const char *entry_id;
	const char *title;
	const char *version_id;
	const char *tags;
	int num_nodes;
	int num_edges;
} StaticNetEntry;

// Static catalog accessor
const StaticNetEntry *netzschleuder_static_entries(int *count);

// Network download (runs on background thread)
void *run_netzschleuder_download(ExecutionContext *ctx);
void apply_netzschleuder_download(ExecutionContext *ctx, void *result_data);
void free_netzschleuder_download(void *result_data);

#endif
