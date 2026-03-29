# igraph-vlk

3D network viewer based on igraph and vulkan in C.

Highly experimental toy project with AI generated code.

# Build Testing

igraph-vlk builds against a patched `igraph` that includes various experimental layout implementations combined in a testing branch:
- Force Atlas 2 (2D/3D)
- Yifan Hu (2D/3D)
- Barnes Hut t-SNE (2D/3D)
- Radial Sugiyama (2D/WIP)
- Layered Sphere (my own 3D layout experiment)

## igraph testing
```sh
git clone https://github.com/leuc/igraph
cd igraph
git checkout testing
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=local_install -DIGRAPH_ENABLE_TLS=ON -DCMAKE_C_FLAGS="-O3 -march=native" -DCMAKE_CXX_FLAGS="-O3 -march=native"
cmake --build build/ --parallel --target install
```

## igraph-vlk
```sh
cd ..
git clone https://github.com/leuc/igraph-vlk
cd igraph-vlk
cmake -S . -B build -Digraph_ROOT=../igraph/local_install/ -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build/ --parallel
OMP_NUM_THREADS=$(nproc) OMP_DISPLAY_ENV=VERBOSE OMP_DISPLAY_AFFINITY=TRUE  build/igraph-vlk /path/to/example.graphml
```

# Citations

https://igraph.org/
> Csardi, G., & Nepusz, T. (2006). The igraph software package for complex network research. InterJournal, Complex Systems, 1695.
