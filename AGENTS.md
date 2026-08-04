# Agents Build & Code Documentation

DO NOT EXECUTE GIT

This document serves as the primary guide for AI coding agents and developers working on the `igraph-vlk` project. It outlines the build process, linting/formatting/testing, and style guidelines. Per-directory code maps live in each `src/*/AGENTS.md`.

## Build Instructions

To configure and compile the project, run the following from the root:

```bash
rm -rf build/
cmake -S . -B build -Digraph_ROOT=../igraph/local_install/ -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build/ --parallel
```

### Notes
- Ensure igraph built thread-safe: `cmake .. -DCMAKE_INSTALL_PREFIX=... -DIGRAPH_ENABLE_TLS=ON`
- Shaders auto-compile to SPIRV in `build/shaders/*.spv` using glslangValidator.
- Run: `./build/igraph-vlk`
- Debug: `cmake --build build/ --config Debug`
- Vulkan validation layers recommended for debugging.

## Lint, Format, and Verify

### Formatting
```bash
clang-format -i src/**/*.c include/**/*.h shaders/* --style=file
```
Follows `.clang-format`: tabs (width 4), unlimited line length, bin-packed args, no short blocks/ifs.

### Linting
```bash
cppcheck --enable=all --inconclusive --force --std=c99 --error-exitcode=2 -I include src/
```
Flags unused functions `[unusedFunction]`, style issues. Suppress with `// cppcheck-suppress unusedFunction`.

### Quick Symbol Lookup
```bash
ctags -x --c-kinds=f -R src/   # List all C function definitions in src/
ctags -x --c-kinds=f -R src/ | grep -i pick  # Find functions matching "pick"
```
No `tags` file generated (plain text output via `-x`). Requires `ctags` (Universal Ctags).

### Static Analysis
```bash
clang-tidy src/**/*.c include/**/*.h -- -Iinclude $(pkg-config --cflags igraph glfw3)
```
No pre-commit hooks; run manually.

### Verify
```bash
cmake --build build/ --parallel --verbose  # Full rebuild
```
No typechecker (C). Check for igraph/Vulkan errors at runtime.

## Testing

Unit tests live in `tests/` and are wired into CMake/CTest.

- **Run all tests**: `ctest --test-dir build --output-on-failure` (or `ctest -C Debug` from inside `build/`). Each test is a `RUN_TEST`-driven executable that asserts with `IGRAPH_ASSERT`; `add_test` registers them in `CMakeLists.txt`.
- **Test layout**:
  - `tests/dyn_kcore_test.c` — data-driven correctness for `src/graph/dyn_k-core.c` (dynamic streaming k-core maintenance). Mirrors igraph's own `tests/unit/coreness.c`: it runs the maintained coreness over the exact same graph list (empty, singleton, full/looped/directed, Zachary, Zachary+loops+multiedges, geometric) and validates against both `igraph_coreness` and the shared `validate_coreness` oracle.
  - `tests/test_utilities.h` — generic harness: `RUN_TEST(func)` drives a test function and verifies the igraph FINALLY stack; `VERIFY_FINALLY_STACK()`.
  - `tests/validate_coreness.h` — structural coreness oracle (ported from igraph's `coreness.c`): every k-core subgraph must have min degree ≥ k.
- **Adding a test**: create `tests/<name>_test.c`, add a matching `add_executable`/`target_include_directories`/`target_link_libraries` + `add_test(NAME <name>_test COMMAND <name>_test)` block in `CMakeLists.txt` (link `igraph::igraph` and, on non-MSVC, `m`). Keep benchmarking/perf out of unit tests.
- **Manual Testing**: Run `./build/igraph-vlk`, load graphs (GraphML), test layouts (OpenOrd, Layered Sphere), interactions (pan/zoom/select), menus.
- **Visual/Perf**: FPS in HUD; stress large graphs (>10k nodes).

## Code Style Guidelines

Mimic existing patterns strictly. Run `clang-format` after edits.

### Formatting (from .clang-format)
- **Indent**: Tabs only (`UseTab: Always`), 4-width (`TabWidth: 4`).
- **Lines**: Unlimited length (`ColumnLimit: 9999`); keep long Vulkan/igraph calls intact.
- **Braces**: Same-line (`BreakBeforeBraces: Custom`), multi-line after control (`AfterControlStatement: MultiLine`); no single-line if/blocks/loops (`AllowShort*: false`).
- **Args/Params**: Bin-packed (`BinPackArguments/Parameters: true`).
- **Strings**: No breaks (`BreakStringLiterals: false`).

### Naming Conventions
- **Functions/Variables**: `snake_case` (e.g., `worker_thread_init`, `current_job`).
- **Constants/Enums/Macros**: `UPPER_SNAKE_CASE` (e.g., `JOB_STATUS_PENDING`, `VK_CHECK`).
- **Structs/Typedefs**: `PascalCase` (e.g., `WorkerThreadContext`, `AppState`).
- **Globals**: Avoid; use `AppContext` state.

### Comments
Keep comments minimal, essential, preceise and bare - no seperators - no headers, no deviders

### Includes
- Order: Local quoted > C std > extern. Not alpha.

### Memory and Error Handling
- `int` 0=success/<0=fail; `fprintf(stderr)` + cleanup/return.
- Vulkan: `VK_CHECK`.
- igraph: `!= IGRAPH_SUCCESS` (manual checks; `IGRAPH_CHECK` cannot be used in `void*` worker functions).
- `IGRAPH_MALLOC` / `IGRAPH_FREE` for heap-allocated igraph objects only (igraph_t*, igraph_matrix_t*, igraph_vector_t*, igraph_vector_int_t*).
- `malloc` / `free` for plain C arrays and non-igraph structs.
- Every `igraph_*_init` call must be checked. Pattern:
  ```c
  if (igraph_vector_int_init(&temp, n) != IGRAPH_SUCCESS) {
      /* cleanup previously allocated resources */
      return NULL; /* or appropriate failure value */
  }
  ```
- Early-exit pattern (`!= IGRAPH_SUCCESS` with cleanup) is preferred over positive-check (`== IGRAPH_SUCCESS` with nested body).

### When Editing
- Mimic neighbors; lint/format/build; no regressions.

## Codebase Map

Per-directory code overviews and workflows live in the matching subdirectory:

| Directory | Doc | Contents |
|-----------|-----|----------|
| `src/graph/` | `src/graph/AGENTS.md` | Graph core/data, filtering, command registry & worker thread, layout/analysis/generation wrappers, graph repository, adding menu commands |
| `src/vulkan/` | `src/vulkan/AGENTS.md` | Vulkan renderer, pipelines, geometry/labels/UI/compute, shaders |
| `src/interaction/` | `src/interaction/AGENTS.md` | Window, camera, input, picking, gamepad, menu interaction, app state machine |
| `src/ui/` | `src/ui/AGENTS.md` | Menu tree/rendering, HUD overlay |
| `src/xr/` | `src/xr/AGENTS.md` | OpenXR context, session, input, view, frame |
| `src/os/` | `src/os/AGENTS.md` | Platform path resolution, streaming stdin reader |

### Entry Point & App State

| File | Role |
|------|------|
| `src/main.c` | GLFW loop, state machine, file loading, input dispatch |
| `include/app_state.h` | `AppState` struct (ties together Renderer, GraphData, Camera, WindowState, WorkerThreadContext, AppContext) |
