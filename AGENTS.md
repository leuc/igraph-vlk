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

### CommandDef Struct
```c
typedef struct CommandDef {
    const char *category_path;    // e.g. "Layout/Force-Directed"
    const char *command_id;       // e.g. "lay_force_fr"
    const char *display_name;     // e.g. "Fruchterman-Reingold"
    IgraphWorkerFunc worker_func; // Pure compute: void* (*)(igraph_t*)
    IgraphApplyFunc apply_func;   // Sync result to UI: void (*)(ExecutionContext*, void*)
    IgraphFreeFunc free_func;     // Cleanup: void (*)(void*)
    IgraphGpuPollFunc gpu_poll_func; // GPU poll: bool (*)(ExecutionContext*), NULL for CPU-only
} CommandDef;
```

### Menu Structure (root categories in order)

| Root | Sub-menus | Purpose |
|------|-----------|---------|
| `Data` | Patterns, Random, Bipartite, Spatial, Repository | Graph source (generate, import, browse) |
| `Layout` | Force-Directed, Hierarchical, Geometric, Embedding, Bipartite, GPU | Node positioning |
| `Rank` | *(flat)* | Per-node importance (degree, centrality, PageRank...) |
| `Group` | *(flat)* | Community detection (Louvain, Leiden, Walktrap...) |
| `Follow` | *(flat)* | Path/traversal exploration (shortest path, BFS, SPLC) |
| `Structure` | *(flat)* | Global graph properties (density, diameter, transitivity...) |
| `Show` | *(flat)* | Filter/highlight/visibility (by degree, by attribute, extract...) |
| `Alter` | Clean Up | Graph transformation (feedback arc set, complement, MST...) |

### Process
1. **Define Worker Function** (in appropriate `src/graph/wrappers_*.c`):
   - `void* compute_new_lay(igraph_t *graph)`: Offloaded CPU compute. Return `igraph_matrix_t*` for layouts, `igraph_vector_t*` for centrality, `igraph_vector_int_t*` for community membership, `igraph_t*` for new graphs, or other data. Check `igraph_error_t != IGRAPH_SUCCESS`, cleanup/free on fail, return NULL.
   - Declare in corresponding `include/graph/wrappers_*.h`.
   - Example: `igraph_layout_circle(graph, result, order);`

2. **Define Apply/Free Functions**:
   - Use existing `apply_layout_matrix` (updates `GraphData.nodes` positions from matrix, calls `renderer_update_graph`).
   - Use `apply_centrality_scores` / `centrality_scores_free` for centrality measures.
   - Use `apply_community_membership` / `free_community_membership` for community detection.
   - Use `apply_new_graph` / `free_new_graph` for graph generation.
   - Use `apply_info_card` / `info_card_free` for scalar results (diameter, density, etc.).
   - `free_layout_matrix`: `igraph_matrix_destroy/free(ptr)`.

3. **Register in `g_command_registry[]`** (`src/graph/command_registry.c`):
   ```c
   {"Category/Subcategory", "unique_id", "Display Name", compute_func, apply_func, free_func},
   ```
   - For GPU-accelerated commands, add `gpu_poll_func` as 7th arg (e.g., `poll_bcgl_gpu`, `poll_splc_gpu`). Pass `NULL` for CPU-only.
   - `category_path`: / separated folders (creates tree).
   - `command_id`: Unique ID for lookup.
   - Auto-sorted by registry order.

4. **Menu Auto-Builds** (`src/ui/menu.c`):
   - Parses registry, creates `MenuNode` tree (branches/folders, leaves/commands).
   - Renders instanced quads + labels bill boarded to camera.
   - Hover/expand animation.

5. **Menu Interaction** (`src/interaction/menu.c`):
   - `interaction_menu_toggle(AppState *state)`: Opens/closes the spherical menu.
   - `interaction_pick_menu_node(AppState *state, mouse_x, mouse_y)`: Mouse picking.
   - `raycast_menu_crosshair(AppState *state)`: VR/immersive crosshair picking.

