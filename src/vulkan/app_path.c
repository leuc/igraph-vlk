#include "vulkan/app_path.h"

#include <linux/limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char base_dir[PATH_MAX];

void app_path_init(void)
{
	ssize_t len = readlink("/proc/self/exe", base_dir, PATH_MAX - 1);
	if (len <= 0) {
		fprintf(stderr, "FATAL: failed to read /proc/self/exe\n");
		exit(1);
	}
	base_dir[len] = '\0';
	*strrchr(base_dir, '/') = '\0';
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
	static char buf[PATH_MAX * 2];

	if (path[0] == '/') {
		if (file_exists(path))
			return path;
		fprintf(stderr, "FATAL: resource not found: %s\n", path);
		exit(1);
	}

	snprintf(buf, sizeof(buf), "%s/%s", base_dir, path);
	if (file_exists(buf))
		return buf;

	snprintf(buf, sizeof(buf), "%s/../share/igraph-vlk/%s", base_dir, path);
	if (file_exists(buf))
		return buf;

	snprintf(buf, sizeof(buf), "%s/../%s", base_dir, path);
	if (file_exists(buf))
		return buf;

	fprintf(stderr, "FATAL: resource not found: %s\n", path);
	fprintf(stderr, "  tried: %s/%s\n", base_dir, path);
	fprintf(stderr, "  tried: %s/../share/igraph-vlk/%s\n", base_dir, path);
	fprintf(stderr, "  tried: %s/../%s\n", base_dir, path);
	exit(1);
}
