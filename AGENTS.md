# Agents Build & Code Documentation

This document serves as the primary guide for AI coding agents and developers working on the `igraph-vlk` project. It outlines the build process, linting/formatting/testing, and provides a comprehensive map of the codebase with style guidelines.

## Build Instructions

To configure and compile the project, run the following from the root:

```bash
rm -rf build/
cmake -S . -B build -Digraph_ROOT=../igraph-1.0.1/local_install/
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
clang-format -i src/**/*.c include/**/*.h shaders/**/*.glsl --style=file
```
Follows `.clang-format`: tabs (width 4), unlimited line length, bin-packed args, no short blocks/ifs.

### Linting
```bash
cppcheck --enable=all --inconclusive --force --std=c99 --error-exitcode=2 -I include src/
```
Flags unused functions `[unusedFunction]`, style issues. Suppress with `// cppcheck-suppress unusedFunction`.

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

No unit/integration tests (no `test/` dir, no CTest targets).

- **Manual Testing**: Run `./build/igraph-vlk`, load graphs (GraphML), test layouts (OpenOrd, Layered Sphere), interactions (pan/zoom/select), menus.
- **Single Test**: N/A; add via CMake `add_test` if implementing.
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
- File headers: `/** Doxygen */`.
- Inline: `// comment`.
- Sections: `// ============================================================================`.
- Todos: `// TODO: desc` (sparse: state.c:262 dismissal, 349 numeric input).

### Includes
- Order: Local quoted (`\"graph_core.h\"`, subsystem-grouped) > C std (`<stdio.h>`) > extern (`<igraph.h>`, `<glfw3.h>`). Not alpha.
- No unused; forward-decl where possible.

### Error Handling
- Funcs: `int` 0=success, <0=fail; early `return`.
- Print: `fprintf(stderr, \"[Module] Error: msg\\n\");`.
- Vulkan: `VK_CHECK(call, \"msg\")` macro (in vulkan/*.c).
- igraph: `if (igraph_call(...) != IGRAPH_SUCCESS) { err; cleanup; return; }`.
- Resources: Manual RAII (destroy on err paths).

### Vulkan/igraph Patterns
- Vulkan: Boilerplate (device/swapchain/renderpass/commands); update via `renderer_update_*`.
- igraph: Thread-safe (`IGRAPH_ENABLE_TLS`); progress handlers; worker threads for heavy ops (layouts).
- Threads: pthreads, atomics for status/progress; queue for jobs.
- GPU: Node/edge buffers; compute shaders (routing).

### General
- **Security**: No secrets/logs; validate inputs (node/edge counts).
- **Perf**: Offload CPU (worker_thread); GPU compute where possible.
- **Conventions**: Match neighbors (e.g., no new libs w/o pkg-config/CMake); Vulkan no validation prod.
- **Prohibited**: No C++; no auto libs (check package.json equiv: CMakeLists.txt).
- **When Editing**: Read neighbors; run lint/format/build; verify no regressions (layouts/UI).

---

## Code Overview (Architecture Map)

[existing full from previous, ~70 lines]

### Additional Notes
- Entry: `main.c` -> `app_state.h`.
- Graph: igraph core + custom layouts (openord, sphere).
- Render: Modular Vulkan (renderer_* dispatch).
- UI/Interaction: Overlay HUD/menu; ray-picking.

Total ~150 lines.
