# Interaction Documentation

Per-directory guide for `src/interaction/` and `include/interaction/`. See root `AGENTS.md` for build, test, and style rules.

## Files

| Files | Role |
|------|------|
| `window.c/.h` | GLFW lifecycle, focus handling, fullscreen, monitor cycling, and window state |
| `camera.c/.h` | FPS camera movement, yaw/pitch vectors, and speed modifiers |
| `input.c/.h` | GLFW callbacks, keyboard/mouse dispatch, controller mapping load, and continuous input |
| `gamepad.c/.h` | Mapped and raw-controller polling, deadzones, movement/look, menu activation, and layout scaling |
| `picking.c/.h` | Node sphere, edge segment, and menu quad ray intersections; object selection/detail cards |
| `spatial.c/.h` | Camera-relative basis and world placement used when the menu opens |
| `menu.c/.h` | Menu toggle, desktop crosshair ray, VR controller ray, and hover clearing |
| `state.c/.h` | Command parameters, menu/info-card types, app state transitions, command submission/application, and Quit |

There is no interaction-layer filter module. Dynamic filter construction is in `src/ui/menu.c`; filter execution is in `src/graph/wrappers_filter.c` and `graph_filter_visibility.c`.

## Input Ownership

- Desktop and gamepad menu targeting use the center crosshair. Do not use `glfwGetCursorPos()` for menu activation.
- VR targeting uses the controller ray.
- Ray updates set `AppContext.menu.hovered_node`. Mouse, gamepad A, and VR select activate that existing target; activation must not cast a second ray.
- Only `MenuNode.is_visible` rows are pickable. `src/ui/menu.c:update_menu_transforms()` recomputes visibility and geometry every frame.
- Object picking is available only in graph view. Menu activation and parameter selection are routed through `handle_menu_selection()`.

## Current Controls

| Input | Feature |
|------|---------|
| Mouse / WASD / Shift | Look, move, and accelerate |
| Mouse wheel, `+`, `-` | Layout scale |
| Space | Toggle menu |
| Left click / gamepad A | Select object or activate hovered menu row |
| Escape | Request worker cancellation |
| Q | Quit |
| N / E / M / H | Nodes, edges, routing mode, HUD toggles |
| R | Reload the original file-backed graph |
| 1–9 / K | Destructive degree and k-core pruning |
| J | Highlight articulation points and bridge endpoints |
| Alt+Enter / Alt+Left / Alt+Right | Fullscreen and monitor controls |
| Gamepad sticks / Start / triggers | Move/look, menu toggle, and layout scale |

Keep this table synchronized with `input.c` and `gamepad.c`. Do not launch the application to test controls; interactive verification belongs to the user.

## Menu and Application State

All app-level menu state lives in `AppContext.menu`: root, active branch, hovered row, captured spatial basis, info card, and open flag. Per-row geometry and expansion/visibility flags stay on `MenuNode`.

The state machine covers graph view, open menu, parameter selection, command execution, background jobs, and displayed results. `update_app_state()` is the only normal path for polling job completion and applying/freeing results. Preserve the split between worker-thread computation and main-thread UI/renderer mutation.
