# Agents Build & Code Documentation

DO NOT EXECUTE GIT

This document serves as the primary guide for AI coding agents and developers working on the `igraph-vlk` project. It outlines the build process, linting/formatting/testing, and provides a comprehensive map of the codebase with style guidelines.

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

### Memory and Error Handling
- `int` 0=success/<0=fail; `fprintf(stderr)` + cleanup/return.
- Vulkan: `VK_CHECK`.
- igraph: `!= IGRAPH_SUCCESS`.
- IGRAPH_MALLOC / IGRAPH_FREE for igraph objects
= IGRAPH_CHECK

### When Editing
- Mimic neighbors; lint/format/build; no regressions.

## Code Overview (Feature-to-File Map)

### Entry Point & App State

| File | Role |
|------|------|
| `src/main.c` | GLFW loop, state machine, file loading, input dispatch |
| `include/app_state.h` | `AppState` struct, application state machine enum |
| `src/app_state.c` | State transitions, command execution dispatch |

### Graph Data & Wrappers

| File | Role |
|------|------|
| `src/graph/graph_data.c` | `GraphData` struct (nodes, edges, layout, igraph_t), load/save GraphML, simplify, layout scale |
| `include/graph/graph_data.h` | `Node`, `Edge`, `GraphData`, `GraphProperties` types |
| `src/graph/command_registry.c` | `g_command_registry[]` — all ~80 command definitions |
| `src/graph/wrappers_layout.c` | Worker functions for all 30+ layout algorithms |
| `include/graph/wrappers_layout.h` | Layout wrapper declarations |
| `src/graph/wrappers_analysis.c` | Centrality (10 measures), global properties (6 measures), community detection (12 algorithms), cycle analysis |
| `include/graph/wrappers_analysis.h` | Analysis wrapper declarations |
| `src/graph/wrappers_generate.c` | Graph generation (deterministic, stochastic, bipartite, spatial) |
| `include/graph/wrappers_generate.h` | Generation wrapper declarations |

### Rendering (Vulkan)

| File | Role |
|------|------|
| `src/renderer/renderer_core.c` | Vulkan instance, device, swapchain, pipelines, descriptor sets, buffers |
| `src/renderer/renderer_node.c` | Node pipeline (instanced billboard quads, SDF shapes) |
| `src/renderer/renderer_edge.c` | Edge pipeline (straight / spherical PCB curved) |
| `src/renderer/renderer_label.c` | Label pipeline (LOD, dynamic atlas, billboarding) |
| `src/renderer/renderer_ui.c` | UI pipeline (2D HUD overlay) |
| `src/renderer/renderer_menu.c` | Menu pipeline (instanced quads, text quads, info cards) |
| `src/renderer/renderer_dtl.c` | Detail card atlas (selected node attributes) |
| `src/renderer/renderer_pick.c` | Ray-picking debug visualization |
| `src/renderer/renderer_splc.c` | SPLC compute pipeline (GPU traffic animation) |
| `src/renderer/renderer_atlas.c` | Font atlas (Inconsolata), text rendering helpers |
| `src/renderer/renderer_xr.c` | XR framebuffers, depth buffers per view |
| `include/renderer/renderer.h` | Public renderer API (init, draw, update, cleanup) |

### Shaders

| File | Role |
|------|------|
| `shaders/node.vert` / `node.frag` | Node billboard quad with SDF shape rendering |
| `shaders/edge.vert` / `edge.frag` | Edge line/curve with SPLC weight coloring |
| `shaders/label.vert` / `label.frag` | Billboarded text labels |
| `shaders/ui.vert` / `ui.frag` | 2D HUD overlay |
| `shaders/menu.vert` / `menu.frag` | Menu card billboards with title bar + items |
| `shaders/textquad.vert` / `textquad.frag` | Generic text-bearing quads |
| `shaders/ray.vert` / `ray.frag` | Debug ray visualization |
| `shaders/routing.comp` | Spherical PCB edge routing (GPU compute) |
| `shaders/splc.comp` | SPLC traffic simulation (GPU compute) |

### UI & Interaction

| File | Role |
|------|------|
| `src/ui/camera.c` | FPS camera (yaw/pitch, WASD, movement speed) |
| `include/ui/camera.h` | Camera struct + API |
| `src/ui/input.c` | Keyboard, mouse, gamepad input dispatch |
| `include/ui/input.h` | Key mapping, gamepad deadzone, button state |
| `src/ui/menu.c` | 3D spherical menu tree (build from registry, render, hover, expand) |
| `include/ui/menu.h` | `MenuNode`, `MenuContext` types, menu API |
| `src/ui/pick.c` | Ray-picking (node sphere, edge segment, menu quad intersection) |
| `include/ui/pick.h` | Pick result types, pick API |

### Background Worker Thread

| File | Role |
|------|------|
| `src/ui/queue.c` | Pthread job queue: submit, poll, cancel, progress/status/step handlers |
| `include/ui/queue.h` | `WorkerJob`, job status enum, queue API |

### VR / XR (OpenXR)

| File | Role |
|------|------|
| `src/xr/openxr_context.c` | Instance, system, session management |
| `src/xr/openxr_vulkan.c` | Vulkan interop (graphics binding) |
| `src/xr/openxr_session.c` | Session lifecycle (begin/end/state) |
| `src/xr/openxr_input.c` | VR controller input, actions |
| `src/xr/openxr_view.c` | View configuration (eye poses) |
| `src/xr/openxr_frame.c` | Frame loop, predicted display times |

### Build System

| File | Role |
|------|------|
| `CMakeLists.txt` | Project config, dependency detection, shader compilation, OpenXR toggle |
| `shaders/compile.py` | SPIR-V compilation script |
