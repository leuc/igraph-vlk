/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "graph/repo.h"
#include "os/path.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

size_t repo_curl_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	FILE *fp = (FILE *)userdata;
	return fwrite(ptr, size, nmemb, fp);
}
