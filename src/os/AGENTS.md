# OS Documentation

Per-directory guide for `src/os/` and `include/os/`. See root `AGENTS.md` for build, test, and style rules.

| Files | Role |
|------|------|
| `path.c/.h` | Executable/base-directory discovery, installed or relative resource resolution, dated Desktop GraphML save paths, and platform cache paths |
| `stream.c/.h` | Piped-stdin detection, detached reader thread, thread-safe FIFO, non-blocking polling, EOF state, and cleanup |

## Path Rules

- Call `os_path_init()` before resolving installed resources.
- Use `os_resolve_path()` for runtime assets, `os_desktop_graphml_save_path()` for Save, and `os_cache_dir()` for cached data.
- Keep Windows, macOS, and Unix branches behaviorally aligned. Callers own returned heap paths where the header says so.

## Stdin Reader Contract

- Call `os_stream_stdin_is_piped()` once during startup to choose file/no-file mode versus NCOL streaming.
- `os_stream_reader_start()` owns a detached thread that performs string I/O only. It must never call igraph or mutate `GraphData`.
- `os_stream_reader_poll()` is non-blocking and transfers ownership of each popped, newline-stripped heap string to the caller.
- `src/graph/stream.c` is the single consumer and applies parsed lines to the graph on the main thread.
- Preserve queue locking, EOF semantics, and shutdown cleanup when changing this module.
