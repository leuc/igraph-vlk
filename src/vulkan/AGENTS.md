# Vulkan Renderer Documentation

Per-directory guide for `src/vulkan/` and `include/vulkan/`. See root `AGENTS.md` for build/lint/testing/style. When editing: mimic neighbors; lint/format/build; no regressions.

## Renderer Files

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

## Shaders

Shaders auto-compile to SPIRV in `build/shaders/*.spv` using glslangValidator.

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
