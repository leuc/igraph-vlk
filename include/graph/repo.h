/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GRAPH_REPO_H
#define GRAPH_REPO_H

#include <stddef.h>

size_t repo_curl_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata);

#endif
