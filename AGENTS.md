# Agents Build & Code Documentation

This document serves as the primary guide for AI coding agents and developers working on the `igraph-vlk` project. It outlines the build process and provides a comprehensive map of the codebase.

## Build Instructions

To configure and compile the project, run the following build command from the root of the repository. This command configures CMake to use the local `igraph` installation and then builds the project:

```bash
cmake -S . -B build -Digraph_ROOT=../igraph-1.0.1/local_install/ && cmake --build build/ --parallel
```

### Notes on Building

* Ensure that the `igraph` is build thread-safe: `cmake .. -DCMAKE_INSTALL_PREFIX=... -DIGRAPH_ENABLE_TLS=ON`

---

## Code Overview (Architecture Map)

The project is a Vulkan-based 3D graph visualization tool that utilizes the `igraph` library for graph algorithms and layouts. The codebase is organized into modular directories under `src/` (implementation) and `include/` (headers).

### 1. Application Entry

* **`src/main.c`**: The entry point of the application. Initializes the window, Vulkan renderer, user interaction states, and the main application loop.
* **`include/app_state.h`**: Defines the global application state shared across different subsystems.

### 2. Graph Subsystem (`src/graph/` & `include/graph/`)

Handles all graph data structures, layout calculations, and interactions with the external `igraph` library.

* **Core Data**: `graph_core.c`, `graph_types.h` - Graph data models, node/edge definitions, and core graph management.
* **File I/O**: `graph_io.c` - Loading and saving graph data formats.
* **Algorithms & Layouts**:
* `graph_layout.c`, `layout_openord.c`, `layered_sphere.c` - Logic for calculating node positions in 3D space.
* `graph_analysis.c`, `graph_clustering.c`, `graph_filter.c` - Graph analysis, clustering algorithms, and filtering nodes/edges based on criteria.


* **Igraph Wrappers**: `wrappers.c`, `wrappers_layout.c` - Bridge code that interfaces directly with the C `igraph` library.
* **Concurrency & Commands**: `worker_thread.c`, `command_registry.c`, `graph_actions.c` - Offloads heavy graph computations to background threads to prevent UI freezing, and manages executable graph commands.

### 3. Rendering Engine (`src/vulkan/` & `include/vulkan/`)

The Vulkan backend responsible for drawing the 3D graph, UI, and handling GPU compute.

* **Vulkan Core**: `vulkan_device.c`, `vulkan_swapchain.c`, `vulkan_render_pass.c`, `vulkan_commands.c` - Vulkan boilerplate, logical device creation, and frame presentation.
* **Renderers**: `renderer.c`, `renderer_geometry.c`, `renderer_ui.c`, `renderer_compute.c`, `renderer_pipelines.c` - The main rendering loops and dispatchers for 3D geometry, 2D overlays, and compute shaders.
* **Pipelines**: `pipeline_graphics.c`, `pipeline_compute.c`, `pipeline_ui.c` - Definitions for the Vulkan graphics and compute pipelines.
* **Assets & Helpers**: `polyhedron.c` (3D shapes/spheres for nodes), `text.c` (font rendering), `animation_manager.c` (smooth transitions for layouts), `utils.c`.

### 4. User Interaction (`src/interaction/` & `include/interaction/`)

Handles user inputs, navigation, and object selection.

* **`camera.c`**: 3D camera management (panning, zooming, rotating).
* **`input.c`**: Mouse and keyboard event handling.
* **`picking.c`**: Raycasting/selection logic to identify which nodes or edges the user is clicking on.
* **`state.c`**: Interaction state machine management.

### 5. User Interface (`src/ui/` & `include/ui/`)

Overlay and menu components rendered on top of the 3D scene.

* **`hud.c`**: Heads-Up Display showing active stats, modes, or selected node information.
* **`menu.c`**: Application menus for triggering layouts, loading files, or changing settings.

### 6. Shaders (`shaders/`)

GLSL shader files used by the Vulkan pipelines.

* **Graphics**: `shader.vert/.frag`, `edge.vert/.frag`, `label.vert/.frag`, `transparent_sphere.vert/.frag` - Defines how nodes, edges, and text are drawn on the GPU.
* **UI**: `ui.vert/.frag`, `menu.vert/.frag` - 2D overlay shaders.
* **Compute**: `routing.comp`, `routing_hub.comp` - Compute shaders, likely used for parallelizing edge routing calculations or physics-based graph layouts on the GPU.
