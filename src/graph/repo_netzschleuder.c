/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "graph/repo_netzschleuder.h"
#include "graph/repo.h"
#include "graph/worker_thread.h"
#include <cjson/cJSON.h>
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NETZSCHLEUDER_URL "https://networks.skewed.de/api/nets?full=True"

// ---------------------------------------------------------------------------
// Download worker
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Catalog parser
// ---------------------------------------------------------------------------

static int count_json_items(cJSON *json)
{
	int n = 0;
	cJSON *child = json->child;
	while (child) {
		n++;
		child = child->next;
	}
	return n;
}

static char **parse_string_array(cJSON *arr, int *out_count)
{
	int count = cJSON_GetArraySize(arr);
	char **result = malloc(sizeof(char *) * count);
	for (int i = 0; i < count; i++) {
		cJSON *item = cJSON_GetArrayItem(arr, i);
		result[i] = strdup(item->valuestring);
	}
	*out_count = count;
	return result;
}

// ---------------------------------------------------------------------------
// Tag index
// ---------------------------------------------------------------------------

static int cmp_str(const void *a, const void *b)
{
	return strcmp(*(const char **)a, *(const char **)b);
}

static int cmp_tag(const void *a, const void *b)
{
	return strcmp(((const NetzschleuderTag *)a)->name, ((const NetzschleuderTag *)b)->name);
}

static void build_tag_index(NetzschleuderCatalog *cat)
{
	// Count total tag references across all entries
	int total = 0;
	for (int i = 0; i < cat->num_entries; i++)
		total += cat->entries[i].num_tags;

	if (total == 0)
		return;

	// Collect all tag strings into a flat array for dedup
	char **all_tags = malloc(sizeof(char *) * total);
	int n = 0;
	for (int i = 0; i < cat->num_entries; i++)
		for (int j = 0; j < cat->entries[i].num_tags; j++)
			all_tags[n++] = cat->entries[i].tags[j];

	// Sort and deduplicate
	qsort(all_tags, n, sizeof(char *), cmp_str);

	int num_unique = 0;
	for (int i = 0; i < n; i++) {
		if (i == 0 || strcmp(all_tags[i], all_tags[i - 1]) != 0)
			num_unique++;
	}

	NetzschleuderTag *tags = calloc(num_unique, sizeof(NetzschleuderTag));

	// Fill unique names
	int t = 0;
	for (int i = 0; i < n; i++) {
		if (i == 0 || strcmp(all_tags[i], all_tags[i - 1]) != 0) {
			tags[t].name = strdup(all_tags[i]);
			tags[t].num_entries = 0;
			t++;
		}
	}

	// Count entries per tag
	for (int i = 0; i < cat->num_entries; i++) {
		for (int j = 0; j < cat->entries[i].num_tags; j++) {
			const char *tag = cat->entries[i].tags[j];
			// Binary search in tags[]
			int lo = 0, hi = num_unique - 1;
			while (lo <= hi) {
				int mid = (lo + hi) / 2;
				int cmp = strcmp(tag, tags[mid].name);
				if (cmp == 0) {
					tags[mid].num_entries++;
					break;
				} else if (cmp < 0) {
					hi = mid - 1;
				} else {
					lo = mid + 1;
				}
			}
		}
	}

	// Allocate index arrays
	for (int i = 0; i < num_unique; i++)
		if (tags[i].num_entries > 0)
			tags[i].entry_indices = malloc(sizeof(int) * tags[i].num_entries);

	// Fill index arrays (reset counts to use as write cursors)
	int *cursors = calloc(num_unique, sizeof(int));
	for (int i = 0; i < cat->num_entries; i++) {
		for (int j = 0; j < cat->entries[i].num_tags; j++) {
			const char *tag = cat->entries[i].tags[j];
			int lo = 0, hi = num_unique - 1;
			while (lo <= hi) {
				int mid = (lo + hi) / 2;
				int cmp = strcmp(tag, tags[mid].name);
				if (cmp == 0) {
					tags[mid].entry_indices[cursors[mid]] = i;
					cursors[mid]++;
					break;
				} else if (cmp < 0) {
					hi = mid - 1;
				} else {
					lo = mid + 1;
				}
			}
		}
	}

	free(cursors);
	free(all_tags);

	cat->tag_index.num_tags = num_unique;
	cat->tag_index.tags = tags;
}

const NetzschleuderTag *netzschleuder_catalog_find_tag(const NetzschleuderCatalog *cat, const char *tag_name)
{
	if (!cat || !tag_name)
		return NULL;

	int lo = 0, hi = cat->tag_index.num_tags - 1;
	while (lo <= hi) {
		int mid = (lo + hi) / 2;
		int cmp = strcmp(tag_name, cat->tag_index.tags[mid].name);
		if (cmp == 0)
			return &cat->tag_index.tags[mid];
		else if (cmp < 0)
			hi = mid - 1;
		else
			lo = mid + 1;
	}
	return NULL;
}

