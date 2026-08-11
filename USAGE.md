# Usage

This guide follows the menu shown by the current application. Author–year citations resolve in the single linked bibliography at the end of the [README](README.md#scientific-references). Standard graph operations and project-specific features are marked as such.

## Start

```sh
igraph-vlk [--vr] [graph.graphml|graph.gml|graph.graphml.zst|graph.gml.zst]
```

- A file argument loads GraphML or GML, including files ending in `.zst` or `.zstd`.
- `--vr` is available only in builds compiled with OpenXR support.
- Piped stdin takes priority over a file argument and starts [live NCOL streaming](#live-ncol-streaming).
- With no file or pipe, open `Data` in the menu to generate or download a graph.

## Controls

### Mouse and keyboard

| Control | Action |
|---|---|
| Mouse move | Look around; the pointer is the center crosshair |
| Left-click | Select the node or edge under the crosshair; activate a hovered menu row when the menu is open |
| Mouse wheel | Scale the layout |
| `W` / `A` / `S` / `D` | Move forward / left / back / right |
| `Shift` | Move three times faster |
| `Space` | Open or close the menu |
| `Escape` | Cancel the running background job |
| `Q` | Quit |
| `N` / `E` | Show or hide all nodes / edges |
| `M` | Switch between straight and spherical-PCB edge routing |
| `H` | Show or hide the HUD |
| `R` | Reload the original file and discard changes; use only for a file-loaded graph |
| `1`–`9` | Permanently remove nodes below that degree, simplify, and reset the layout |
| `K` | Raise the destructive k-core threshold from 0 to 20, then wrap to 0; wrapping does not restore deleted nodes |
| `J` | Highlight articulation points and bridge endpoints |
| `+` / `=` / `-` | Increase / decrease layout scale |
| `Alt`+`Enter` | Toggle fullscreen |
| `Alt`+`Left` / `Alt`+`Right` | Change monitor while fullscreen |

Degree and k-core shortcuts change the graph. Use `Filter` for a reversible visibility filter. Save first if you may need the current graph.

### Gamepad

| Control | Action |
|---|---|
| Left stick | Move |
| Right stick | Look |
| Start | Open or close the menu |
| A | Activate the crosshair-hovered menu row |
| L2 / R2 | Scale the layout |

## How menu commands behave

- `Data` replaces the current graph. `Data > Repository` downloads once and then uses its local cache.
- `Layout` changes coordinates only. Rank and Group commands change visual encoding and store calculated attributes for saving or filtering.
- `Follow` animates an order or highlights a selected subnetwork.
- `Filter` changes visibility only. `Alter` changes the graph itself and cannot be undone except by reloading a file.
- One background job runs at a time. The HUD shows progress; `Escape` requests cancellation.
- `Save` writes a dated GraphML file to the desktop and preserves graph, vertex, and edge attributes.

## Menu reference

### Data

| Menu | Features | Use |
|---|---|---|
| `Data > Patterns` | Ring, Star, Tree, Lattice, Full Graph (Clique), Circle | Small deterministic examples; these are standard graph families. |
| `Data > Famous` | 31 named graphs, from Bull through Zachary | Built-in benchmark and teaching graphs. Selecting one replaces the current graph. |
| `Data > Random` | Erdős–Rényi GNP/GNM (Erdős & Rényi); Barabási–Albert (Barabási & Albert); Watts–Strogatz (Watts & Strogatz); Forest Fire (Leskovec et al.); Random Tree; Degree Sequence configuration model (Bollobás) | Uses fixed built-in parameters. The Erdős–Rényi command currently runs GNP; GNM is not a separate choice. |
| `Data > Bipartite` | Generate Random Bipartite; Create Bipartite Projections (Borgatti & Everett) | Projection requires a bipartite current graph. It replaces the graph with the first projection. |
| `Data > Spatial` | Geometric random graph; Gabriel graph (Gabriel & Sokal) | Generates spatial proximity graphs with fixed built-in parameters. |
| `Data > Repository` | Tagged catalogue of Netzschleuder networks (Peixoto) | Requires network access for the first download. Labels show node and edge counts. |
| `Data > Stream` | Pause; Live Layered Sphere | Available in stdin-streaming mode. Pause stops applying queued lines; it does not discard them. |

The 31 Famous entries are Bull, Chvatal, Coxeter, Cubical, Diamond, Dodecahedral, Folkman, Franklin, Frucht, Grotzsch, Heawood, Herschel, House, HouseX, Icosahedral, Krackhardt_Kite, Levi, McGee, Meredith, Noperfectmatching, Nonline, Octahedral, Petersen, Robertson, Smallestcyclicgroup, Tetrahedral, Thomassen, Tutte, Uniquely3colorable, Walther, and Zachary.

### Layout

All layouts operate on the current graph. Two-dimensional layouts are shown in the 3D viewer on a plane.

| Menu | Features | Notes and scientific basis |
|---|---|---|
| `Layout > Seed` | Use current positions; Random Uniform; Random Bounded; Random Normal | Random entries immediately replace positions. The checkbox supplies current positions to compatible iterative layouts; see [layout seeding](#layout-seeding). |
| `Layout > Force-Directed` | Fruchterman–Reingold 2D/3D; Kamada–Kawai 2D/3D; DrL 2D/3D (Martin et al.); Davidson–Harel; GraphOpt; LGL; GEM; ForceAtlas2 3D; Yifan Hu 2D/3D | General-purpose choices. GraphOpt is an implementation-origin method without a canonical paper. |
| `Layout > Hierarchical` | Reingold–Tilford; Sugiyama; Radial Sugiyama | Best when direction or hierarchy matters. Reingold–Tilford treats edges as undirected; Sugiyama can route cyclic directed graphs by reversing selected edges for layout. |
| `Layout > Geometric` | Circle 2D/3D, Sphere, Star, Grid 2D/3D, Random 2D/3D | Standard direct placements; fast baselines for any graph. |
| `Layout > Bipartite` | Sugiyama (Bipartite), Bipartite (Simple) | Require a bipartite graph. |
| `Layout > Embedding` | Torgerson MDS 2D/3D; Spherical MDS 3D; UMAP 2D/3D; Barnes–Hut t-SNE 2D/3D | — |
| `Layout > Binary Classification` | BCGL-t 2D/3D and 3D GPU Compute | The GPU variant can reduce frame rate while it runs. |
| `Layout > Layered Sphere` | Community-aware layered spherical layout | Project-specific method: Leiden groups, k-core ordering, and spherical optimization. Best for graphs with several communities; not a general-purpose layout. |

### Rank

Rank commands map node scores to node color and size, except the three edge measures, which style edges. A numeric `weight` edge attribute is used where the method supports weights.

| Feature | Meaning and scientific basis | Important condition |
|---|---|---|
| Degree | Number of incident edges (Freeman) | Loops count. |
| Closeness | Inverse distance to other nodes (Bavelas) | Uses all edge directions and normalized scores. |
| Betweenness | Share of shortest paths through a node (Brandes) | Can be slow on large graphs. |
| Eigenvector Centrality | Importance from important neighbors (Bonacich) | Uses all edge directions. |
| PageRank | Random-surfer importance, damping 0.85 (Brin & Page) | Respects direction on directed graphs. |
| HITS Hub / Authority | Good linkers / good targets (Kleinberg) | On undirected graphs both reduce to eigenvector centrality. |
| Harmonic | Distance-based reachability that handles disconnected graphs (Marchiori & Latora) | Uses all edge directions. |
| Strength | Weighted degree (Barrat et al.) | Without a numeric `weight`, it equals degree. |
| Constraint | Burt structural-hole constraint (Burt) | Lower values indicate less constrained brokerage. |
| Coreness | Highest k-core containing each node (Batagelj & Zaveršnik) | Direction is ignored; loops and parallel edges retain igraph semantics. |
| CD Index | Citation disruption (Funk & Owen-Smith) | Requires a directed loop-free graph and a valid `date` string on every vertex in `YYYY-MM-DD` form. Uses a 182-day window. Run `Alter > Simplify` first if needed. |
| Edge Betweenness | Share of shortest paths through an edge (Brandes) | Can be slow on large graphs. |
| Convergence Degree | Whether shortest-path flow converges or diverges across an edge (Bányai et al.) | Edge intensity shows magnitude; `Filter > Edge > convergence` exposes convergent, divergent, and neutral classes. |
| Edge Trussness | Highest triangle-supported k-truss containing an edge (Wang & Cheng) | Requires a simple graph; simplify first if loops or parallel/mutual edges are present. |

### Group

Group commands color nodes by community. Results may vary between runs for stochastic methods.

| Feature | Scientific basis | Practical note |
|---|---|---|
| Louvain (Multilevel) | Blondel et al. | Fast modularity baseline; intended for undirected graphs. |
| Leiden | Traag et al. | CPM objective; supports the app’s static and streaming workflows. |
| Walktrap | Pons & Latapy | Random-walk method for undirected graphs. |
| Edge Betweenness | Girvan & Newman | Expensive on large graphs. |
| Fast Greedy | Clauset et al. | Fast undirected modularity method. |
| Infomap | Rosvall & Bergstrom | Strong choice for flow and directed networks. |
| Label Propagation | Raghavan et al. | Fast; results can change between runs. |
| Spinglass | Reichardt & Bornholdt | Slower; best for small undirected graphs. |
| Leading Eigenvector | Newman | Spectral modularity split; intended for undirected graphs. |
| Optimal Modularity | Brandes et al. | Exact and exponential; use only on small graphs. |
| Voronoi | Lázár et al.; Molnár et al. | Supports directed, weighted networks. |
| Fluid Communities | Parés et al. | Requires a connected simple undirected graph. The app chooses the group count from graph size. |

### Follow

Follow commands animate order or emphasize a subnetwork; they do not create a new layout.

| Feature | Use and scientific basis | Important condition |
|---|---|---|
| Breadth-first / Depth-first search | Reveal from the highest-degree node using standard BFS / DFS | Traverses all edge directions. Unreachable nodes remain hidden during the reveal. |
| Topological sort | Reveal in topological order (Kahn) | Requires a directed acyclic graph (DAG). |
| K-Core Tree | Reveal a project-specific dynamic hierarchy based on k-core decomposition (Batagelj & Zaveršnik) | Direction is ignored. |
| Max Flow (Sampled Pairs) | Aggregate flow over sampled reachable source–target pairs (Goldberg & Tarjan) | Requires a directed graph with at least two nodes and one edge. `weight` is treated as capacity when present. |
| Minimum Path Cover | Reveal a smallest set of paths covering a DAG | Requires a directed graph. If it has cycles, the command deletes an approximate feedback arc set first. |
| Maximum Antichain | Highlight a largest set of pairwise incomparable DAG nodes (Dilworth) | Same cycle-removal behavior as Minimum Path Cover; writes an `antichain` filter attribute. |
| Minimum Chain Cover | Reveal a smallest chain cover (Dilworth) | Same cycle-removal behavior as Minimum Path Cover. |

#### Main Path Analysis

Main Path Analysis is a two-step workflow for directed acyclic citation or dependency networks (Hummon & Doreian; Batagelj).

1. Confirm `Properties` reports `Directed: Yes` and `DAG: Yes`. If the graph has cycles, run `Alter > Remove feedback arc set`; this permanently deletes edges.
2. Under `Follow > Main Path Analysis > Step 1: Weighting`, run one method: SPLC, Unit, SPC, SPE, NPPC, or SPNP. SPLC, NPPC, and SPNP originate with Hummon and Doreian; SPC is formalized by Batagelj; SPE is an entropy/log-space variant (Price & Evans). NPPC has the highest memory and time cost; use SPE if count weights overflow.
3. Under the matching method in `Step 2: Selection`, choose a result. Selection must use the same weighting already run.

| Selection | Result |
|---|---|
| Basket | Near-optimal important nodes rather than one path (Price & Evans) |
| Global Path | Maximum total-weight path |
| Local | Greedy forward path from sources |
| Backward Local | Greedy reverse path from sinks |
| Multiple (20%) | Forward branches within 20% of each local best |
| Key-Route (K=10) | Forward and backward paths through the 10 highest-weight edges (Liu & Lu) |
| Valued Network | SPLC only; stochastic path sampling and tie-frequency thresholding (Hummon & Carley) |

Weighting is cached as graph attributes. Altering the graph invalidates the workflow; rerun Step 1 before another selection.

### Structure

`Properties` at the menu root reports vertex/edge counts, directedness, DAG/acyclic state, connectivity, simplicity, and whether a `weight` edge attribute exists.

| Feature | Meaning and scientific basis |
|---|---|
| Diameter / Radius / Average Path Length | Standard shortest-path summaries. Numeric `weight` values are treated as distances. Disconnected pairs are omitted. |
| Assortativity | Degree mixing; positive values favor similar degrees (Newman). |
| Density | Present edges as a fraction of possible non-loop edges. |
| Transitivity (undirected) | Global triangle clustering (Watts & Strogatz); direction is ignored. |

### Filter

`Filter > Node` and `Filter > Edge` are populated from string or Boolean attributes that are present on every item and have a manageable number of distinct values. Choose an attribute value to show only matches; choose `Show All` to restore visibility. Numeric attributes are not listed.

Analysis can add filterable categories. Examples include `cd-index-type`, edge `convergence`, `antichain`, and main-path selection flags.

### Alter and root commands

| Feature | Effect | Important condition |
|---|---|---|
| Remove feedback arc set | Deletes a heuristic feedback set to make a directed graph acyclic (Eades et al.) | Destructive; intended before Main Path Analysis. |
| Simplify | Removes all self-loops and parallel edges | Destructive; combined edge attributes are not preserved by a merge rule. |
| Remove Nodes with Empty `date` | Deletes vertices with missing or empty date strings | Requires a `date` vertex attribute. Destructive; use before CD Index, then ensure every remaining date is `YYYY-MM-DD`. |
| To directed | Gives each undirected edge one arbitrary direction | Destructive; arbitrary directions are not inferred from dates or attributes. |
| To undirected (collapse) | Collapses directed edges between a pair into one undirected edge | Destructive. |
| To undirected (mutual) | Keeps only pairs that have edges in both directions | Destructive; one-way edges are dropped. |
| Save | Writes dated GraphML to the desktop | Save before destructive commands. |
| Quit | Closes the application | `Q` is the keyboard equivalent. |

## Notable workflows

### Large graphs

Start with methods that scale well:

1. Group with Leiden, Infomap, Label Propagation, or Louvain.
2. Rank with Degree, PageRank, Coreness, or Strength.
3. Layout with Layered Sphere, UMAP, ForceAtlas2, Yifan Hu, BCGL-t, or Barnes–Hut t-SNE.

Avoid Optimal Modularity, Spinglass, Edge Betweenness, NPPC, and exact-looking global analyses until the graph is small enough. CPU layouts can leave the GPU free to render; GPU BCGL can temporarily lower FPS.

### Layout seeding

Enable `Layout > Seed > Use current positions as seed`, then run a compatible layout: Fruchterman–Reingold, Kamada–Kawai, Davidson–Harel, GEM, GraphOpt, Yifan Hu, Barnes–Hut t-SNE, or UMAP. This refines or combines layouts. Disable the checkbox when you want a fresh start.

### Live NCOL streaming

When stdin is not a terminal, the app starts with an empty undirected graph and reads whitespace-separated lines:

```text
name1 name2 [weight]
```

Nodes are created on first mention. Bad lines are logged and skipped. A running worker job pauses ingest application; queued lines are applied afterward. The stream maintains coreness (Sarıyüce et al.), Leiden communities (Sahu), and the optional live Layered Sphere incrementally.

```sh
printf 'a b 1\nb c 2\nc a\n' | ./igraph-vlk
```

[`scripts/bsky_reply_graph.jq`](scripts/bsky_reply_graph.jq) converts Bluesky Jetstream replies into NCOL edges:

```sh
curl -sN "wss://jetstream2.us-east.bsky.network/subscribe?wantedCollections=app.bsky.feed.post" \
  | ./scripts/bsky_reply_graph.jq \
  | ./igraph-vlk
```
