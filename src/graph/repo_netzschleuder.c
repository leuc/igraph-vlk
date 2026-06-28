/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "graph/repo_netzschleuder.h"
#include "graph/repo.h"
#include "graph/worker_thread.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NETZSCHLEUDER_URL "https://networks.skewed.de/api/nets?full=True"

static int download_progress_cb(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
	(void)clientp;
	(void)ultotal;
	(void)ulnow;
	if (dltotal > 0) {
		float p = (float)dlnow / (float)dltotal;
		worker_thread_set_progress(p);
		worker_thread_set_status_message("Downloading netzschleuder catalog");
	}
	return 0;
}

void *netzschleuder_refresh(igraph_t *graph)
{
	(void)graph;

	const char *dir = repo_cache_dir();
	char path[4096];
	snprintf(path, sizeof(path), "%s/netzschleuder.json", dir);

	FILE *fp = fopen(path, "wb");
	if (!fp) {
		fprintf(stderr, "[Repo] Failed to open %s for writing\n", path);
		return NULL;
	}

	CURL *curl = curl_easy_init();
	if (!curl) {
		fclose(fp);
		fprintf(stderr, "[Repo] curl_easy_init failed\n");
		return NULL;
	}

	worker_thread_set_status_message("Downloading netzschleuder catalog...");

	curl_easy_setopt(curl, CURLOPT_URL, NETZSCHLEUDER_URL);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, repo_curl_write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, download_progress_cb);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);

	CURLcode res = curl_easy_perform(curl);

	curl_easy_cleanup(curl);
	fclose(fp);

	if (res != CURLE_OK) {
		fprintf(stderr, "[Repo] Download failed: %s\n", curl_easy_strerror(res));
		remove(path);
		worker_thread_set_status_message("Download failed");
		return NULL;
	}

	worker_thread_set_progress(1.0f);
	worker_thread_set_status_message("Netzschleuder catalog updated");

	char *ok = malloc(1);
	if (ok)
		ok[0] = '\0';
	return ok;
}

void netzschleuder_refresh_apply(ExecutionContext *ctx, void *result_data)
{
	(void)ctx;
	(void)result_data;
}

void netzschleuder_refresh_free(void *result_data)
{
	free(result_data);
}
