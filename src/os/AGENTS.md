# OS Documentation

Per-directory guide for `src/os/` and `include/os/`. See root `AGENTS.md` for build/lint/testing/style. When editing: mimic neighbors; lint/format/build; no regressions.

| File | Role |
|------|------|
| `src/os/path.c` | Platform path resolution: app base dir, relative path resolution, dated Desktop GraphML save path, cache dir |
| `include/os/path.h` | `os_path_init`, `os_resolve_path`, `os_desktop_graphml_save_path`, `os_cache_dir` |
| `src/os/stream.c` | Background stdin line reader + thread-safe FIFO queue |
| `include/os/stream.h` | `OsStreamReader` type, `os_stream_stdin_is_piped`, `os_stream_reader_start`, `os_stream_reader_poll` |

## Streaming stdin reader

The reader thread does pure string I/O only (no igraph calls); it is safe to use from any single consumer thread that polls it non-blockingly.

- Call `os_stream_stdin_is_piped()` once at startup to decide whether to enter streaming mode.
- `os_stream_reader_start()` starts a detached background thread reading newline-delimited lines into a FIFO.
- `os_stream_reader_poll(reader, out_lines, max_lines)` is non-blocking; pops up to `max_lines` lines (trailing `\n`/`\r` stripped). Each popped line is a heap string owned by the caller (must `free()`). Safe to call every frame.
