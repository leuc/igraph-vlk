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
- Debug: `cmake --build build/ --config Debug`
- Vulkan validation layers recommended for debugging.
- Do not launch `igraph-vlk`. Interactive, visual, VR, and performance testing is left to the user.

## Lint, Format, and Verify

### Formatting
```bash
clang-format -i src/**/*.c include/**/*.h shaders/* --style=file
```
Follows `.clang-format`: tabs (width 4), unlimited line length, bin-packed args, no short blocks/ifs.

### Linting
```bash
cppcheck --enable=all --inconclusive --force --std=c11 --error-exitcode=2 -I include src/
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
- **Graph streaming tests**: `dyn_kcore_test`, `dyn_core_tree_test`, `dyn_core_tree_order_test`, `dyn_leiden_test`, `dyn_layered_sphere_test`, `community_simhash_test`, and `ncol_parse_test`.
- **Main-path tests**: `criticality_test` exercises `main_path.comp` headlessly; `main_path_cache_test` and `main_path_search_test` cover cached results and CPU selection variants.
- **Geometry and rendering tests**: `layered_sphere_common_test`, `spatial_test`, `picking_test`, `renderer_anim_values_test`, and `menu_scene_test`.
- **Shared test support**: `test_utilities.h`, `validate_coreness.h`, `validate_core_tree.h`, and `criticality_test_harness.*`.
- **Adding a test**: create `tests/<name>_test.c`, add a matching `add_executable`/`target_include_directories`/`target_link_libraries` + `add_test(NAME <name>_test COMMAND <name>_test)` block in `CMakeLists.txt` (link `igraph::igraph` and, on non-MSVC, `m`). Keep benchmarking/perf out of unit tests.
- **Application testing**: Do not run the application. Leave interactive, visual, VR, streaming, and performance checks to the user.

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
| `src/graph/` | `src/graph/AGENTS.md` | Graph lifecycle/I/O, streaming maintainers, filtering, command execution, layouts, analysis, generation, Follow workflows, repository |
| `src/vulkan/` | `src/vulkan/AGENTS.md` | Vulkan lifecycle, drawing, animation/transitions, geometry, labels, UI, GPU layouts, Main Path compute, shaders |
| `src/interaction/` | `src/interaction/AGENTS.md` | Window/camera/input, picking, gamepad, menu activation, application state machine |
| `src/ui/` | `src/ui/AGENTS.md` | Static and dynamic menu construction, transforms, cards, HUD overlay |
| `src/xr/` | `src/xr/AGENTS.md` | OpenXR context, session, input, view, frame |
| `src/os/` | `src/os/AGENTS.md` | Platform paths, save/cache locations, threaded stdin line reader |

### Entry Point & App State

| File | Role |
|------|------|
| `src/main.c` | CLI/VR setup, GLFW loop, file or NCOL-stream startup, worker polling, rendering, shutdown |
| `include/app_state.h` | `AppState` struct (ties together Renderer, GraphData, Camera, WindowState, WorkerThreadContext, AppContext) |
