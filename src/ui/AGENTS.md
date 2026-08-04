# UI Documentation

Per-directory guide for `src/ui/` and `include/ui/`. See root `AGENTS.md` for build/lint/testing/style. When editing: mimic neighbors; lint/format/build; no regressions.

## Menu System

| File | Role |
|------|------|
| `src/ui/menu.c` | Menu tree construction from registry, 3D layout, rendering data |
| `include/ui/menu.h` | Menu tree API |

- Parses `g_command_registry[]` (`src/graph/command_registry.c`), creates `MenuNode` tree (branches/folders, leaves/commands).
- Renders instanced quads + labels billboarded to camera.
- Hover/expand animation.
- GPU buffer management for the rendered menu lives in `src/vulkan/menu.c`.

## UI Overlays

| File | Role |
|------|------|
| `src/ui/hud.c` | Heads-up display (FPS, job status, graph info) |
| `include/ui/hud.h` | HUD API |
