/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GRAPH_REPO_H
#define GRAPH_REPO_H

#include <stdbool.h>
#include <stddef.h>

// Shared repo utilities
const char *repo_cache_dir(void);
size_t repo_curl_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata);

// Netzschleuder catalog types
typedef struct
{
	int num_vertices;
	int num_edges;
	bool is_directed;
	bool is_bipartite;
	float average_degree;
} NetzschleuderNetStats;

typedef struct
{
	char *id;					  // JSON key (e.g. "karate"), owned
	char *title;				  // "Zachary Karate Club", owned
	bool restricted;			  // true = requires special access
	int num_nets;				  // count of versions
	char **nets;				  // version strings, owned
	int num_tags;				  // count of tags
	char **tags;				  // tag strings, owned
	NetzschleuderNetStats *stats; // one per nets[i], owned (or NULL)
} NetzschleuderEntry;

typedef struct
{
	char *name;			// unique tag name, owned
	int *entry_indices; // indices into catalog->entries[], owned
	int num_entries;	// count of entries with this tag
} NetzschleuderTag;

typedef struct
{
	int num_tags;
	NetzschleuderTag *tags; // sorted by name for bsearch
} NetzschleuderTagIndex;

typedef struct
{
	int num_entries;
	NetzschleuderEntry *entries;
	NetzschleuderTagIndex tag_index;
} NetzschleuderCatalog;

// Tag index lookup — returns matching entries, sets *out_count
const NetzschleuderTag *netzschleuder_catalog_find_tag(const NetzschleuderCatalog *cat, const char *tag_name);

#endif
