/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef OS_PATH_H
#define OS_PATH_H

// Initialize OS path detection. Must be called before any other os_path_* function.
void os_path_init(void);

// Resolve a relative path against the application base directory.
// Tries base/<rel>, base/../share/igraph-vlk/<rel>, base/../<rel>.
// Aborts with fatal error if the file cannot be found.
const char *os_resolve_path(const char *rel);

// Returns a full path on the user's Desktop for a new dated GraphML save file,
// e.g. "/home/user/Desktop/graph_20260801_143022.graphml".
// Creates the Desktop directory if it does not exist. Returns NULL if
// HOME/USERPROFILE is unset.
const char *os_desktop_graphml_save_path(void);

// Returns the platform cache directory for the given app name.
// Respects IGRAPH_VLK_CACHE_PATH env var if set.
// Linux: $XDG_CACHE_HOME/<name> or ~/.cache/<name>
// macOS: ~/Library/Caches/<name>
// Windows: %LOCALAPPDATA%\<name>
// Creates the directory if it does not exist.
const char *os_cache_dir(const char *app_name);

// Searches for a monospace font file at runtime.
// Respects IGRAPH_VLK_FONT_PATH env var if set.
// Searches OS-specific font directories for common monospace fonts.
// Aborts with fatal error if no font is found.
const char *os_find_monospace_font(void);

// Creates directories recursively (like mkdir -p).
void os_mkdir_p(const char *path);

// Returns 1 if the file exists, 0 otherwise.
int os_file_exists(const char *path);

#endif
