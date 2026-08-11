# UI Documentation

Per-directory guide for `src/ui/` and `include/ui/`. See root `AGENTS.md` for build, test, and style rules.

## Menu System

| Files | Role |
|------|------|
| `menu.c/.h` | Registry-to-tree construction, dynamic leaves, card sizing, spherical transforms, row visibility, labels, lookup, and cleanup |
| `hud.c/.h` | FPS, graph/stream state, worker status/progress, and menu-state overlay text |

`init_menu_tree()` converts `g_command_registry[]` category paths into branches and static command leaves. Registry order controls menu order.

Three families are populated separately:

- `menu_populate_famous_graphs()` creates all named igraph Famous leaves under `Data/Famous`.
- `menu_populate_netzschleuder_static()` groups static catalogue entries by tag under `Data/Repository` and includes node/edge counts in labels.
- `menu_populate_attribute_filters()` and `menu_populate_attribute_edge_filters()` rebuild `Filter/Node` and `Filter/Edge` from eligible string/Boolean attributes, including Show All.

Call the corresponding clear functions before rebuilding attribute menus after graph replacement or attribute changes. Dynamic leaves reuse parameterized `CommandDef` entries from the registry; do not create a second execution path in UI code.

## Layout and Rendering Boundary

- `update_menu_transforms()` lays out only the expanded path, sets `is_visible`, and caches row/card geometry used by both drawing and picking.
- Menu width derives from cached label text. Keep card dimensions and constants consistent with `src/vulkan/menu.c`.
- Interaction code owns raycasts and activation. UI code owns tree structure and geometry.
- Vulkan buffer allocation, instanced card/text upload, and info-card drawing live in `src/vulkan/menu.c`.
- App-level open/hover/active-card state lives in `AppContext.menu`; `MenuNode` owns only tree and per-row state.

Do not launch the application for visual verification; leave menu and HUD checks to the user.