6. **Execution Flow**:
   - Select leaf -> `node->command->cmd_def` -> `worker_thread_submit_job` queues `worker_func` (compute).
   - Complete -> `apply_func` updates state -> renderer refresh.
   - For GPU jobs: `gpu_poll_func` is called per-frame until it returns `true`.
   - Background thread prevents UI freeze.

### Examples
- Layouts: `lay_force_fr` -> `compute_igraph_layout_fruchterman_reingold_3d` + `apply_layout_matrix`.
- Rank: `igraph_degree` -> `compute_igraph_degree` + `apply_centrality_scores`.
- Group: `igraph_community_leiden` -> `compute_igraph_community_leiden` + `apply_community_membership`.
- Data: `igraph_ring` -> `compute_igraph_ring` + `apply_new_graph`.
- GPU: `lay_bcgl` -> `compute_layout_bcgl` + `apply_layout_bcgl` + `poll_bcgl_gpu`.

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
- Todos: `// TODO: desc` (sparse).

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

## Code Overview (Feature-to-File Map)

### Entry Point & App State

| File | Role |
|------|------|
| `src/main.c` | GLFW loop, state machine, file loading, input dispatch |
| `include/app_state.h` | `AppState` struct (ties together Renderer, GraphData, Camera, WindowState, WorkerThreadContext, AppContext) |

### Graph Core & Data

| File | Role |
|------|------|
| `include/graph/graph_types.h` | `Node`, `Edge`, `GraphData`, `GraphProperties`, `FilterableAttr`, `FilterLookup` |
| `src/graph/graph_core.c` | `graph_free_data`, `graph_build_visualization`, `graph_rebuild_edges` lifecycle |
| `include/graph/graph_core.h` | Graph lifecycle API |
| `src/graph/graph_io.c` | Graph file loading: `graph_load` (auto-detect .graphml/.gml), `graph_load_graphml`, `graph_load_gml` |
| `include/graph/graph_io.h` | I/O API |
| `src/graph/graph_actions.c` | High-level graph actions (filter, highlight) dispatched from AppState |
| `include/graph/graph_actions.h` | Graph actions API |

### Graph Filtering

| File | Role |
|------|------|
| `src/graph/graph_filter.c` | Node filtering by degree, coreness; infrastructure highlighting |
| `include/graph/graph_filter.h` | Filter API |
| `src/graph/graph_filter_visibility.c` | Attribute-based visibility filtering (show/hide by vertex attribute) |
| `include/graph/graph_filter_visibility.h` | `FilterContext` type, visibility filter API |

### Command Registry & Worker Thread

| File | Role |
|------|------|
| `src/graph/command_registry.c` | `g_command_registry[]` — all ~80 command definitions (`CommandDef` structs) |
| `include/graph/command_registry.h` | `CommandDef`, `IgraphWorkerFunc`, `IgraphApplyFunc`, `IgraphFreeFunc`, `IgraphGpuPollFunc` types |
| `src/graph/worker_thread.c` | Pthread job queue: submit, poll, cancel, progress/status/step handlers; igraph progress/status callbacks |
| `include/graph/worker_thread.h` | `WorkerJob`, `WorkerJobStatus`, `WorkerThreadContext` types, queue API |

### Layout Wrappers

