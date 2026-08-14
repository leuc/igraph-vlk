# Graph Code Documentation

Per-directory guide for `src/graph/` and `include/graph/`. See root `AGENTS.md` for build, test, and style rules.

## Core, I/O, Visual State

| Files | Role |
|------|------|
| `graph_types.h` | `Node`, `Edge`, `GraphData`, graph properties, and filter metadata |
| `graph_core.c/.h` | Graph lifecycle, visualization construction, and edge rebuilding |
| `graph_io.c/.h` | GraphML/GML loading, `.zst`/`.zstd` decompression, and dated GraphML saving |
| `graph_actions.c/.h` | App-level filter, highlight, simplify, date cleanup, and directedness actions |
| `graph_filter.c/.h` | Destructive degree/k-core pruning and articulation/bridge highlighting |
| `graph_filter_visibility.c/.h` | Reversible node/edge visibility and filterable string/Boolean attribute discovery |
| `graph_animation.c/.h` | Shared reveal and emphasis animation construction |
| `graph_color.c/.h` | Score heatmaps, community colors, and shared color constants |

## Commands and Worker Execution

| Files | Role |
|------|------|
| `command_registry.c/.h` | Complete static command registry, dynamic-command anchors, parameters, GPU poll callbacks, and transition durations |
| `worker_thread.c/.h` | Single pthread job queue, cancellation, progress/status callbacks, result polling, and GPU-job handoff |
| `wrappers_filter.c/.h` | Dynamic Filter node/edge commands and Show All actions |

`CommandDef.worker_func` receives `ExecutionContext *`. Access the current graph through `ctx->app_state->current_graph.g`, command parameters through `ctx->params`, and cancellation through `ctx->running`. The worker owns its result until the main-thread apply callback consumes it; `free_func` must handle every completed or cancelled result.

## Layouts

| Files | Role |
|------|------|
| `wrappers_layout.h`, `layouts_apply.c` | Layout declarations, matrix application, center/autoscale, cleanup, and renderer transitions |
| `layouts_seed.c` | Current-position seed toggle and uniform, bounded, and normal random seeds |
| `layouts_force_fr.c` | Fruchterman–Reingold and Kamada–Kawai, 2D/3D |
| `layouts_drl.c`, `layouts_davidson_harel.c`, `layouts_graphopt.c`, `layouts_gem.c` | DrL, Davidson–Harel, GraphOpt/LGL, and GEM |
| `layouts_forceatlas2.c`, `layouts_yifan_hu.c` | ForceAtlas2 3D and Yifan Hu 2D/3D |
| `layouts_tree.c` | Reingold–Tilford |
| `layouts_sugiyama.c` | Sugiyama and Radial Sugiyama |
| `layouts_basic.c`, `layouts_bipartite.c` | Geometric and bipartite placements |
| `layouts_mds.c`, `layouts_umap.c`, `layouts_tsne.c` | Torgerson/Spherical MDS, UMAP, and Barnes–Hut t-SNE |
| `layouts_bcgl.c`, `layouts_vk_bcgl.c` | BCGL-t CPU and GPU-compute variants |
| `layered_sphere.c/.h` | Static Layered Sphere state machine |
| `layered_sphere_common.c`, `layered_sphere_common.h` | Sphere grids, ordering, placement, movement, and cleanup shared by static/dynamic layouts |

The menu exposes 37 layout algorithms plus four seed controls. Add new layout callbacks to `wrappers_layout.h`, their implementation file, `CMakeLists.txt`, and `g_command_registry[]`.

## Rank, Group, Structure, Alter

| Files | Role |
|------|------|
| `wrappers_centrality.c/.h` | Degree, Closeness, Betweenness, Eigenvector, PageRank, HITS Hub/Authority, Harmonic, Strength, Constraint, Coreness, CD Index, Edge Betweenness, Convergence Degree, Edge Trussness |
| `wrappers_community.c/.h` | Louvain, Leiden, Walktrap, Edge Betweenness, Fast Greedy, Infomap, Label Propagation, Spinglass, Leading Eigenvector, Optimal Modularity, Voronoi, Fluid Communities |
| `wrappers_structural.c/.h` | Graph properties, density, transitivity, and assortativity info cards |
| `wrappers_paths.c/.h` | Diameter, radius, average path length, BFS/DFS/topological reveal, info-card allocation/application |
| `wrappers_cycles.c/.h` | Feedback-arc removal, simplify, empty-date removal, and directed/undirected conversions |

Rank and Group commands normally persist calculated attributes so GraphML saving and dynamic Filter menus can use them. Preserve igraph direction, loop, multiedge, and weight semantics unless the command explicitly documents a conversion.

## Follow and Main Path

