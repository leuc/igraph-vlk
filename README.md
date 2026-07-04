# igraph-vlk

3D network viewer based on [igraph](https://igraph.org/) and Vulkan, written in C.

Interactive graph visualization with
 - 35+ layout algorithms 2D & 3D
 - 12 community detection methods
 - 10 centrality measures
 - Realtime layout progression
 - Animated main path SPLC
 - Scales to very large graphs
 - Fast Barnes & Hut
 - VR support
 - 3D menu system
 - WASD + Gamepad navigation
 - Directly load graphs from Netzschleuder Repository
 - Runs on Steam Deck

## EXPERIMENTAL

Development is still very fluid and experimental.
The goal is a desktop app for exploring very large graphs.

The "hard" C and Vulkan code is written with AI support.
Be aware that the AI may have introduced errors.

Efforts are made to:
- Keep code structured and maintainable for humans
- Cross-check implementations with reference code and papers
- Test, compare and validate results

## Index

- [Inatall](#install)
- [Usage](#usage)
- [Features](#features)
  - [Graph I/O & Generation](#graph-io--generation)
  - [Graph Analysis](#graph-analysis)
  - [Graph Layout Algorithms](#graph-layout-algorithms)
  - [Rendering (Vulkan)](#rendering-vulkan)
  - [UI & Interaction](#ui--interaction)
  - [VR / XR (OpenXR)](#vr--xr-openxr)
  - [Background Worker Thread](#background-worker-thread)
- [Build Source](#build-source)
- [Architecture](#architecture)
- [Selected Scientific References](#selected-scientific-references)

## Install

- [Ubuntu Packages](https://github.com/leuc/igraph-vlk/releases)
- [Flatpak Bundle](https://github.com/leuc/io.github.leuc.igraph-vlk/releases)
- Arch AUR soon™

## Usage

### Mouse

| Action | Effect |
|---|---|
| Mouse move (locked) | Camera yaw/pitch look |
| Left-click | Pick nearest node or edge (select) |
| Left-click on menu item | Execute command or expand/collapse branch |

### Keyboard Shortcuts

| Key | Action |
|---|---|
| `W` / `A` / `S` / `D` | Camera movement (forward/left/back/right) |
| `Shift` | 3x camera speed |
| `Space` | Toggle 3D menu |
| `Escape` / `Q` | Quit |
| `N` | Toggle node visibility |
| `E` | Toggle edge visibility |
| `M` | Cycle edge routing mode (straight ↔ spherical PCB) |
| `H` | Toggle HUD overlay |
| `R` | Reset graph to original state |
| `1`–`9` | Filter nodes by minimum degree |
| `K` | Cycle k-core threshold filter |
| `J` | Highlight infrastructure (articulation points + bridge endpoints) |
| `+` / `=` | Increase layout scale (1.2x) |
| `-` | Decrease layout scale (1.2x) |
| `Alt`+`Enter` | Toggle fullscreen |
| `Alt`+`Left` / `Alt`+`Right` | Switch monitors in fullscreen |

### Gamepad

| Control | Action |
|---|---|
| Left stick | Movement (forward/back/strafe) |
| Right stick | Camera look |
| Start | Toggle menu |
| A | Activate crosshair-hovered menu item |
| L2 / R2 | Layout Scale |

### Graph Filtering

| Shortcut | Action |
|---|---|
| `1`–`9` | Remove nodes with degree < N and re-layout |
| `K` | Increment k-core threshold, remove nodes below it |
| `J` | Color articulation points red, bridge endpoints orange |

### Large Graphs

Vulkan itself can easily render 500k+ nodes/edges at 60fps even on moderate hardware.
However, not all graph methods and layouts scale to large graphs.

A typical workflow for large graphs:

1. Group with Leiden or Infomap
2. Rank by Degree, PageRank or K-core
2. Layout: try one of UMAP, Force Atlas 2, Yifan Hu, BCGL-t or t-SNE (in order of scale)

FA2, YH and t-SNE use Barnes & Hut and can run in parallel on CPU with OpenMP.

BCGL-t has a GPU only variant, but it can stall the GPU and lower FPS.

For 500k+ nodes/edges the app remains usable when the CPU computes and the GPU renders.

### Layered Spheres

The experimental **layered spheres** layout can very quickly render very large graphs.
It provides a quick **initial** data exploration with visual clustering and centered k-core order. It works best with graphs that have many communities. However, it is _not_ a general layout method that works with any graph.

### Layout seed

Most layout methods use a random initial placement of nodes.
Some layouts can use a custom /seed/ (node positions) for the initial placement.
`Menu/Layout/Seed/Use current node positions as seed` will enable the custom /seed/ for the following layouts.

- Fruchterman-Reingold
- Kamada-Kawai
- Davidson-Harel
- GEM
- Graphopt
- Yifan Hu
- Barnes-Hut t-SNE
- UMAP

This allows the combination or refinement of resulting layout node positions.

## Features

### Graph I/O & Generation

| Feature | Details |
|---|---|
| **GraphML import** | Load graphs via CLI argument |
| **Graph generation** | 7 deterministic (Ring, Star, K-ary Tree, Lattice, Clique, Cycle, Famous/Notable), 6 stochastic (Erdos-Renyi, Barabasi-Albert, Watts-Strogatz, Forest Fire, Random Tree, Degree Sequence), 2 bipartite (Random Bipartite, Bipartite Projection), 2 spatial (Geometric Random, Gabriel) |

### Graph Analysis

**Centrality & Roles** (results mapped to node color heatmap):

| Measure | Reference |
|---|---|
| Degree centrality | Freeman (1979) |
| Closeness centrality | Bavelas (1950); Sabidussi (1966) |
| Betweenness centrality | Brandes (2001) *A faster algorithm for betweenness centrality* |
| Eigenvector centrality | Bonacich (1972, 1987) |
| PageRank | Brin & Page (1998) *The Anatomy of a Large-Scale Hypertextual Web Search Engine* |
| HITS (Hub & Authority) | Kleinberg (1999) *Authoritative sources in a hyperlinked environment* |
| Harmonic centrality | Marchiori & Latora (2000); Rochat (2009) |
| Strength (weighted degree) | Barrat et al. (2004) |
| Burt's constraint (structural holes) | Burt (2004) *Structural holes and good ideas* |
| Coreness (k-core) | Batagelj & Zaversnik (2003) *An O(m) Algorithm for Cores Decomposition of Networks* |

**Global Properties** (displayed in info cards):

| Property | Reference |
|---|---|
| Diameter, Radius | Standard graph theory |
| Average path length | Standard graph theory |
| Assortativity (degree) | Newman (2002, 2003) *Assortative mixing in networks* |
| Edge density | Standard graph theory |
| Global transitivity (clustering coefficient) | Watts & Strogatz (1998) *Collective dynamics of small-world networks* |

**Community Detection** (results mapped to node colors):

| Algorithm | Reference |
|---|---|
| Louvain (Multilevel) | Blondel et al. (2008) *Fast unfolding of communities in large networks* |
| Leiden | Traag, Waltman & van Eck (2019) *From Louvain to Leiden: guaranteeing well-connected communities* |
| Walktrap | Pons & Latapy (2005) |
| Edge Betweenness (Girvan-Newman) | Girvan & Newman (2002) *Community Structure in Social and Biological Networks* |
| Fast Greedy | Clauset, Newman & Moore (2004) |
| Infomap | Rosvall & Bergstrom (2008) |
| Label Propagation | Raghavan, Albert & Kumara (2007) |
| Spinglass | Reichardt & Bornholdt (2006) |
| Leading Eigenvector | Newman (2006) *Finding community structure in networks using the eigenvectors of matrices* |
| Optimal Modularity | Brandes et al. (2008) |
| Voronoi Communities | Deritei et al. (2014); Molnar et al. (2024) |
| Fluid Communities | Pares et al. (2018) *Fluid Communities: A Competitive, Scalable and Diverse Community Detection Algorithm* |

**Cycle Analysis:**
- Remove feedback arc set — Eades, Lin & Smyth (1993); Baharev et al. (2021)

**GPU-Accelerated Main Path Analysis (SPLC):**
- Search Path Link Count traffic simulation on GPU compute shader with real-time animation

### Graph Layout Algorithms

Layouts run on a background thread with real-time snapshot polling for interactive convergence.

**Force-Directed (13):**

| Layout | 2D | 3D | Reference |
|---|---|---|---|
| Fruchterman-Reingold | ✓ | ✓ | Fruchterman & Reingold (1991) *Graph Drawing by Force-directed Placement* |
| Kamada-Kawai | ✓ | ✓ | Kamada & Kawai (1989) *An Algorithm for Drawing General Undirected Graphs* |
| DrL (Distributed Recursive Layout) | ✓ | ✓ | Graph layout |
| Davidson-Harel | ✓ | | Davidson & Harel (1996) *Drawing Graphs Nicely Using Simulated Annealing* |
| Graphopt | ✓ | | Graphopt (Schmuhl) |
| LGL (Large Graph Layout) | ✓ | | LGL |
| GEM | ✓ | | Frick, Ludwig & Mehldau (1995) |
| ForceAtlas2 | | ✓ | ForceAtlas2 |
| Yifan Hu | ✓ | ✓ | Hu (2005) *Efficient, High-Quality Force-Directed Graph Drawing* |

**Tree & Hierarchical (3):**
- Reingold-Tilford — Reingold & Tilford (1981) *Tidier drawing of trees*
- Sugiyama — Sugiyama, Tagawa & Toda (1981) *Methods for Visual Understanding of Hierarchical Systems*
- Radial Sugiyama — Bachmaier (2007) *A Radial Adaptation of the Sugiyama Framework*

**Geometric (8):**
- Circle (2D/3D), Star, Grid (2D/3D), Sphere, Random (2D/3D)

**MDS (3):**
- Torgerson MDS (2D) — Cox & Cox (1994)
- Torgerson MDS (3D) — Cox & Cox (1994)
- **Spherical MDS (3D)** — Miller, Huroyan & Kobourov (2023) *Spherical Graph Drawing by Multi-Dimensional Scaling*

**Dimension Reduction (4):**
- UMAP (2D/3D) — McInnes, Healy & Melville (2018) *UMAP: Uniform Manifold Approximation and Projection for Dimension Reduction*
- t-SNE Barnes-Hut (2D/3D) — Van der Maaten & Hinton (2008)

**Bipartite (2):**
- Sugiyama Bipartite, Simple Bipartite

**Binary Classification (3):**
- **BCGL-t (2D/3D)** — Yan, Zhao & Yang (2022) *BCGL: Binary Classification-Based Graph Layout*
- **BCGL-t (3D GPU Compute)** — GPU-accelerated iteration via compute shader with real-time convergence

**Custom Layout:**
- **Layered Sphere** — custom multi-sphere community-aware layout using Leiden CPM communities, k-core nucleus sorting, Fibonacci sphere + Hilbert curve slotting, iterative intra/inter-sphere geodesic optimization (OpenMP parallelized)

### Rendering (Vulkan)

- **8 graphics pipelines**: Node (instanced billboard quads with SDF shapes based on degree), Edge (straight/spherical PCB curved routing), Label (LOD-limited billboarded labels from dynamic atlas), UI (2D HUD overlay), Menu (instanced quads), Text Quad, Debug Ray
- **2 compute pipelines**: Spherical PCB edge routing (curved traces on sphere surface), SPLC traffic animation
- **SDF node shapes**: dot (deg 0), circle (deg 1), two-tone circle (deg 2), regular N-gon (deg 3+)
- **Edge routing modes**: Straight lines or GPU-computed spherical PCB traces (curved with stubs, highways, lat/lon sweeps)
- **Label LOD**: Barnes-Hut spatial index selects nearest 200 nodes per frame for labeling
- **Update-after-bind descriptor sets** for SSBO-backed edge weight updates
- **Triple-buffered graph updates** with ring of fences
- **Double-buffered command buffers** (MAX_FRAMES_IN_FLIGHT = 2)

### UI & Interaction

- **3D spherical menu system**: NeXTSTEP-style
- **Info cards**: Side-by-side key-value panels for global network property display
- **HUD overlay**: Node/edge count, degree filter, k-core filter, FPS, job progress, menu state

### VR / XR (OpenXR)

- Optional OpenXR integration (compile-time `USE_OPENXR`)
- CLI `--vr` flag to enable VR

### Background Worker Thread

All graph operations run on a dedicated pthread with a circular job queue. Features:
- igraph progress handler
- igraph status handler → job status messages
- igraph step handler → real-time layout snapshot polling

## Build Source

igraph-vlk builds against a patched `igraph` testing branch that includes experimental layout implementations (ForceAtlas2 2D/3D, Yifan Hu 2D/3D, Barnes-Hut t-SNE 2D/3D, Radial Sugiyama, Layered Sphere, Spherical MDS, BCGL-t).

Build has been tested on Ubuntu, Arch and macOS

### Build igraph testing branch
```sh
git clone https://github.com/leuc/igraph
cd igraph
git checkout testing
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=local_install -DIGRAPH_ENABLE_TLS=ON -DCMAKE_C_FLAGS="-O3 -march=native" -DCMAKE_CXX_FLAGS="-O3 -march=native"
cmake --build build/ --parallel --target install
cd ..
```

### Build igraph-vlk
```sh
git clone https://github.com/leuc/igraph-vlk
cd igraph-vlk
cmake -S . -B build -Digraph_ROOT=../igraph/local_install/ -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build/ --parallel
```

### Create .deb
```sh
cd build
cpack -G DEB -R ${VERSION}
```

### Run with OpenMP
```sh
OMP_NUM_THREADS=$(nproc) igraph-vlk /path/to/example.graphml
```

### Dependencies

| Dependency | Role |
|---|---|
| Vulkan SDK | Rendering API |
| GLFW | Window, input, OpenXR platform |
| igraph | Graph algorithms |
| cglm | 3D math |
| stb_truetype.h | Font render |
| curl | Graph Repository Download |
| zstd | Compressed Graphs |
| OpenMP (optional) | Parallelization |
| OpenXR (optional) | VR support |
| glslangValidator / glslc | SPIR-V shader compilation |
| gamecontrollerdb.txt | Gamepad Mapping |

## Architecture

```
src/
├── main.c                  # Entry point, GLFW loop, state machine
├── app_state.h             # Central app state (AppState)
├── graph/
│   ├── graph_data.c/h      # GraphData (nodes, edges, layout matrix)
│   ├── command_registry.c  # ~80 CommandDef entries
│   ├── wrappers_layout.c   # Layout wrapper functions
│   ├── wrappers_analysis.c # Centrality + global property wrappers
│   └── wrappers_generate.c # Graph generation wrappers
├── renderer/
│   ├── renderer_*.c/h      # Vulkan pipelines, buffers, swapchain
│   └── renderer_xr.c       # VR renderer setup
├── ui/
│   ├── menu.c/h            # 3D spherical menu tree + rendering
│   ├── camera.c/h          # FPS-style camera
│   ├── input.c/h           # Keyboard, mouse, gamepad
│   ├── pick.c/h            # Ray-picking
│   └── queue.c/h           # Background worker thread
└── xr/
    ├── openxr_*.c/h        # OpenXR context, session, input, frame
shaders/                    # GLSL → SPIR-V (auto-compiled)
include/                    # Public headers
```

## Selected Scientific References

igraph-vlk is built on igraph, which implements a vast body of network science algorithms. Below are key references for the algorithms used in this application.

igraph core:
> Csardi, G., & Nepusz, T. (2006). The igraph software package for complex network research. *InterJournal*, Complex Systems, 1695.

**Graph Repositories:**
- Tiago P. Peixoto, "The Netzschleuder network catalogue and repository", https://networks.skewed.de/ (2020).

**Layout:**
- Fruchterman, T.M.J. & Reingold, E.M. (1991). Graph Drawing by Force-directed Placement. *Software — Practice and Experience*, 21/11, 1129–1164.
- Kamada, T. & Kawai, S. (1989). An Algorithm for Drawing General Undirected Graphs. *Information Processing Letters*, 31/1, 7–15.
- Sugiyama, K., Tagawa, S. & Toda, M. (1981). Methods for Visual Understanding of Hierarchical Systems. *IEEE Trans. Systems, Man and Cybernetics*, 11(2), 109–125.
- Reingold, E. & Tilford, J. (1981). Tidier Drawing of Trees. *IEEE Trans. Software Engineering*, SE-7(2), 223–228.
- Davidson, R. & Harel, D. (1996). Drawing Graphs Nicely Using Simulated Annealing. *ACM Trans. Graphics*, 15(4), 301–331.
- McInnes, L., Healy, J. & Melville, J. (2018). UMAP: Uniform Manifold Approximation and Projection for Dimension Reduction. *arXiv:1802.03426*.
- Bachmaier, C. (2007). A Radial Adaptation of the Sugiyama Framework for Visualizing Hierarchical Information. *IEEE Trans. Vis. Comp. Graphics*, 13(3), 583–594.

**Centrality:**
- Brandes, U. (2001). A faster algorithm for betweenness centrality. *J. Mathematical Sociology*, 25(2), 163–177.
- Brin, S. & Page, L. (1998). The Anatomy of a Large-Scale Hypertextual Web Search Engine. *Proc. 7th WWW Conference*.
- Kleinberg, J. (1999). Authoritative sources in a hyperlinked environment. *J. ACM*, 46(5), 604–632.
- Burt, R.S. (2004). Structural holes and good ideas. *American J. Sociology*, 110, 349–399.

**Community Detection:**
- Blondel, V.D., Guillaume, J.-L., Lambiotte, R. & Lefebvre, E. (2008). Fast unfolding of communities in large networks. *J. Statistical Mechanics*, P10008.
- Traag, V.A., Waltman, L. & van Eck, N.J. (2019). From Louvain to Leiden: guaranteeing well-connected communities. *Scientific Reports*, 9, 5233.
- Girvan, M. & Newman, M.E.J. (2002). Community structure in social and biological networks. *PNAS*, 99, 7821–7826.
- Raghavan, U.N., Albert, R. & Kumara, S. (2007). Near linear time algorithm to detect community structures in large-scale networks. *Phys. Rev. E*, 76, 036106.
- Pares, F. et al. (2018). Fluid Communities: A Competitive, Scalable and Diverse Community Detection Algorithm. *Complex Networks & Their Applications VI*, Springer, 229.
- Newman, M.E.J. (2006). Finding community structure in networks using the eigenvectors of matrices. *Phys. Rev. E*, 74, 036104.

**Random Graph Models:**
- Barabasi, A.-L. & Albert, R. (1999). Emergence of scaling in random networks. *Science*, 286, 509–512.
- Watts, D.J. & Strogatz, S.H. (1998). Collective dynamics of "small-world" networks. *Nature*, 393, 440–442.
- Erdos, P. & Renyi, A. (1959). On random graphs. *Publicationes Mathematicae*, 6, 290–297.

**Transitivity:**
- Watts, D.J. & Strogatz, S.H. (1998). Collective dynamics of small-world networks. *Nature*, 393, 440–442.

**Assortativity:**
- Newman, M.E.J. (2002). Assortative mixing in networks. *Phys. Rev. Lett.*, 89, 208701.
- Newman, M.E.J. (2003). Mixing patterns in networks. *Phys. Rev. E*, 67, 026126.

See [AGENTS.md](AGENTS.md) for build details, code style, and contribution guide.