static void parse_stats(cJSON *obj, NetzschleuderNetStats *stats)
{
	cJSON *v;
	v = cJSON_GetObjectItem(obj, "num_vertices");
	stats->num_vertices = v ? v->valueint : 0;
	v = cJSON_GetObjectItem(obj, "num_edges");
	stats->num_edges = v ? v->valueint : 0;
	v = cJSON_GetObjectItem(obj, "is_directed");
	stats->is_directed = v && v->type == cJSON_True;
	v = cJSON_GetObjectItem(obj, "is_bipartite");
	stats->is_bipartite = v && v->type == cJSON_True;
	v = cJSON_GetObjectItem(obj, "average_degree");
	stats->average_degree = v ? (float)v->valuedouble : 0.0f;
}

static void parse_entry(cJSON *entry_obj, NetzschleuderEntry *entry)
{
	// title
	cJSON *title = cJSON_GetObjectItem(entry_obj, "title");
	entry->title = title ? strdup(title->valuestring) : strdup("");

	// restricted
	cJSON *restricted = cJSON_GetObjectItem(entry_obj, "restricted");
	entry->restricted = restricted && restricted->type == cJSON_True;

	// tags
	cJSON *tags = cJSON_GetObjectItem(entry_obj, "tags");
	entry->tags = tags ? parse_string_array(tags, &entry->num_tags) : NULL;

	// nets
	cJSON *nets = cJSON_GetObjectItem(entry_obj, "nets");
	entry->nets = nets ? parse_string_array(nets, &entry->num_nets) : NULL;

	// analyses — detect flat vs nested
	cJSON *analyses = cJSON_GetObjectItem(entry_obj, "analyses");
	entry->stats = NULL;
	if (analyses && entry->num_nets > 0) {
		entry->stats = calloc(entry->num_nets, sizeof(NetzschleuderNetStats));

		// Check first child: if it's an object → nested (multi-version),
		// if it's a number → flat (single-version)
		cJSON *first_child = analyses->child;
		if (first_child && first_child->type == cJSON_Object) {
			// Nested: analyses["77"] = { num_vertices: ..., ... }
			for (int i = 0; i < entry->num_nets; i++) {
				cJSON *version_obj = cJSON_GetObjectItem(analyses, entry->nets[i]);
				if (version_obj)
					parse_stats(version_obj, &entry->stats[i]);
			}
		} else if (first_child) {
			// Flat: analyses.num_vertices = ...
			parse_stats(analyses, &entry->stats[0]);
		}
	}
}

NetzschleuderCatalog *netzschleuder_catalog_load(void)
{
	const char *dir = repo_cache_dir();
	char path[4096];
	snprintf(path, sizeof(path), "%s/netzschleuder.json", dir);

	FILE *fp = fopen(path, "rb");
	if (!fp) {
		fprintf(stderr, "[Catalog] No catalog file at %s\n", path);
		return NULL;
	}

	// Read entire file
	fseek(fp, 0, SEEK_END);
	long size = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	char *buffer = malloc(size + 1);
	if (!buffer) {
		fclose(fp);
		return NULL;
	}
	fread(buffer, 1, size, fp);
	buffer[size] = '\0';
	fclose(fp);

	// Parse JSON
	cJSON *root = cJSON_Parse(buffer);
	free(buffer);
	if (!root) {
		fprintf(stderr, "[Catalog] JSON parse failed: %s\n", cJSON_GetErrorPtr());
		return NULL;
	}

	// Count entries
	int count = count_json_items(root);
	if (count == 0) {
		cJSON_Delete(root);
		return NULL;
	}

	// Allocate catalog
	NetzschleuderCatalog *cat = malloc(sizeof(NetzschleuderCatalog));
	cat->num_entries = count;
	cat->entries = calloc(count, sizeof(NetzschleuderEntry));

	// Parse each entry
	int idx = 0;
	cJSON *entry = root->child;
	while (entry) {
		cat->entries[idx].id = strdup(entry->string);
		parse_entry(entry, &cat->entries[idx]);
		idx++;
		entry = entry->next;
	}

	cJSON_Delete(root);

	// Build tag index
	build_tag_index(cat);

	fprintf(stderr, "[Catalog] Loaded %d network entries (%d tags) from %s\n", count, cat->tag_index.num_tags, path);
	return cat;
}

void netzschleuder_catalog_free(NetzschleuderCatalog *cat)
{
	if (!cat)
		return;

	for (int i = 0; i < cat->num_entries; i++) {
		NetzschleuderEntry *e = &cat->entries[i];
		free(e->id);
		free(e->title);
		for (int j = 0; j < e->num_nets; j++)
			free(e->nets[j]);
		free(e->nets);
		for (int j = 0; j < e->num_tags; j++)
			free(e->tags[j]);
		free(e->tags);
		free(e->stats);
	}
	free(cat->entries);

	for (int i = 0; i < cat->tag_index.num_tags; i++) {
		free(cat->tag_index.tags[i].name);
		free(cat->tag_index.tags[i].entry_indices);
	}
	free(cat->tag_index.tags);

	free(cat);
}
