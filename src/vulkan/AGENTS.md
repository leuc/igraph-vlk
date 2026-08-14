# Vulkan Renderer Documentation

Per-directory guide for `src/vulkan/`, `include/vulkan/`, and `shaders/`. See root `AGENTS.md` for build, test, and style rules.

## Renderer Lifecycle and Frames

| Files | Role |
|------|------|
| `renderer.h`, `vulkan_types.h` | Public graph/ray update API and aggregate renderer/resource state |
| `renderer_lifecycle.c/.h` | Renderer initialization, frame-idle waits, and resize recreation |
| `renderer_cleanup.c/.h` | Central destruction of renderer, compute, menu, label, swapchain, and optional XR resources |
| `renderer_draw.c/.h` | Desktop frame acquisition, linear scene recording, submission, and presentation |
| `renderer_present.c/.h`, `color_space.c/.h` | SDR/HDR10 output transform, luminance policy, presentation descriptors, and fullscreen pipeline |
| `renderer_camera.c/.h` | Per-view camera uniform updates |
| `renderer_pipelines.c/.h` | Creation of graph, label, ray, menu, text, and compute pipelines/descriptors |
| `renderer_xr.c/.h` | XR swapchain image framebuffers and depth resources |

Keep resource creation and destruction paired. Before replacing buffers or pipelines used by in-flight frames, use the existing fence/frame-idle helpers.

## Graph Geometry, Labels, UI

| Files | Role |
|------|------|
| `renderer_geometry.c/.h` | Node/edge GPU geometry and attribute uploads, including routed edges |
| `renderer_labels.c/.h` | Label atlas instances, Barnes–Hut visibility selection, and detail-card support |
| `renderer_update_node_labels.c/.h` | Dynamic node/detail label refresh and positioning |
| `renderer_ui.c/.h` | HUD background/text instance upload |
| `menu.c/.h` | Menu text cache, dirty-scene upload, persistent GPU buffers, descriptors, and lifecycle |
| `menu_scene.c/.h` | CPU menu traversal and card, row, arrow, text, and info-card instance construction |
| `text.c/.h` | Inconsolata atlas creation, glyph caching, text measurement, and text regions |

Menu tree structure and transforms live in `src/ui/menu.c`; picking and dirty revisions live in interaction state. Build the CPU scene before replacing GPU data. Menu buffers grow geometrically, remain allocated while hidden, and may be changed only after in-flight frames are idle. Refresh text descriptors only when the atlas image revision changes.

## Animation, Transitions, GPU Analysis

| Files | Role |
|------|------|
| `renderer_anim.c/.h` | Node/edge reveal clips, strengths, ownership, descriptor buffers, and per-frame animation state |
| `renderer_anim_values.c/.h` | Pure strength and reveal-time conversions used by renderer code and tests |
| `renderer_transition.c/.h` | Snapshot/retarget/advance of smooth node and edge layout morphs |
| `renderer_compute.c/.h` | Spherical-PCB edge-routing compute context and dispatch |
| `renderer_bcgl.c/.h` | BCGL-t GPU buffers, dispatch, poll, readback, and cleanup |
| `renderer_criticality.c/.h` | Main Path GPU weighting pipeline, DAG level sweeps, tiled NPPC reachability, readback, cancellation |
| `criticality_types.h` | CPU/shader-shared Main Path modes, stages, push constants, headers, flags, and buffer offsets |

Main Path supports SPLC, Unit, SPC, SPE, NPPC, and SPNP. Weighting progresses non-blockingly through per-frame polls; NPPC may span multiple tiled submissions. Keep all C structs, offsets, workgroup assumptions, and enums synchronized with `shaders/main_path.comp` and `tests/criticality_test*`.

## Vulkan Infrastructure

| Files | Role |
|------|------|
| `device.c/.h` | Physical/logical device selection, features, queues, and extensions |
| `swapchain.c/.h`, `surface_format.c/.h` | Swapchain lifecycle, format choice, and effective surface/display HDR10 capability |
| `buffers.c/.h` | Host/device buffer allocation, staging, copy, map/update helpers |
| `images.c/.h` | Image allocation, views, formats, and transitions |
| `commands.c/.h` | Command pools/buffers and one-shot command helpers |
| `render_pass.c/.h` | Linear desktop scene targets plus swapchain presentation passes |
| `pipeline_graphics.c/.h` | Graphics shader/module/pipeline helpers |
| `pipeline_compute.c/.h` | Compute shader/module/pipeline helpers |
| `pipeline_ui.c/.h` | Vertex formats and pipelines for HUD/menu/text quads |
| `utils.c/.h` | Vulkan result checks and shared macros/helpers |

There is no `src/vulkan/app_path.c`; platform resource paths are owned by `src/os/path.c`.

## Shaders

Shaders are compiled to `build/shaders/*.spv` by CMake.

| Shaders | Role |
|------|------|
| `node.vert`, `node.frag` | Billboard nodes, visibility/reveal state, SDF shape and color |
| `edge.vert`, `edge.frag` | Straight/routed edge geometry, animation strength, visibility, emphasis |
| `label.vert`, `label.frag` | Billboarded atlas labels |
| `ui.vert`, `ui.frag` | HUD overlay |
| `menu.vert`, `menu.frag` | Menu and info-card backgrounds |
| `textquad.vert`, `textquad.frag` | Menu/info-card atlas text |
| `ray.vert`, `ray.frag` | VR/debug controller ray |
| `routing.comp` | Spherical-PCB edge routing |
| `bcgl.comp` | BCGL-t layout optimization |
| `main_path.comp` | Six Main Path weighting modes, Basket/Global data, and presentation strength |

When a shader interface changes, update the matching C layout, descriptor writes, pipeline setup, compile definition/path in `CMakeLists.txt`, and relevant headless tests. Do not launch the application for visual or performance testing; leave that to the user.
