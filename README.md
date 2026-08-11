# igraph-vlk

3D network viewer based on [igraph](https://igraph.org/) and Vulkan, written in C.

Interactive graph visualization with
 - 37 layout algorithms in 2D and 3D
 - 12 community detection methods
 - 15 node and edge ranking measures
 - Realtime layout progression
 - Six main-path weighting methods and seven selection modes
 - Scales to very large graphs
 - Fast Barnes & Hut
 - VR support
 - 3D menu system
 - WASD + Gamepad navigation
 - Directly load graphs from Netzschleuder Repository
 - Live graph streaming from stdin (NCOL firehose)
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

## [Usage Guide](USAGE.md)

## Index

- [Install](#install)
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
- [Scientific references](#scientific-references)

## Install

- [Ubuntu Packages](https://github.com/leuc/igraph-vlk/releases)
- [Flatpak Bundle](https://github.com/leuc/io.github.leuc.igraph-vlk/releases)
- Arch AUR soon™

## Features

The tables below summarize the complete user-facing menu. See the [Usage Guide](USAGE.md#menu-reference) for purpose, prerequisites, and destructive-operation warnings. Author–year citations resolve in the bibliography at the end of this README.

| Menu | Available features |
|---|---|
| `Data` | Patterns: Ring, Star, Tree, Lattice, Full Graph, Circle; 31 Famous graphs; Random: Erdős–Rényi, Barabási–Albert, Watts–Strogatz, Forest Fire, Random Tree, Degree Sequence; Random Bipartite, Bipartite Projection; Geometric Random, Gabriel; Netzschleuder Repository; stream Pause and Live Layered Sphere |
| `Layout` | Seed: current, uniform, bounded, normal; Force-Directed: Fruchterman–Reingold 2D/3D, Kamada–Kawai 2D/3D, DrL 2D/3D, Davidson–Harel, GraphOpt, LGL, GEM, ForceAtlas2 3D, Yifan Hu 2D/3D; Hierarchical: Reingold–Tilford, Sugiyama, Radial Sugiyama; Geometric: Circle 2D/3D, Sphere, Star, Grid 2D/3D, Random 2D/3D; Bipartite: Sugiyama and Simple; Embedding: MDS 2D/3D, Spherical MDS, UMAP 2D/3D, Barnes–Hut t-SNE 2D/3D; BCGL-t 2D/3D/GPU; Layered Sphere |
| `Rank` | Degree, Closeness, Betweenness, Eigenvector, PageRank, HITS Hub, HITS Authority, Harmonic, Strength, Constraint, Coreness, CD Index, Edge Betweenness, Convergence Degree, Edge Trussness |
| `Group` | Louvain, Leiden, Walktrap, Edge Betweenness, Fast Greedy, Infomap, Label Propagation, Spinglass, Leading Eigenvector, Optimal Modularity, Voronoi, Fluid Communities |
| `Follow` | BFS, DFS, Topological Sort, K-Core Tree; Main Path Analysis with SPLC, Unit, SPC, SPE, NPPC, or SPNP weighting and Basket, Global, Local, Backward Local, Multiple 20%, Key-Route K=10, or SPLC Valued Network selection; Sampled Max Flow; Minimum Path Cover; Maximum Antichain; Minimum Chain Cover |
| `Structure` | Diameter, Radius, Average Path Length, Assortativity, Density, Transitivity |
| `Filter` | Reversible node and edge visibility by string or Boolean attribute, plus Show All |
| `Alter` | Remove feedback arc set, Simplify, remove empty-date nodes, convert to directed, convert to undirected by collapse or mutual edges |
| Menu root | Properties, Save GraphML, Quit |

### Graph I/O & Generation

| Feature | Details |
|---|---|
| **GraphML and GML import** | Load plain or Zstandard-compressed files via a CLI argument |
| **GraphML export** | Save a dated file with graph, vertex, and edge attributes to the desktop |
| **Live NCOL streaming (stdin)** | Auto-detected when stdin is piped/redirected — grows the graph live from NCOL-format lines (`name1 name2 [weight]`) as they arrive; ingest pauses while a worker job is running |
| **Graph generation** | 7 deterministic (Ring, Star, K-ary Tree, Lattice, Clique, Cycle, Famous/Notable), 6 stochastic (Erdos-Renyi, Barabasi-Albert, Watts-Strogatz, Forest Fire, Random Tree, Degree Sequence), 2 bipartite (Random Bipartite, Bipartite Projection), 2 spatial (Geometric Random, Gabriel) |
| **Network repository** | Browse and cache tagged networks from Netzschleuder |

### Graph Analysis

**Centrality & Roles** (results mapped to node color heatmap):

| Measure | Reference |
|---|---|
| Degree centrality | Freeman (1979) |
| Closeness centrality | Bavelas (1950); Sabidussi (1966) |
| Betweenness centrality | Brandes (2001) *A faster algorithm for betweenness centrality* |
| Eigenvector centrality | Bonacich (1972) |
| PageRank | Brin & Page (1998) *The Anatomy of a Large-Scale Hypertextual Web Search Engine* |
| HITS (Hub & Authority) | Kleinberg (1999) *Authoritative sources in a hyperlinked environment* |
| Harmonic centrality | Marchiori & Latora (2000) |
| Strength (weighted degree) | Barrat et al. (2004) |
| Burt's constraint (structural holes) | Burt (2004) *Structural holes and good ideas* |
| Coreness (k-core) | Batagelj & Zaversnik (2003) *An O(m) Algorithm for Cores Decomposition of Networks* |
| CD Index (citation disruption) | Funk & Owen-Smith (2017) |
| Edge betweenness | Brandes (2001) |
| Convergence degree | Bányai, Négyessy & Bazsó (2011) |
| Edge trussness | Wang & Cheng (2012) |

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
| Voronoi Communities | Lázár et al. (2017); Molnár et al. (2024) |
| Fluid Communities | Pares et al. (2018) *Fluid Communities: A Competitive, Scalable and Diverse Community Detection Algorithm* |

**Main Path Analysis:**

- Six GPU weighting modes: SPLC, Unit, SPC, SPE, NPPC, and SPNP. Foundational references: Hummon & Doreian (1989), Batagelj (2003), and Price & Evans (2025).
- Selection modes: Basket, Global Path, Local, Backward Local, Multiple (20%), Key-Route (K=10), and SPLC Valued Network. Search variants follow Liu & Lu (2012) and Hummon & Carley (1993).
- Requires a directed acyclic graph and a completed matching weighting step. See the [two-step usage workflow](USAGE.md#main-path-analysis).

**Dynamic (Streaming) k-Core Maintenance:**
- Maintains exact coreness as live NCOL edges arrive, updating only the affected subcore instead of decomposing the whole graph again.
- Self-loops and parallel edges keep igraph's coreness semantics. See the [streaming workflow](USAGE.md#live-ncol-streaming).
- Reference: Sarıyüce et al. (2013).

**Dynamic (Streaming) Leiden Communities:**
- Maintains CPM Leiden communities as live edges arrive. A dynamic frontier limits work to affected vertices and their neighbors.
- Refinement and aggregation update changed communities while preserving Leiden's well-connected-community guarantee. See the [streaming workflow](USAGE.md#live-ncol-streaming).
- References: Sahu (2024a, 2024b).
- Static method: Traag, Waltman & van Eck (2019).


### Graph Layout Algorithms

Layouts run on a background thread with real-time snapshot polling for interactive convergence.

**Force-Directed (13):**

| Layout | 2D | 3D | Reference |
|---|---|---|---|
| Fruchterman-Reingold | ✓ | ✓ | Fruchterman & Reingold (1991) *Graph Drawing by Force-directed Placement* |
| Kamada-Kawai | ✓ | ✓ | Kamada & Kawai (1989) *An Algorithm for Drawing General Undirected Graphs* |
| DrL (Distributed Recursive Layout) | ✓ | ✓ | Martin et al. (2008) |
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
- Torgerson MDS (2D) — Torgerson (1952)
- Torgerson MDS (3D) — Torgerson (1952)
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

See [AGENTS.md](AGENTS.md) for build details, code style, and contribution guide.

## Scientific references

### Software, data, and graph generation

- [Csárdi & Nepusz (2006), *The igraph software package for complex network research*](https://igraph.org/)
- [Peixoto (2023), *The Netzschleuder network catalogue and repository*](https://doi.org/10.5281/zenodo.7839981)
- [Erdős & Rényi (1959), *On random graphs I*](https://doi.org/10.2307/1999405)
- [Barabási & Albert (1999), *Emergence of scaling in random networks*](https://doi.org/10.1126/science.286.5439.509)
- [Watts & Strogatz (1998), *Collective dynamics of small-world networks*](https://doi.org/10.1038/30918)
- [Leskovec, Kleinberg & Faloutsos (2005), *Graphs over time*](https://doi.org/10.1145/1081870.1081893)
- [Bollobás (1980), *A probabilistic proof of an asymptotic formula for the number of labelled regular graphs*](https://doi.org/10.1016/0097-3165(80)90030-8)
- [Borgatti & Everett (1997), *Network analysis of 2-mode data*](https://doi.org/10.1016/S0378-8733(96)00301-2)
- [Gabriel & Sokal (1969), *A new statistical approach to geographic variation analysis*](https://doi.org/10.2307/2286010)

### Layout and embedding

- [Fruchterman & Reingold (1991), *Graph drawing by force-directed placement*](https://doi.org/10.1002/spe.4380211102)
- [Kamada & Kawai (1989), *An algorithm for drawing general undirected graphs*](https://doi.org/10.1016/0020-0190(89)90102-6)
- [Martin et al. (2008), *DrL: Distributed Recursive (Graph) Layout*](https://www.osti.gov/biblio/1145621)
- [Davidson & Harel (1996), *Drawing graphs nicely using simulated annealing*](https://doi.org/10.1145/234535.234538)
- [Adai et al. (2004), *LGL: creating a map of protein function with an algorithm for visualizing very large biological networks*](https://doi.org/10.1016/j.jmb.2004.04.047)
- [Frick, Ludwig & Mehldau (1995), *A fast adaptive layout algorithm for undirected graphs*](https://doi.org/10.1007/3-540-58950-3_393)
- [Jacomy et al. (2014), *ForceAtlas2, a continuous graph layout algorithm*](https://doi.org/10.1371/journal.pone.0098679)
- [Hu (2005), *Efficient, high-quality force-directed graph drawing*](https://doi.org/10.1007/978-3-540-31843-9_25)
- [Reingold & Tilford (1981), *Tidier drawings of trees*](https://doi.org/10.1109/TSE.1981.234519)
- [Sugiyama, Tagawa & Toda (1981), *Methods for visual understanding of hierarchical system structures*](https://doi.org/10.1109/TSMC.1981.4308636)
- [Bachmaier (2007), *A radial adaptation of the Sugiyama framework*](https://doi.org/10.1109/TVCG.2007.1009)
- [Torgerson (1952), *Multidimensional scaling: I. Theory and method*](https://doi.org/10.1007/BF02288916)
- [Miller, Huroyan & Kobourov (2023), *Spherical graph drawing by multi-dimensional scaling*](https://doi.org/10.1007/978-3-031-22203-0_7)
- [McInnes et al. (2018), *UMAP: Uniform Manifold Approximation and Projection*](https://doi.org/10.21105/joss.00861)
- [van der Maaten (2014), *Accelerating t-SNE using tree-based algorithms*](https://jmlr.org/papers/v15/vandermaaten14a.html)
- [Onoue et al. (2022), *BCGL: a graph layout for bicluster visualization*](https://doi.org/10.1587/transinf.2021EDP7260)

### Ranking and structure

- [Freeman (1979), *Centrality in social networks: conceptual clarification*](https://doi.org/10.1016/0378-8733(78)90021-7)
- [Bavelas (1950), *Communication patterns in task-oriented groups*](https://doi.org/10.1121/1.1906679)
- [Brandes (2001), *A faster algorithm for betweenness centrality*](https://doi.org/10.1080/0022250X.2001.9990249)
- [Bonacich (1972), *Factoring and weighting approaches to status scores*](https://doi.org/10.1080/0022250X.1972.9989806)
- [Brin & Page (1998), *The anatomy of a large-scale hypertextual Web search engine*](https://doi.org/10.1016/S0169-7552(98)00110-X)
- [Kleinberg (1999), *Authoritative sources in a hyperlinked environment*](https://doi.org/10.1145/324133.324140)
- [Marchiori & Latora (2000), *Harmony in the small-world*](https://doi.org/10.1016/S0378-4371(00)00377-2)
- [Barrat et al. (2004), *The architecture of complex weighted networks*](https://doi.org/10.1073/pnas.0400087101)
- [Burt (2004), *Structural holes and good ideas*](https://doi.org/10.1086/421787)
- [Batagelj & Zaveršnik (2003), *An O(m) algorithm for cores decomposition of networks*](https://arxiv.org/abs/cs/0310049)
- [Funk & Owen-Smith (2017), *A dynamic network measure of technological change*](https://doi.org/10.1287/mnsc.2015.2366)
- [Bányai, Négyessy & Bazsó (2011), *Organization of signal flow in directed networks*](https://doi.org/10.1088/1742-5468/2011/06/P06001)
- [Wang & Cheng (2012), *Truss decomposition in massive networks*](https://doi.org/10.14778/2311906.2311909)
- [Newman (2002), *Assortative mixing in networks*](https://doi.org/10.1103/PhysRevLett.89.208701)

### Community detection

- [Blondel et al. (2008), *Fast unfolding of communities in large networks*](https://doi.org/10.1088/1742-5468/2008/10/P10008)
- [Traag, Waltman & van Eck (2019), *From Louvain to Leiden*](https://doi.org/10.1038/s41598-019-41695-z)
- [Pons & Latapy (2005), *Computing communities in large networks using random walks*](https://doi.org/10.1007/11569596_31)
- [Girvan & Newman (2002), *Community structure in social and biological networks*](https://doi.org/10.1073/pnas.122653799)
- [Clauset, Newman & Moore (2004), *Finding community structure in very large networks*](https://doi.org/10.1103/PhysRevE.70.066111)
- [Rosvall & Bergstrom (2008), *Maps of random walks on complex networks reveal community structure*](https://doi.org/10.1073/pnas.0706851105)
- [Raghavan, Albert & Kumara (2007), *Near linear time algorithm to detect community structures*](https://doi.org/10.1103/PhysRevE.76.036106)
- [Reichardt & Bornholdt (2006), *Statistical mechanics of community detection*](https://doi.org/10.1103/PhysRevE.74.016110)
- [Newman (2006), *Finding community structure using the eigenvectors of matrices*](https://doi.org/10.1103/PhysRevE.74.036104)
- [Brandes et al. (2008), *On modularity clustering*](https://doi.org/10.1109/TKDE.2007.190689)
- [Lázár et al. (2017), *Community detection by graph Voronoi diagrams*](https://doi.org/10.1103/PhysRevE.95.022306)
- [Molnár et al. (2024), *Generalized graph Voronoi communities for directed and weighted networks*](https://doi.org/10.1038/s41598-024-58624-4)
- [Parés et al. (2018), *Fluid communities*](https://doi.org/10.1007/978-3-319-72150-7_19)

### Traversal, paths, and graph alteration

- [Kahn (1962), *Topological sorting of large networks*](https://doi.org/10.1145/368996.369025)
- [Goldberg & Tarjan (1988), *A new approach to the maximum-flow problem*](https://doi.org/10.1145/48014.61051)
- [Dilworth (1950), *A decomposition theorem for partially ordered sets*](https://doi.org/10.2307/2372249)
- [Hummon & Doreian (1989), *Connectivity in a citation network*](https://doi.org/10.1016/0378-8733(89)90017-8)
- [Batagelj (2003), *Efficient algorithms for citation network analysis*](https://arxiv.org/abs/cs/0309023)
- [Price & Evans (2025), *Understanding Main Path Analysis*](https://arxiv.org/abs/2512.12355)
- [Liu & Lu (2012), *An integrated approach for main path analysis*](https://doi.org/10.1002/asi.21692)
- [Hummon & Carley (1993), *Social networks as normal science*](https://doi.org/10.1016/0378-8733(93)90022-D)
- [Eades, Lin & Smyth (1993), *A fast and effective heuristic for the feedback arc set problem*](https://doi.org/10.1016/0020-0190(93)90079-O)

### Streaming analysis

- [Sarıyüce et al. (2013), *Streaming algorithms for k-core decomposition*](https://doi.org/10.14778/2536336.2536344)
- [Sahu (2024a), *A starting point for dynamic community detection with Leiden algorithm*](https://arxiv.org/abs/2405.11658)
- [Sahu (2024b), *Heuristic-based dynamic Leiden algorithm for efficient tracking of communities on evolving graphs*](https://doi.org/10.48550/arXiv.2410.15451)
