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
#include <yyjson.h>

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

void *netzschleuder_refresh(ExecutionContext *ctx)
{
	(void)ctx;

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
	worker_thread_set_status_message("Extracting index...");

	yyjson_read_err err;
	yyjson_doc *doc = yyjson_read_file(path, YYJSON_READ_NOFLAG, NULL, &err);
	if (!doc) {
		fprintf(stderr, "[Repo] Failed to parse downloaded catalog: %s\n", err.msg);
		worker_thread_set_status_message("Index extraction failed");
		return NULL;
	}

	yyjson_mut_doc *out = yyjson_mut_doc_new(NULL);
	if (!out) {
		yyjson_doc_free(doc);
		worker_thread_set_status_message("Index extraction failed");
		return NULL;
	}
	yyjson_mut_val *arr = yyjson_mut_arr(out);
	yyjson_mut_doc_set_root(out, arr);

	yyjson_val *root = yyjson_doc_get_root(doc);
	yyjson_val *key, *val;
	yyjson_obj_iter iter = yyjson_obj_iter_with(root);
	while ((key = yyjson_obj_iter_next(&iter))) {
		const char *entry_id = yyjson_get_str(key);
		if (!entry_id)
			continue;
		val = yyjson_obj_iter_get_val(key);

		yyjson_val *v_title = yyjson_obj_get(val, "title");
		yyjson_val *v_tags = yyjson_obj_get(val, "tags");
		yyjson_val *v_nets = yyjson_obj_get(val, "nets");
		yyjson_val *v_analyses = yyjson_obj_get(val, "analyses");

		const char *title = v_title && yyjson_is_str(v_title) ? yyjson_get_str(v_title) : entry_id;
		const char *version_id = entry_id;
		int64_t num_vertices = 0, num_edges = 0;

		if (v_nets && yyjson_is_arr(v_nets) && yyjson_arr_size(v_nets) > 0) {
			yyjson_val *v_ver = yyjson_arr_get(v_nets, 0);
			if (v_ver && yyjson_is_str(v_ver))
				version_id = yyjson_get_str(v_ver);
		}

		if (v_analyses && yyjson_is_obj(v_analyses)) {
			yyjson_val *stats = yyjson_obj_get(v_analyses, version_id);
			if (!stats)
				stats = yyjson_obj_get(v_analyses, entry_id);
			if (stats && yyjson_is_obj(stats)) {
				yyjson_val *vv = yyjson_obj_get(stats, "num_vertices");
				yyjson_val *ve = yyjson_obj_get(stats, "num_edges");
				if (vv && yyjson_is_num(vv))
					num_vertices = yyjson_get_sint(vv);
				if (ve && yyjson_is_num(ve))
					num_edges = yyjson_get_sint(ve);
			}
		}

		yyjson_mut_val *obj = yyjson_mut_obj(out);
		yyjson_mut_obj_add_str(out, obj, "id", entry_id);
		yyjson_mut_obj_add_str(out, obj, "title", title);
		yyjson_mut_obj_add_str(out, obj, "version", version_id);
		yyjson_mut_obj_add_int(out, obj, "nodes", num_vertices);
		yyjson_mut_obj_add_int(out, obj, "edges", num_edges);

		if (v_tags && yyjson_is_arr(v_tags) && yyjson_arr_size(v_tags) > 0) {
			yyjson_mut_val *tags_arr = yyjson_mut_arr(out);
			yyjson_val *t;
			yyjson_arr_iter t_iter = yyjson_arr_iter_with(v_tags);
			while ((t = yyjson_arr_iter_next(&t_iter))) {
				if (yyjson_is_str(t))
					yyjson_mut_arr_append(tags_arr, yyjson_mut_strcpy(out, yyjson_get_str(t)));
			}
			if (yyjson_mut_arr_size(tags_arr) > 0)
				yyjson_mut_obj_add_val(out, obj, "tags", tags_arr);
		}

		yyjson_mut_arr_append(arr, obj);
	}

	// Keep immutable doc alive during write (string refs in mut doc may still reference it)
	char index_path[4096];
	snprintf(index_path, sizeof(index_path), "%s/netzschleuder_index.json", dir);

	size_t json_len = 0;
	char *json_str = yyjson_mut_write(out, YYJSON_WRITE_PRETTY, &json_len);
	yyjson_mut_doc_free(out);

	if (!json_str) {
		yyjson_doc_free(doc);
		fprintf(stderr, "[Repo] Failed to serialize index\n");
		worker_thread_set_status_message("Index extraction failed");
		return NULL;
	}

	FILE *ofp = fopen(index_path, "w");
	if (!ofp) {
		yyjson_doc_free(doc);
		free(json_str);
		fprintf(stderr, "[Repo] Failed to open %s for writing\n", index_path);
		worker_thread_set_status_message("Index extraction failed");
		return NULL;
	}
	fwrite(json_str, 1, json_len, ofp);
	fclose(ofp);
	free(json_str);
	yyjson_doc_free(doc);

	worker_thread_set_status_message("Netzschleuder catalog ready");
	fprintf(stderr, "[Repo] Index written to %s\n", index_path);

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
// Network download (stub: print only)
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

// ---------------------------------------------------------------------------
// Catalog index loader (reads the compact index file)
// ---------------------------------------------------------------------------

static int str_cmp(const void *a, const void *b)
{
	return strcmp(*(const char **)a, *(const char **)b);
}

static void build_tag_index(NetzschleuderCatalog *cat)
{
	// Count unique tags via linear scan (tiny dataset: 65 tags, 286 entries)
	int total = 0;
	for (int i = 0; i < cat->num_entries; i++)
		total += cat->entries[i].num_tags;

	if (total == 0)
		return;

	char **all_tags = malloc(sizeof(char *) * total);
	int n = 0;
	for (int i = 0; i < cat->num_entries; i++)
		for (int j = 0; j < cat->entries[i].num_tags; j++)
			all_tags[n++] = cat->entries[i].tags[j];

	qsort(all_tags, n, sizeof(char *), str_cmp);

	int num_unique = 0;
	for (int i = 0; i < n; i++) {
		if (i == 0 || strcmp(all_tags[i], all_tags[i - 1]) != 0)
			num_unique++;
	}

	NetzschleuderTag *tags = calloc(num_unique, sizeof(NetzschleuderTag));
	int t = 0;
	for (int i = 0; i < n; i++) {
		if (i == 0 || strcmp(all_tags[i], all_tags[i - 1]) != 0) {
			tags[t].name = strdup(all_tags[i]);
			tags[t].num_entries = 0;
			t++;
		}
	}

	free(all_tags);

	// Count entries per tag
	for (int i = 0; i < cat->num_entries; i++) {
		for (int j = 0; j < cat->entries[i].num_tags; j++) {
			for (int k = 0; k < num_unique; k++) {
				if (strcmp(cat->entries[i].tags[j], tags[k].name) == 0) {
					tags[k].num_entries++;
					break;
				}
			}
		}
	}

	for (int i = 0; i < num_unique; i++)
		if (tags[i].num_entries > 0)
			tags[i].entry_indices = malloc(sizeof(int) * tags[i].num_entries);

	int *cursors = calloc(num_unique, sizeof(int));
	for (int i = 0; i < cat->num_entries; i++) {
		for (int j = 0; j < cat->entries[i].num_tags; j++) {
			for (int k = 0; k < num_unique; k++) {
				if (strcmp(cat->entries[i].tags[j], tags[k].name) == 0) {
					tags[k].entry_indices[cursors[k]] = i;
					cursors[k]++;
					break;
				}
			}
		}
	}

	free(cursors);

	cat->tag_index.num_tags = num_unique;
	cat->tag_index.tags = tags;
}

const NetzschleuderTag *netzschleuder_catalog_find_tag(const NetzschleuderCatalog *cat, const char *tag_name)
{
	if (!cat || !tag_name)
		return NULL;
	for (int i = 0; i < cat->tag_index.num_tags; i++) {
		if (strcmp(tag_name, cat->tag_index.tags[i].name) == 0)
			return &cat->tag_index.tags[i];
	}
	return NULL;
}

NetzschleuderCatalog *netzschleuder_catalog_load(void)
{
	const char *dir = repo_cache_dir();
	char path[4096];
	snprintf(path, sizeof(path), "%s/netzschleuder_index.json", dir);

	yyjson_read_err err;
	yyjson_doc *doc = yyjson_read_file(path, YYJSON_READ_NOFLAG, NULL, &err);
	if (!doc) {
		fprintf(stderr, "[Catalog] No index at %s: %s\n", path, err.msg);
		return NULL;
	}

	yyjson_val *root = yyjson_doc_get_root(doc);
	size_t count = yyjson_arr_size(root);
	if (count == 0) {
		yyjson_doc_free(doc);
		return NULL;
	}

	NetzschleuderCatalog *cat = malloc(sizeof(NetzschleuderCatalog));
	if (!cat) {
		yyjson_doc_free(doc);
		return NULL;
	}
	cat->num_entries = (int)count;
	cat->entries = calloc(count, sizeof(NetzschleuderEntry));
	if (!cat->entries) {
		free(cat);
		yyjson_doc_free(doc);
		return NULL;
	}

	yyjson_val *val;
	size_t idx = 0;
	yyjson_arr_iter iter = yyjson_arr_iter_with(root);
	while ((val = yyjson_arr_iter_next(&iter))) {
		NetzschleuderEntry *e = &cat->entries[idx];
		yyjson_val *v;

		v = yyjson_obj_get(val, "id");
		e->id = v && yyjson_is_str(v) ? strdup(yyjson_get_str(v)) : strdup("");

		v = yyjson_obj_get(val, "title");
		e->title = v && yyjson_is_str(v) ? strdup(yyjson_get_str(v)) : strdup("");

		v = yyjson_obj_get(val, "version");
		const char *ver = v && yyjson_is_str(v) ? yyjson_get_str(v) : e->id;
		e->num_nets = 1;
		e->nets = malloc(sizeof(char *));
		e->nets[0] = strdup(ver);

		v = yyjson_obj_get(val, "tags");
		if (v && yyjson_is_arr(v) && yyjson_arr_size(v) > 0) {
			e->num_tags = (int)yyjson_arr_size(v);
			e->tags = malloc(sizeof(char *) * e->num_tags);
			if (e->tags) {
				size_t ti = 0;
				yyjson_val *t;
				yyjson_arr_iter t_iter = yyjson_arr_iter_with(v);
				while ((t = yyjson_arr_iter_next(&t_iter))) {
					e->tags[ti++] = yyjson_is_str(t) ? strdup(yyjson_get_str(t)) : strdup("");
				}
			}
		} else {
			e->num_tags = 0;
			e->tags = NULL;
		}

		v = yyjson_obj_get(val, "nodes");
		int64_t nv = v && yyjson_is_num(v) ? yyjson_get_sint(v) : 0;
		v = yyjson_obj_get(val, "edges");
		int64_t ne = v && yyjson_is_num(v) ? yyjson_get_sint(v) : 0;

		e->stats = calloc(1, sizeof(NetzschleuderNetStats));
		if (e->stats) {
			e->stats[0].num_vertices = (int)nv;
			e->stats[0].num_edges = (int)ne;
		}
		e->restricted = false;

		idx++;
	}

	yyjson_doc_free(doc);

	build_tag_index(cat);

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