| File | Role |
|------|------|
| `src/graph/wrappers_layout.c` | Worker functions for all 30+ layout algorithms (force-directed, tree, geometric, bipartite, MDS, dimension reduction, BCGL) |
| `include/graph/wrappers_layout.h` | Layout wrapper declarations, `apply_layout_matrix`, `free_layout_matrix`, `layout_center_and_autoscale` |
| `src/graph/layouts_force_fr.c` | Fruchterman-Reingold, Kamada-Kawai implementations |
| `src/graph/layouts_drl.c` | DrL (Distributed Recursive Layout) |
| `src/graph/layouts_davidson_harel.c` | Davidson-Harel |
| `src/graph/layouts_graphopt.c` | GraphOpt |
| `src/graph/layouts_gem.c` | GEM |
| `src/graph/layouts_forceatlas2.c` | ForceAtlas2 |
| `src/graph/layouts_yifan_hu.c` | Yifan Hu |
| `src/graph/layouts_bcgl.c` | BCGL (Binary Classification Graph Layout) CPU |
| `src/graph/layouts_vk_bcgl.c` | BCGL GPU compute wrapper |
| `src/graph/layouts_tree.c` | Reingold-Tilford, Sugiyama, Radial Sugiyama |
| `src/graph/layouts_basic.c` | Circle, Star, Grid, Sphere, Random |
| `src/graph/layouts_bipartite.c` | Bipartite layouts |
| `src/graph/layouts_mds.c` | Multidimensional Scaling (Torgerson, Spherical) |
| `src/graph/layouts_umap.c` | UMAP dimension reduction |
| `src/graph/layouts_tsne.c` | t-SNE (Barnes-Hut) |
| `src/graph/layouts_sugiyama.c` | Sugiyama layered layout |
| `src/graph/layouts_apply.c` | Layout application utilities |
| `src/graph/layered_sphere.c` | Layered Sphere custom layout |

### Analysis Wrappers

| File | Role |
|------|------|
| `src/graph/wrappers_centrality.c` | Centrality measures: degree, closeness, betweenness, eigenvector, PageRank, HITS, harmonic, strength, constraint, coreness |
| `include/graph/wrappers_centrality.h` | Centrality wrapper declarations, `apply_centrality_scores`, `centrality_scores_free` |
| `src/graph/wrappers_structural.c` | Global properties: density, transitivity, assortativity |
| `include/graph/wrappers_structural.h` | Structural wrapper declarations |
| `src/graph/wrappers_paths.c` | Path-based measures: diameter, radius, average path length |
| `include/graph/wrappers_paths.h` | Path wrapper declarations, `apply_info_card`, `info_card_free` |
| `src/graph/wrappers_community.c` | Community detection: Louvain, Leiden, Walktrap, Edge Betweenness, Fast Greedy, Infomap, Label Propagation, Spinglass, Leading Eigenvector, Optimal Modularity, Voronoi, Fluid Communities |
| `include/graph/wrappers_community.h` | Community wrapper declarations, `apply_community_membership`, `free_community_membership` |
| `src/graph/wrappers_cycles.c` | Cycle analysis: feedback arc set removal |
| `include/graph/wrappers_cycles.h` | Cycles wrapper declarations, `free_noop` |
| `src/graph/wrappers_splc.c` | SPLC (Search Path Link Count) animation + DAG level calculation |
| `include/graph/wrappers_splc.h` | SPLC declarations, `poll_splc_gpu` |

### Graph Generation Wrappers

| File | Role |
|------|------|
| `src/graph/wrappers_constructors.c` | Graph generation: deterministic (ring, star, tree, lattice, full, cycle, famous), stochastic (Erdos-Renyi, Barabasi, Watts-Strogatz, forest fire, random tree, degree sequence), bipartite, spatial |
| `include/graph/wrappers_constructors.h` | Constructor wrapper declarations, `apply_new_graph`, `free_new_graph` |

### Graph Repository

| File | Role |
|------|------|
| `src/graph/repo.c` | Shared repo utilities (cache dir, curl write callback) |
| `include/graph/repo.h` | `repo_cache_dir`, `repo_curl_write_cb` |
| `src/graph/repo_netzschleuder.c` | Static Netzschleuder catalog, download stub |
| `include/graph/repo_netzschleuder.h` | `StaticNetEntry`, catalog accessor, download API |
| `src/graph/netzschleuder_data.inc` | Auto-generated C initializers for 286 networks |

#### Regenerating the network catalog

The static catalog in `netzschleuder_data.inc` is generated from the Netzschleuder API:

