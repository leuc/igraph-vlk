/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "vulkan/app_path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <limits.h>
#include <mach-o/dyld.h>
#else
#include <limits.h>
#include <unistd.h>
#endif

static char base_dir[4096];

void app_path_init(void)
{
#if defined(_WIN32)
	char buf[MAX_PATH];
	DWORD len = GetModuleFileNameA(NULL, buf, MAX_PATH);
	if (len == 0) {
		fprintf(stderr, "FATAL: failed to get executable path\n");
		exit(1);
	}
	buf[len] = '\0';
	char *last_slash = strrchr(buf, '\\');
	if (!last_slash)
		last_slash = strrchr(buf, '/');
	if (last_slash)
		*last_slash = '\0';
	strncpy(base_dir, buf, sizeof(base_dir) - 1);
	base_dir[sizeof(base_dir) - 1] = '\0';
#elif defined(__APPLE__)
	char buf[4096];
	uint32_t size = sizeof(buf);
	if (_NSGetExecutablePath(buf, &size) != 0) {
		fprintf(stderr, "FATAL: executable path too long\n");
		exit(1);
	}
	char *last_slash = strrchr(buf, '/');
	if (last_slash)
		*last_slash = '\0';
	strncpy(base_dir, buf, sizeof(base_dir) - 1);
	base_dir[sizeof(base_dir) - 1] = '\0';
#else
	ssize_t len = readlink("/proc/self/exe", base_dir, sizeof(base_dir) - 1);
	if (len <= 0) {
		fprintf(stderr, "FATAL: failed to read /proc/self/exe\n");
		exit(1);
	}
	base_dir[len] = '\0';
	char *last_slash = strrchr(base_dir, '/');
	if (last_slash)
		*last_slash = '\0';
#endif
}

static int file_exists(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return 0;
	fclose(f);
	return 1;
}

const char *app_path_resolve(const char *path)
{
	static char buf[8192];
#if defined(_WIN32)
	char sep = '\\';
#else
	char sep = '/';
#endif

	if (path[0] == '/' || path[0] == '\\' || (path[0] && path[1] == ':')) {
		if (file_exists(path))
			return path;
		fprintf(stderr, "FATAL: resource not found: %s\n", path);
		exit(1);
	}

	snprintf(buf, sizeof(buf), "%s%c%s", base_dir, sep, path);
	if (file_exists(buf))
		return buf;

	snprintf(buf, sizeof(buf), "%s%c..%cshare%cigraph-vlk%c%s", base_dir, sep, sep, sep, sep, path);
	if (file_exists(buf))
		return buf;

	snprintf(buf, sizeof(buf), "%s%c..%c%s", base_dir, sep, sep, path);
	if (file_exists(buf))
		return buf;

	fprintf(stderr, "FATAL: resource not found: %s\n", path);
	fprintf(stderr, "  tried: %s/%s\n", base_dir, path);
	fprintf(stderr, "  tried: %s/../share/igraph-vlk/%s\n", base_dir, path);
	fprintf(stderr, "  tried: %s/../%s\n", base_dir, path);
	exit(1);
}
