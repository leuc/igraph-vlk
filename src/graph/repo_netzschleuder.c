/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Netzschleuder network repository integration.
 * The catalog is built from static C data (no JSON download).
 * Actual network downloads use curl, decompress with zstd, and load via igraph.
 */

#include "graph/repo_netzschleuder.h"
#include "app_state.h"
#include "graph/graph_core.h"
#include "graph/graph_io.h"
#include "graph/repo.h"
#include "graph/worker_thread.h"
#include "os/path.h"
#include "vulkan/renderer.h"
#include "vulkan/renderer_anim.h"
#include <curl/curl.h>
#include <igraph.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zstd.h>

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
// Helpers
// ---------------------------------------------------------------------------

static void ensure_dir(const char *path)
{
	char tmp[4096];
	snprintf(tmp, sizeof(tmp), "%s", path);
	for (char *p = tmp + 1; *p; p++) {
		if (*p == '/') {
			*p = '\0';
			mkdir(tmp, 0755);
			*p = '/';
		}
	}
	mkdir(tmp, 0755);
}

static int download_progress_cb(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
	(void)clientp;
	(void)ultotal;
	(void)ulnow;
	if (dltotal > 0)
		worker_thread_set_progress((float)dlnow / (float)dltotal);
	return 0;
}

static igraph_t *load_graphml_from_zst(const char *path)
{
	// Read compressed file
	FILE *fp = fopen(path, "rb");
	if (!fp) {
		fprintf(stderr, "[Netzschleuder] Cannot open %s\n", path);
		return NULL;
	}
	fseek(fp, 0, SEEK_END);
	long comp_len = ftell(fp);
	rewind(fp);
	if (comp_len <= 0) {
		fclose(fp);
		fprintf(stderr, "[Netzschleuder] Empty file %s\n", path);
		return NULL;
	}
	void *comp = malloc((size_t)comp_len);
	if (!comp) {
		fclose(fp);
		return NULL;
	}
	if (fread(comp, 1, (size_t)comp_len, fp) != (size_t)comp_len) {
		fclose(fp);
		free(comp);
		return NULL;
	}
	fclose(fp);

	// Streaming decompression (handles files without embedded content size)
	ZSTD_DCtx *dctx = ZSTD_createDCtx();
	if (!dctx) {
		free(comp);
		return NULL;
	}

	size_t dst_cap = 1048576; // start at 1 MB
	void *dst = malloc(dst_cap);
	if (!dst) {
		ZSTD_freeDCtx(dctx);
		free(comp);
		return NULL;
	}

	ZSTD_inBuffer in = {comp, (size_t)comp_len, 0};
	size_t total_out = 0;

	while (in.pos < in.size) {
		ZSTD_outBuffer out = {(unsigned char *)dst + total_out, dst_cap - total_out, 0};
		size_t ret = ZSTD_decompressStream(dctx, &out, &in);
		if (ZSTD_isError(ret)) {
			fprintf(stderr, "[Netzschleuder] ZSTD_decompressStream: %s\n", ZSTD_getErrorName(ret));
			ZSTD_freeDCtx(dctx);
			free(dst);
			free(comp);
			return NULL;
		}
		total_out += out.pos;
		if (ret == 0)
			break; // frame fully decoded
		if (in.pos < in.size && out.pos == dst_cap - total_out) {
			// Need larger buffer
			dst_cap *= 2;
			void *ndst = realloc(dst, dst_cap);
			if (!ndst) {
				ZSTD_freeDCtx(dctx);
				free(dst);
				free(comp);
				return NULL;
			}
			dst = ndst;
		}
	}

	ZSTD_freeDCtx(dctx);
	free(comp);

	// Parse GML from decompressed memory
	FILE *mfp = fmemopen(dst, total_out, "r");
	if (!mfp) {
		free(dst);
		return NULL;
	}

	igraph_t *graph = IGRAPH_MALLOC(sizeof(igraph_t));
	if (!graph) {
		fclose(mfp);
		free(dst);
		return NULL;
	}

	igraph_error_handler_t *prev_handler = igraph_set_error_handler(igraph_error_handler_printignore);
	bool ok = graph_read_gml(graph, mfp);
	igraph_set_error_handler(prev_handler);
	fclose(mfp);
	free(dst);

	if (!ok) {
		fprintf(stderr, "[Netzschleuder] graph_read_gml failed for %s\n", path);
		free(graph);
		return NULL;
	}

	worker_thread_set_status_message("Graph loaded");
	return graph;
}

// ---------------------------------------------------------------------------
// Network download + load
// ---------------------------------------------------------------------------