```bash
curl -s 'https://networks.skewed.de/api/nets?full=True' \
| jq -r 'to_entries[]|(.key|@json)as$eid|(.value.title//.key|@json)as$title|(.value.nets[0]//.key|@json)as$ver|(.value.tags|join(",")|@json)as$tags|(if.value.analyses|type=="object"then if.value.analyses|has("num_vertices")then[.value.analyses.num_vertices,.value.analyses.num_edges]elif.value.nets[0]and.value.analyses[.value.nets[0]]then[.value.analyses[.value.nets[0]].num_vertices,.value.analyses[.value.nets[0]].num_edges]else[null,null]end else[null,null]end)as$stats|"\t{\($eid), \($title), \($ver), \($tags), \($stats[0]//0), \($stats[1]//0)},"' \
> src/graph/netzschleuder_data.inc
```

Requires `curl` and `jq`. Handles both single-version (flat `analyses`) and multi-version (keyed `analyses`) networks.

### Vulkan Renderer

| File | Role |
|------|------|
| `include/vulkan/renderer.h` | Public renderer API (`renderer_update_graph`, `renderer_render_ray`) |
| `include/vulkan/vulkan_types.h` | `Renderer`, `SPLCNode`, `SPLCEdge`, `BCGLNodeData`, `BCGLPushConstants`, `EdgeRoutingMode` types; all Vulkan struct definitions |
| `src/vulkan/renderer_lifecycle.c` | Renderer init, cleanup, window resize recreation |
| `include/vulkan/renderer_lifecycle.h` | Lifecycle API |
| `src/vulkan/renderer_draw.c` | Main draw loop, frame submission |
| `include/vulkan/renderer_draw.h` | Draw API |
| `src/vulkan/renderer_pipelines.c` | Pipeline creation for all render passes |
| `include/vulkan/renderer_pipelines.h` | Pipeline creation API |
| `src/vulkan/renderer_geometry.c` | Node/edge geometry buffer updates |
| `include/vulkan/renderer_geometry.h` | Geometry update API |
| `src/vulkan/renderer_labels.c` | Label rendering (atlas-based text) |
| `include/vulkan/renderer_labels.h` | Label rendering API |
| `src/vulkan/renderer_ui.c` | 2D HUD overlay rendering |
| `include/vulkan/renderer_ui.h` | UI rendering API |
| `src/vulkan/renderer_camera.c` | Camera uniform buffer updates |
| `include/vulkan/renderer_camera.h` | Camera rendering API |
| `src/vulkan/renderer_compute.c` | Compute dispatch (SPLC, routing, BCGL) |
| `include/vulkan/renderer_compute.h` | Compute dispatch API |
| `src/vulkan/renderer_init_splc_buffers.c` | SPLC compute buffer initialization |
| `include/vulkan/renderer_init_splc_buffers.h` | SPLC buffer init API |
| `src/vulkan/renderer_update_node_labels.c` | Dynamic node label updates |
| `include/vulkan/renderer_update_node_labels.h` | Label update API |
| `src/vulkan/renderer_xr.c` | XR framebuffers, depth buffers per view |
| `include/vulkan/renderer_xr.h` | XR rendering API |
| `src/vulkan/renderer_bcgl.c` | BCGL GPU compute pipeline |
| `include/vulkan/renderer_bcgl.h` | BCGL rendering API |
| `src/vulkan/device.c` | Vulkan physical/logical device selection, queue families |
| `include/vulkan/device.h` | Device API |
| `src/vulkan/swapchain.c` | Swapchain creation, image acquisition |
| `include/vulkan/swapchain.h` | Swapchain API |
| `src/vulkan/buffers.c` | Buffer creation, memory allocation, staging |
| `include/vulkan/buffers.h` | Buffer API |
| `src/vulkan/images.c` | Image creation, view creation, format utilities |
| `include/vulkan/images.h` | Image API |
| `src/vulkan/commands.c` | Command pool/buffer allocation, one-shot commands |
| `include/vulkan/commands.h` | Command API |
| `src/vulkan/render_pass.c` | Render pass creation |
| `include/vulkan/render_pass.h` | Render pass API |
| `src/vulkan/pipeline_graphics.c` | Graphics pipeline creation helpers |
| `include/vulkan/pipeline_graphics.h` | Graphics pipeline API |
| `src/vulkan/pipeline_compute.c` | Compute pipeline creation |
| `include/vulkan/pipeline_compute.h` | Compute pipeline API |
| `src/vulkan/pipeline_ui.c` | UI overlay pipeline |
| `include/vulkan/pipeline_ui.h` | UI pipeline API |
| `src/vulkan/menu.c` | Menu GPU buffer management (instanced quads, text) |
| `include/vulkan/menu.h` | Menu GPU API |
| `src/vulkan/text.c` | Font atlas (Inconsolata), text rendering helpers |
| `include/vulkan/text.h` | `TextRegion` type, text API |
| `src/vulkan/utils.c` | Vulkan utility functions |
| `include/vulkan/utils.h` | Utility API |
| `src/vulkan/app_path.c` | Application path resolution (XDG, installed) |
| `include/vulkan/app_path.h` | App path API |

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
| `shaders/bcgl.comp` | BCGL binary classification graph layout (GPU compute) |

