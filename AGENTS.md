# Agents Build & Code Documentation

This document serves as the primary guide for AI coding agents and developers working on the `igraph-vlk` project. It outlines the build process, linting/formatting/testing, and provides a comprehensive map of the codebase with style guidelines.

## Build Instructions

To configure and compile the project, run the following from the root:

```bash
rm -rf build/
cmake -S . -B build -Digraph_ROOT=../igraph-1.0.1/local_install/ -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
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

## Adding Menu Items

The menu is a 3D spherical UI built dynamically from `src/graph/command_registry.c:g_command_registry[]`.

### Process
1. **Define Worker Function** (`src/graph/wrappers_layout.c` for layouts):
   - `void* compute_new_lay(igraph_t *graph)`: Offloaded CPU compute. Return `igraph_matrix_t*` for layouts (positions), or other data. Check `igraph_error_t != IGRAPH_SUCCESS`, cleanup/free on fail, return NULL.
   - Decl in `include/graph/wrappers_layout.h`.
   - Example: `igraph_layout_circle(graph, result, order);`

2. **Define Apply/Free Functions**:
   - Use existing `apply_layout_matrix` (updates `GraphData.nodes` positions from matrix, calls `renderer_update_graph`).
   - `free_layout_matrix`: `igraph_matrix_destroy/free(ptr)`.

3. **Register in `g_command_registry[]`** (`src/graph/command_registry.c`):
   ```
   {\"Category/Subcategory\", \"unique_id\", \"Display Name\", compute_new_lay, apply_layout_matrix, free_layout_matrix},
   ```
   - `category_path`: / separated folders (creates tree).
   - `command_id`: Unique ID for lookup.
   - Auto-sorted by registry order.

4. **Menu Auto-Builds** (`src/ui/menu.c:init_menu_tree`):
   - Parses registry, creates `MenuNode` tree (branches/folders, leaves/commands).
   - Renders instanced quads + labels bill boarded to camera.
   - Hover/expand animation.

5. **Execution Flow**:
   - Select leaf -> `node->command->cmd_def` -> `worker_thread_submit_job` queues `worker_func` (compute).
   - Complete -> `apply_func` updates state -> renderer refresh.
   - Background thread prevents UI freeze.

### Examples
- Layouts: `lay_force_fr` -> `compute_lay_force_fr`.
- Analysis: Stub `NULL` for quick (no worker).

Rebuild & run to see menu update.

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
- Inline: `// comment`.
- Sections: `// ============================================================================`.
- Todos: `// TODO: desc` (sparse).

### Includes
- Order: Local quoted > C std > extern. Not alpha.

### Error Handling
- `int` 0=success/<0=fail; `fprintf(stderr)` + cleanup/return.
- Vulkan: `VK_CHECK`.
- igraph: `!= IGRAPH_SUCCESS`.

### When Editing
- Mimic neighbors; lint/format/build; no regressions.

## Code Overview (Architecture Map)

- **Entry**: main.c -> app_state.h.
- **Graph**: igraph core + layouts (openord, sphere).
- **Render**: Vulkan modular (renderer_*).
- **UI**: HUD/menu; ray-picking.
