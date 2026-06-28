/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "graph/repo.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void mkdir_p(const char *path)
{
	char tmp[4096];
	snprintf(tmp, sizeof(tmp), "%s", path);
	for (char *p = tmp + 1; *p; p++) {
		if (*p == '/') {
			*p = '\0';
			if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
				fprintf(stderr, "[Repo] mkdir %s failed: %s\n", tmp, strerror(errno));
			*p = '/';
		}
	}
	if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
		fprintf(stderr, "[Repo] mkdir %s failed: %s\n", tmp, strerror(errno));
}

const char *repo_cache_dir(void)
{
	static char path[4096];
	if (path[0])
		return path;

	const char *xdg = getenv("XDG_CACHE_HOME");
	if (xdg && xdg[0])
		snprintf(path, sizeof(path), "%s/igraph-vlk", xdg);
	else {
		const char *home = getenv("HOME");
		if (!home)
			home = ".";
		snprintf(path, sizeof(path), "%s/.cache/igraph-vlk", home);
	}
	mkdir_p(path);
	return path;
}

size_t repo_curl_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	FILE *fp = (FILE *)userdata;
	return fwrite(ptr, size, nmemb, fp);
}
