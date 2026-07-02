/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "os/path.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <limits.h>
#include <mach-o/dyld.h>
#else
#include <limits.h>
#include <unistd.h>
#endif

// ============================================================================
// Static state
// ============================================================================

static char base_dir[4096];

// ============================================================================
// Utility
// ============================================================================

int os_file_exists(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return 0;
	fclose(f);
	return 1;
}

void os_mkdir_p(const char *path)
{
	char tmp[4096];
	snprintf(tmp, sizeof(tmp), "%s", path);
	for (char *p = tmp + 1; *p; p++) {
		if (*p == '/') {
			*p = '\0';
			if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
				fprintf(stderr, "[os] mkdir %s failed: %s\n", tmp, strerror(errno));
			*p = '/';
		}
	}
	if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
		fprintf(stderr, "[os] mkdir %s failed: %s\n", tmp, strerror(errno));
}

// ============================================================================
// Application path resolution
// ============================================================================

void os_path_init(void)
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

const char *os_resolve_path(const char *rel)
{
	static char buf[8192];
#if defined(_WIN32)
	char sep = '\\';
#else
	char sep = '/';
#endif

	if (rel[0] == '/' || rel[0] == '\\' || (rel[0] && rel[1] == ':')) {
		if (os_file_exists(rel))
			return rel;
		fprintf(stderr, "FATAL: resource not found: %s\n", rel);
		exit(1);
	}

	snprintf(buf, sizeof(buf), "%s%c%s", base_dir, sep, rel);
	if (os_file_exists(buf))
		return buf;

	snprintf(buf, sizeof(buf), "%s%c..%cshare%cigraph-vlk%c%s", base_dir, sep, sep, sep, sep, rel);
	if (os_file_exists(buf))
		return buf;

	snprintf(buf, sizeof(buf), "%s%c..%c%s", base_dir, sep, sep, rel);
	if (os_file_exists(buf))
		return buf;

	fprintf(stderr, "FATAL: resource not found: %s\n", rel);
	fprintf(stderr, "  tried: %s/%s\n", base_dir, rel);
	fprintf(stderr, "  tried: %s/../share/igraph-vlk/%s\n", base_dir, rel);
	fprintf(stderr, "  tried: %s/../%s\n", base_dir, rel);
	exit(1);
}

// ============================================================================
// Cache directory
// ============================================================================

const char *os_cache_dir(const char *app_name)
{
	static char path[4096];
	if (path[0])
		return path;

	const char *env_override = getenv("IGRAPH_VLK_CACHE_PATH");
	if (env_override && env_override[0]) {
		snprintf(path, sizeof(path), "%s", env_override);
		os_mkdir_p(path);
		return path;
	}

#if defined(_WIN32)
	const char *base = getenv("LOCALAPPDATA");
	if (base && base[0])
		snprintf(path, sizeof(path), "%s\\%s", base, app_name);
	else {
		const char *home = getenv("USERPROFILE");
		if (!home)
			home = ".";
		snprintf(path, sizeof(path), "%s\\AppData\\Local\\%s", home, app_name);
	}
#elif defined(__APPLE__)
	const char *home = getenv("HOME");
	if (!home)
		home = ".";
	snprintf(path, sizeof(path), "%s/Library/Caches/%s", home, app_name);
#else
	const char *xdg = getenv("XDG_CACHE_HOME");
	if (xdg && xdg[0])
		snprintf(path, sizeof(path), "%s/%s", xdg, app_name);
	else {
		const char *home = getenv("HOME");
		if (!home)
			home = ".";
		snprintf(path, sizeof(path), "%s/.cache/%s", home, app_name);
	}
#endif

	os_mkdir_p(path);
	return path;
}

// ============================================================================
// Monospace font detection
// ============================================================================

const char *os_find_monospace_font(void)
{
	const char *env_override = getenv("IGRAPH_VLK_FONT_PATH");
	if (env_override && env_override[0]) {
		if (os_file_exists(env_override))
			return env_override;
		fprintf(stderr, "FATAL: IGRAPH_VLK_FONT_PATH set to \"%s\" but file not found\n", env_override);
		exit(1);
	}

#if defined(_WIN32)
	static const char *font_dirs[] = {
		"C:\\Windows\\Fonts",
		NULL,
	};
	static const char *font_names[] = {
		"Inconsolata.otf",
		"consola.ttf",
		"cour.ttf",
		NULL,
	};
#elif defined(__APPLE__)
	static const char *font_dirs[] = {
		"/System/Library/Fonts",
		"/Library/Fonts",
		NULL,
	};
	static const char *font_names[] = {
		"Inconsolata.otf", "Monaco.ttf", "Menlo.ttc", "Courier New.ttf", NULL,
	};
#else
	static const char *font_dirs[] = {
		"/usr/share/fonts/truetype/inconsolata", "/usr/share/fonts/opentype/inconsolata", "/app/share/fonts/truetype/inconsolata", "/usr/share/fonts/truetype/dejavu", "/usr/share/fonts/truetype/liberation", "/usr/share/fonts/dejavu", "/usr/share/fonts/liberation", "/usr/share/fonts/google-noto", "/usr/share/fonts/noto", "/usr/share/fonts/truetype", "/usr/share/fonts/opentype", "/usr/share/fonts", "/usr/local/share/fonts", NULL,
	};
	static const char *font_names[] = {
		"Inconsolata.otf", "Inconsolata-Regular.otf", "Inconsolata-Regular.ttf", "Inconsolata.ttf", "DejaVuSansMono.ttf", "LiberationMono-Regular.ttf", "NotoSansMono-Regular.ttf", "SourceCodePro-Regular.ttf", "UbuntuMono-R.ttf", "FreeMono.ttf", NULL,
	};
#endif

	static char found[4096];

	// Also search user-local font directories from env
	const char *xdg_data = getenv("XDG_DATA_HOME");
	char user_font_dir[4096];
	if (xdg_data && xdg_data[0]) {
		snprintf(user_font_dir, sizeof(user_font_dir), "%s/fonts", xdg_data);
		for (int fi = 0; font_names[fi]; fi++) {
			snprintf(found, sizeof(found), "%s/%s", user_font_dir, font_names[fi]);
			if (os_file_exists(found))
				return found;
		}
	}

	const char *home = getenv("HOME");
	if (home && home[0]) {
		snprintf(user_font_dir, sizeof(user_font_dir), "%s/.local/share/fonts", home);
		for (int fi = 0; font_names[fi]; fi++) {
			snprintf(found, sizeof(found), "%s/%s", user_font_dir, font_names[fi]);
			if (os_file_exists(found))
				return found;
		}
	}

	for (int di = 0; font_dirs[di]; di++) {
		for (int fi = 0; font_names[fi]; fi++) {
			snprintf(found, sizeof(found), "%s/%s", font_dirs[di], font_names[fi]);
			if (os_file_exists(found))
				return found;
		}
	}

	fprintf(stderr, "FATAL: no monospace font found\n");
	fprintf(stderr, "  searched IGRAPH_VLK_FONT_PATH (not set or invalid)\n");
	fprintf(stderr, "  searched %d directories for %d font names\n", (int)(sizeof(font_dirs) / sizeof(font_dirs[0]) - 1), (int)(sizeof(font_names) / sizeof(font_names[0]) - 1));
	fprintf(stderr, "  install Inconsolata, DejaVu Sans Mono, or Liberation Mono\n");
	fprintf(stderr, "  or set IGRAPH_VLK_FONT_PATH=/path/to/font.ttf\n");
	exit(1);
}