void *run_netzschleuder_download(ExecutionContext *ctx)
{
	const char *entry_id = ctx->params[0].value.str_val;
	const char *version_id = ctx->params[1].value.str_val ? ctx->params[1].value.str_val : entry_id;

	char file_path[4096];
	char url[4096];
	char tmp_path[4096 + 16];
	const char *cache = os_cache_dir("igraph-vlk");

	snprintf(file_path, sizeof(file_path), "%s/netzschleuder/%s/%s.gml.zst", cache, entry_id, version_id);
	snprintf(url, sizeof(url), "https://networks.skewed.de/net/%s/files/%s.gml.zst", entry_id, version_id);

	// Download if not already cached
	struct stat st;
	if (stat(file_path, &st) != 0 || !S_ISREG(st.st_mode)) {
		worker_thread_set_status_message("Downloading...");

		char dir[4096];
		snprintf(dir, sizeof(dir), "%s/netzschleuder/%s", cache, entry_id);
		ensure_dir(dir);

		snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", file_path);

		FILE *fp = fopen(tmp_path, "wb");
		if (!fp) {
			fprintf(stderr, "[Repo] Failed to open %s for writing\n", tmp_path);
			return NULL;
		}

		CURL *curl = curl_easy_init();
		if (!curl) {
			fclose(fp);
			remove(tmp_path);
			return NULL;
		}

		curl_easy_setopt(curl, CURLOPT_URL, url);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, repo_curl_write_cb);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
		curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, download_progress_cb);
		curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
		curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
		curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);
		curl_easy_setopt(curl, CURLOPT_USERAGENT, "igraph-vlk");

		CURLcode res = curl_easy_perform(curl);
		long http_code = 0;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
		curl_easy_cleanup(curl);
		fclose(fp);

		if (res != CURLE_OK || http_code != 200) {
			fprintf(stderr, "[Repo] Download failed for %s: curl=%d http=%ld\n", url, res, http_code);
			remove(tmp_path);
			worker_thread_set_status_message("Download failed");
			return NULL;
		}

		rename(tmp_path, file_path);
		worker_thread_set_status_message("Download complete");
	} else {
		worker_thread_set_status_message("Already cached");
	}

	// Decompress and load graph
	worker_thread_set_status_message("Decompressing...");
	return load_graphml_from_zst(file_path);
}

// ---------------------------------------------------------------------------
// Apply — swap graph on main thread
// ---------------------------------------------------------------------------

void apply_netzschleuder_download(ExecutionContext *ctx, void *result_data)
{
	if (!ctx || !ctx->app_state || !result_data) {
		fprintf(stderr, "[apply_netzschleuder_download] Invalid parameters\n");
		return;
	}

	AppState *state = ctx->app_state;
	GraphData *data = &state->current_graph;
	Renderer *renderer = &state->renderer;
	igraph_t *new_graph = (igraph_t *)result_data;

	if (igraph_vcount(new_graph) == 0) {
		fprintf(stderr, "[apply_netzschleuder_download] Empty graph\n");
		return;
	}

	graph_free_data(data);

	igraph_error_t code = igraph_copy(&data->g, new_graph);
	if (code != IGRAPH_SUCCESS) {
		fprintf(stderr, "[apply_netzschleuder_download] igraph_copy failed\n");
		return;
	}
	data->graph_initialized = true;

	int n = igraph_vcount(&data->g);
	if (n > 0) {
		if (!graph_import_layout_pos(data)) {
			igraph_matrix_t *layout = IGRAPH_MALLOC(sizeof(igraph_matrix_t));
			if (igraph_matrix_init(layout, n, 3) != IGRAPH_SUCCESS) {
				IGRAPH_FREE(layout);
				return;
			}
			igraph_layout_grid_3d(&data->g, layout, 0, 0);
			igraph_matrix_init_copy(&data->current_layout, layout);
			igraph_matrix_destroy(layout);
			IGRAPH_FREE(layout);
		}
	} else {
		igraph_matrix_init(&data->current_layout, 1, 3);
	}

	if (!graph_build_visualization(data)) {
		fprintf(stderr, "[apply_netzschleuder_download] graph_build_visualization failed\n");
		return;
	}
	renderer_update_graph(renderer, data);
	renderer->label.tree_needs_rebuild = true;

	printf("[Netzschleuder] Loaded graph: %d vertices, %d edges\n", data->node_count, data->edge_count);
}

void free_netzschleuder_download(void *result_data)
{
	if (result_data) {
		igraph_destroy((igraph_t *)result_data);
		IGRAPH_FREE(result_data);
	}
}
