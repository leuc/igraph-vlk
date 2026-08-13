# UI Documentation

Per-directory guide for `src/ui/` and `include/ui/`. See root `AGENTS.md` for build, test, and style rules.

## Menu System

| Files | Role |
|------|------|
| `menu.c/.h` | Registry-to-tree construction, dynamic leaves, dirty layout, card sizing, spherical transforms, row visibility, lookup, and cleanup |
| `menu_metrics.c/.h` | Shared row, title, padding, text-scale, card, and arrow dimensions |
| `hud.c/.h` | FPS, graph/stream state, worker status/progress, and menu-state overlay text |

`init_menu_tree()` converts `g_command_registry[]` category paths into branches and static command leaves. Registry order controls menu order.

Three families are populated separately:

- `menu_populate_famous_graphs()` creates all named igraph Famous leaves under `Data/Famous`.
- `menu_populate_netzschleuder_static()` groups static catalogue entries by tag under `Data/Repository` and includes node/edge counts in labels.
- `menu_populate_attribute_filters()` and `menu_populate_attribute_edge_filters()` rebuild `Filter/Node` and `Filter/Edge` from eligible string/Boolean attributes, including Show All.

Call the corresponding clear functions before rebuilding attribute menus after graph replacement or attribute changes. Dynamic leaves reuse parameterized `CommandDef` entries from the registry; do not create a second execution path in UI code.

## Layout and Rendering Boundary

- `menu_update_layout()` runs only when `MenuState.layout_revision` changes. It lays out the expanded path and caches row/card geometry used by drawing and picking.
- Menu width derives from label text. Keep shared dimensions in `MenuMetrics`; do not duplicate them in the renderer.
- Interaction code owns raycasts and activation. UI code owns tree structure and geometry.
- CPU render-scene construction and Vulkan upload live in `src/vulkan/menu_scene.c` and `src/vulkan/menu.c`.
- Dynamic population functions receive `MenuState` and invalidate layout and text after tree changes.
- App-level open, hover, active-card, revision, and info-card state lives in `AppContext.menu`; `MenuNode` owns tree, expansion, visibility, and shared row geometry.

Do not launch the application for visual verification; leave menu and HUD checks to the user.
