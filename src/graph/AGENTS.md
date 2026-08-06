# Graph Code Documentation

Per-directory guide for `src/graph/` and `include/graph/`. See root `AGENTS.md` for build/lint/testing/style. When editing: mimic neighbors; lint/format/build; no regressions.

## Graph Core & Data

| File | Role |
|------|------|
| `include/graph/graph_types.h` | `Node`, `Edge`, `GraphData`, `GraphProperties`, `FilterableAttr`, `FilterLookup` |
| `src/graph/graph_core.c` | `graph_free_data`, `graph_build_visualization`, `graph_rebuild_edges` lifecycle |
| `include/graph/graph_core.h` | Graph lifecycle API |
| `src/graph/graph_io.c` | Graph file loading: `graph_load` (auto-detect .graphml/.gml), `graph_load_graphml`, `graph_load_gml` |
| `include/graph/graph_io.h` | I/O API |
| `src/graph/graph_actions.c` | High-level graph actions (filter, highlight) dispatched from AppState |
| `include/graph/graph_actions.h` | Graph actions API |

## Graph Filtering

| File | Role |
|------|------|
| `src/graph/graph_filter.c` | Node filtering by degree, coreness; infrastructure highlighting |
| `include/graph/graph_filter.h` | Filter API |
| `src/graph/graph_filter_visibility.c` | Attribute-based visibility filtering (show/hide by vertex attribute) |
| `include/graph/graph_filter_visibility.h` | `FilterContext` type, visibility filter API |

## Command Registry & Worker Thread

| File | Role |
|------|------|
| `src/graph/command_registry.c` | `g_command_registry[]` — all ~80 command definitions (`CommandDef` structs) |
| `include/graph/command_registry.h` | `CommandDef`, `IgraphWorkerFunc`, `IgraphApplyFunc`, `IgraphFreeFunc`, `IgraphGpuPollFunc` types |
| `src/graph/worker_thread.c` | Pthread job queue: submit, poll, cancel, progress/status/step handlers; igraph progress/status callbacks |
| `include/graph/worker_thread.h` | `WorkerJob`, `WorkerJobStatus`, `WorkerThreadContext` types, queue API |

## Layout Wrappers

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

## Analysis Wrappers

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
| `src/graph/main_path.c` | Main Path preparation, criticality weighting, selection, and result application |
| `include/graph/main_path.h` | Main Path command callbacks and selection APIs |

## Graph Generation Wrappers

| File | Role |
|------|------|
| `src/graph/wrappers_constructors.c` | Graph generation: deterministic (ring, star, tree, lattice, full, cycle, famous), stochastic (Erdos-Renyi, Barabasi, Watts-Strogatz, forest fire, random tree, degree sequence), bipartite, spatial |
| `include/graph/wrappers_constructors.h` | Constructor wrapper declarations, `apply_new_graph`, `free_new_graph` |

## Graph Repository

| File | Role |
|------|------|
| `src/graph/repo.c` | Shared repo utilities (cache dir, curl write callback) |
| `include/graph/repo.h` | `repo_cache_dir`, `repo_curl_write_cb` |
| `src/graph/repo_netzschleuder.c` | Static Netzschleuder catalog, download stub |
| `include/graph/repo_netzschleuder.h` | `StaticNetEntry`, catalog accessor, download API |
| `src/graph/netzschleuder_data.inc` | Auto-generated C initializers for 286 networks |

### Regenerating the network catalog

The static catalog in `netzschleuder_data.inc` is generated from the Netzschleuder API:

```bash
curl -s 'https://networks.skewed.de/api/nets?full=True' \
| jq -r 'to_entries[]|(.key|@json)as$eid|(.value.title//.key|@json)as$title|(.value.nets[0]//.key|@json)as$ver|(.value.tags|join(",")|@json)as$tags|(if.value.analyses|type=="object"then if.value.analyses|has("num_vertices")then[.value.analyses.num_vertices,.value.analyses.num_edges]elif.value.nets[0]and.value.analyses[.value.nets[0]]then[.value.analyses[.value.nets[0]].num_vertices,.value.analyses[.value.nets[0]].num_edges]else[null,null]end else[null,null]end)as$stats|"\t{\($eid), \($title), \($ver), \($tags), \($stats[0]//0), \($stats[1]//0)},"' \
> src/graph/netzschleuder_data.inc
```

Requires `curl` and `jq`. Handles both single-version (flat `analyses`) and multi-version (keyed `analyses`) networks.

## Adding Menu Commands

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
   - For GPU-accelerated commands, add `gpu_poll_func` as 7th arg (e.g., `poll_bcgl_gpu`, `poll_main_path_weighting`). Pass `NULL` for CPU-only.
   - `category_path`: / separated folders (creates tree).
   - `command_id`: Unique ID for lookup.
   - Auto-sorted by registry order.

4. **Execution Flow**:
   - Select leaf -> `node->command->cmd_def` -> `worker_thread_submit_job` queues `worker_func` (compute).
   - Complete -> `apply_func` updates state -> renderer refresh.
   - For GPU jobs: `gpu_poll_func` is called per-frame until it returns `true`.
   - Background thread prevents UI freeze.

The menu tree auto-builds in `src/ui/menu.c`; menu interaction lives in `src/interaction/menu.c` — see those directories' AGENTS.md.

### Examples
- Layouts: `lay_force_fr` -> `compute_igraph_layout_fruchterman_reingold_3d` + `apply_layout_matrix`.
- Rank: `igraph_degree` -> `compute_igraph_degree` + `apply_centrality_scores`.
- Group: `igraph_community_leiden` -> `compute_igraph_community_leiden` + `apply_community_membership`.
- Data: `igraph_ring` -> `compute_igraph_ring` + `apply_new_graph`.
- GPU: `lay_bcgl` -> `compute_layout_bcgl` + `apply_layout_bcgl` + `poll_bcgl_gpu`.

Rebuild & run to see menu update.