| Files | Role |
|------|------|
| `wrappers_flow.c/.h` | Sampled reachable-pair maximum flow and reveal animation |
| `rotor_routing.c/.h`, `wrappers_rotor_routing.c/.h` | Single-chip rotor walk and finite-host rotor aggregation with reveal/intensity application |
| `wrappers_kcore_tree.c/.h` | Chunked k-core hierarchy construction and reveal ordering |
| `wrappers_path_cover.c/.h` | Minimum path cover, maximum antichain, and minimum chain cover; cyclic directed inputs lose an approximate feedback arc set |
| `main_path.c/.h` | DAG preparation, six GPU weighting modes, Basket/Global callbacks, and result application |
| `main_path_cache.c/.h` | Weight, strength, and selection attribute cache validation/loading |
| `main_path_search.c/.h` | CPU Local, Backward Local, Multiple, Key-Route, and SPLC Valued Network selections |

Main Path is a two-step workflow: run weighting, then a selection under the same method. It requires a non-empty directed DAG. GPU weighting is advanced per frame through `poll_main_path_weighting`; CPU selections consume the persisted weight/strength attributes.

## Graph Generation and Repository

| Files | Role |
|------|------|
| `wrappers_constructors.c/.h` | Ring, Star, Tree, Lattice, Full, Circle, Famous, six random models, random bipartite, projection, geometric random, and Gabriel commands |
| `repo.c/.h` | Cache directory and shared curl helpers |
| `repo_netzschleuder.c/.h` | Netzschleuder catalogue access, download, cache, and graph replacement |
| `netzschleuder_data.inc` | Generated static catalogue initializers |

Famous and Repository leaves are populated dynamically in `src/ui/menu.c`; their registry entries have parameters and no fixed display name. Dynamic population receives `MenuState` so tree changes invalidate menu layout and text.

To regenerate the catalogue:

```bash
curl -s 'https://networks.skewed.de/api/nets?full=True' \
| jq -r 'to_entries[]|(.key|@json)as$eid|(.value.title//.key|@json)as$title|(.value.nets[0]//.key|@json)as$ver|(.value.tags|join(",")|@json)as$tags|(if.value.analyses|type=="object"then if.value.analyses|has("num_vertices")then[.value.analyses.num_vertices,.value.analyses.num_edges]elif.value.nets[0]and.value.analyses[.value.nets[0]]then[.value.analyses[.value.nets[0]].num_vertices,.value.analyses[.value.nets[0]].num_edges]else[null,null]end else[null,null]end)as$stats|"\t{\($eid), \($title), \($ver), \($tags), \($stats[0]//0), \($stats[1]//0)},"' \
> src/graph/netzschleuder_data.inc
```

## Live NCOL Streaming

| Files | Role |
|------|------|
| `stream.c/.h` | Main-thread NCOL ingest, name index, incremental graph buffers, pause/live-layout toggles, and visual updates |
| `ncol_parse.c/.h` | Strict in-place `name1 name2 [weight]` parsing |
| `dyn_k-core.c/.h` | Exact insertion-only coreness maintenance |
| `dyn_core_tree.c/.h` | Insertion-only nested k-core hierarchy maintenance |
| `dyn_core_tree_order.c/.h` | Incremental barycentric ordering within core-tree nodes |
| `dyn_leiden.c/.h` | Dynamic-frontier CPM Leiden maintenance |
| `community_simhash.c/.h` | Stable order-independent community fingerprints and colors |
| `dyn_layered_sphere.c/.h` | Incremental Layered Sphere placement from core-tree and Leiden changes |
| `dyn_ls_sphere_rotation.c/.h` | Per-sphere rotation fitting |

Only `src/os/stream.c` reads stdin on its thread. `graph_stream_poll()` is the single main-thread consumer and the only streaming path allowed to mutate igraph or `GraphData`.

## Current Menu Shape

| Root | Coverage |
|------|----------|
| `Data` | Patterns, Famous, Random, Bipartite, Spatial, Repository, Stream |
| `Layout` | Seed, Force-Directed, Hierarchical, Geometric, Bipartite, Embedding, Binary Classification, Layered Sphere |
| `Rank` | 15 node/edge measures |
| `Group` | 12 community methods |
| `Follow` | Reveal speed, BFS, DFS, topological order, rotor routing/aggregation, K-Core Tree, Main Path, sampled max flow, path/antichain/chain covers |
| `Structure` | Diameter, radius, average path length, assortativity, density, transitivity |
| `Filter` | Dynamic Node and Edge attribute filters plus Show All |
| `Alter` | Feedback-arc removal, simplify, empty-date removal, directedness conversions |
| Root leaves | Properties, Save, Quit |

## Adding a Menu Command

1. Implement `void *compute_*(ExecutionContext *ctx)` and matching apply/free callbacks in the appropriate module. Check all igraph initialization and algorithm returns.
2. Declare the callbacks in the matching header and add a new source file to `CMakeLists.txt` if needed.
3. Register a unique ID and display name in `g_command_registry[]`. Set `param_defs`, `gpu_poll_func`, and `transition_duration` only when used.
4. Reuse `apply_layout_matrix`, `apply_centrality_scores`, `apply_community_membership`, `apply_new_graph`, or `apply_info_card` where their ownership contract fits.
5. For a new dynamic family, add population/clear logic to `src/ui/menu.c`; static leaves need no UI code.
6. Build and run relevant automated tests. Do not launch the application; menu and interactive verification is left to the user.
