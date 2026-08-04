# Interaction Documentation

Per-directory guide for `src/interaction/` and `include/interaction/`. See root `AGENTS.md` for build/lint/testing/style. When editing: mimic neighbors; lint/format/build; no regressions.

## Window, Camera, Input

| File | Role |
|------|------|
| `src/interaction/window.c` | GLFW window lifecycle, fullscreen toggle, monitor cycling |
| `include/interaction/window.h` | `WindowState` struct, window creation and management API |
| `src/interaction/camera.c` | FPS camera (yaw/pitch, WASD, movement speed) |
| `include/interaction/camera.h` | `Camera` struct + API |
| `src/interaction/input.c` | Keyboard, mouse, gamepad input dispatch |
| `include/interaction/input.h` | Key mapping, gamepad deadzone, button state |
| `src/interaction/gamepad.c` | Gamepad axis/button handling |
| `include/interaction/gamepad.h` | Gamepad API |

## Picking

| File | Role |
|------|------|
| `src/interaction/picking.c` | Ray-picking (node sphere, edge segment intersection) |
| `include/interaction/picking.h` | Pick result types, pick API |

## Spatial & Filter

| File | Role |
|------|------|
| `src/interaction/spatial.c` | Spatial basis calculation for menu spawning |
| `include/interaction/spatial.h` | `SpatialBasis` type, spatial API |
| `src/interaction/filter.c` | Filter UI interaction (attribute filter dispatch) |
| `include/interaction/filter.h` | Filter interaction API |

## Menu Interaction

| File | Role |
|------|------|
| `src/interaction/menu.c` | Menu toggle, mouse/crosshair picking, hover clear |
| `include/interaction/menu.h` | `interaction_menu_toggle`, `interaction_pick_menu_node`, `raycast_menu_crosshair` |

- `interaction_menu_toggle(AppState *state)`: Opens/closes the spherical menu.
- `raycast_menu_crosshair(AppState *state)`: Desktop/gamepad picking. The cursor is captured while the menu is open, so the crosshair is the pointer — never pick from `glfwGetCursorPos()`.
- `raycast_menu_vr(AppState *state, ray_ori, ray_dir)`: VR controller picking.
- Both resolve hover into `AppContext.menu.hovered_node`. Input sources (mouse press, gamepad A, VR select) **activate that node**; they must not cast rays of their own, or one press selects two different rows.
- Only nodes with `MenuNode.is_visible` are pickable. It is recomputed every frame by `update_menu_transforms()` and marks the rows actually laid out and drawn, so collapsed subtrees and the root's non-existent row can never be hit.
- All app-level menu state (`root`, `active_level`, `hovered_node`, `spawn_basis`, `info_card`, `is_open`) lives in `AppContext.menu` (`MenuState`, `include/interaction/state.h`). Per-node flags/geometry stay on `MenuNode`.

## Application State Machine

| File | Role |
|------|------|
| `src/interaction/state.c` | State transitions (`update_app_state`), command execution dispatch, menu selection handling |
| `include/interaction/state.h` | `AppContext`, `AppInteractionState`, `ExecutionContext`, `IgraphCommand`, `MenuNode`, `InfoCardData` types |