### Interaction & Input

| File | Role |
|------|------|
| `src/interaction/window.c` | GLFW window lifecycle, fullscreen toggle, monitor cycling |
| `include/interaction/window.h` | `WindowState` struct, window creation and management API |
| `src/interaction/camera.c` | FPS camera (yaw/pitch, WASD, movement speed) |
| `include/interaction/camera.h` | `Camera` struct + API |
| `src/interaction/input.c` | Keyboard, mouse, gamepad input dispatch |
| `include/interaction/input.h` | Key mapping, gamepad deadzone, button state |
| `src/interaction/picking.c` | Ray-picking (node sphere, edge segment intersection) |
| `include/interaction/picking.h` | Pick result types, pick API |
| `src/interaction/gamepad.c` | Gamepad axis/button handling |
| `include/interaction/gamepad.h` | Gamepad API |
| `src/interaction/spatial.c` | Spatial basis calculation for menu spawning |
| `include/interaction/spatial.h` | `SpatialBasis` type, spatial API |
| `src/interaction/filter.c` | Filter UI interaction (attribute filter dispatch) |
| `include/interaction/filter.h` | Filter interaction API |

### Menu System

| File | Role |
|------|------|
| `src/interaction/menu.c` | Menu toggle, mouse/crosshair picking, hover clear |
| `include/interaction/menu.h` | `interaction_menu_toggle`, `interaction_pick_menu_node`, `raycast_menu_crosshair` |
| `src/ui/menu.c` | Menu tree construction from registry, 3D layout, rendering data |
| `include/ui/menu.h` | Menu tree API |

### Application State Machine

| File | Role |
|------|------|
| `src/interaction/state.c` | State transitions (`update_app_state`), command execution dispatch, menu selection handling |
| `include/interaction/state.h` | `AppContext`, `AppInteractionState`, `ExecutionContext`, `IgraphCommand`, `MenuNode`, `InfoCardData` types |

### UI Overlays

| File | Role |
|------|------|
| `src/ui/hud.c` | Heads-up display (FPS, job status, graph info) |
| `include/ui/hud.h` | HUD API |

### VR / XR (OpenXR)

| File | Role |
|------|------|
| `src/xr/openxr_context.c` | Instance, system, session management |
| `src/xr/openxr_vulkan.c` | Vulkan interop (graphics binding) |
| `src/xr/openxr_session.c` | Session lifecycle (begin/end/state) |
| `src/xr/openxr_input.c` | VR controller input, actions |
| `src/xr/openxr_view.c` | View configuration (eye poses) |
| `src/xr/openxr_frame.c` | Frame loop, predicted display times |
| `include/xr/openxr_context.h` | `XrContext` type, XR context API |
| `include/xr/openxr_frame.h` | XR frame API |

### Build System

| File | Role |
|------|------|
| `CMakeLists.txt` | Project config, dependency detection, shader compilation, OpenXR toggle |
| `shaders/compile.py` | SPIR-V compilation script |
