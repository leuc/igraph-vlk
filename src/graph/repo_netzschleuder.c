/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Netzschleuder network repository integration.
 * The catalog is built from static C data (no JSON download).
 * Actual network downloads use curl.
 */

#include "graph/repo_netzschleuder.h"
#include "graph/repo.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Static catalog entries (auto-generated from netzschleuder.json, see
//   the curl+jq command at the top of netzschleuder_data.inc)
// ---------------------------------------------------------------------------

static const StaticNetEntry g_static_nets[] = {
#include "netzschleuder_data.inc"
};

static const int g_static_nets_count = sizeof(g_static_nets) / sizeof(g_static_nets[0]);

const StaticNetEntry *netzschleuder_static_entries(int *count)
{
	if (count)
		*count = g_static_nets_count;
	return g_static_nets;
}

// ---------------------------------------------------------------------------
// Network download (stub)
// ---------------------------------------------------------------------------

void *run_netzschleuder_download(ExecutionContext *ctx)
{
	const char *entry_id = ctx->params[0].value.str_val;
	const char *version_id = ctx->params[1].value.str_val ? ctx->params[1].value.str_val : entry_id;

	printf("[Netzschleuder] Download requested: entry_id=%s, version_id=%s\n", entry_id, version_id);

	// TODO: implement actual download
	// URL: https://networks.skewed.de/net/{entry_id}/files/{version_id}.xml.zst
	return NULL;
}

void apply_netzschleuder_download(ExecutionContext *ctx, void *result_data)
{
	(void)ctx;
	(void)result_data;
}

void free_netzschleuder_download(void *result_data)
{
	free(result_data);
}
