# Routing Algorithm Core

Minimal C++17 core for GRO and baseline routing algorithms:

- shared graph/query/route/traffic data structures
- shared graph, query, and parameter-file loading
- shared BPR travel-time function and traffic evaluator
- reusable indexed k-ary heap
- GRO-specific Traffic Dependency Graph and rerouting API

Baseline algorithms should include `core.hpp` and `data_structures.hpp`.
GRO-specific code should include `gro.hpp`.
All algorithms should output `std::vector<gro::Route>` so final solutions can be
compared with the same `gro::evaluate_traffic(...)` function.

Input paths and algorithm parameters can be loaded from one config file:

```cpp
gro::InputConfig input = gro::load_input_config("config/config.yaml");
gro::Graph graph = gro::read_graph(input);
std::vector<gro::Query> queries = gro::read_queries(input);

gro::AlgorithmOptions options =
    gro::load_algorithm_options("config/config.yaml");

gro::GROAlgorithm algorithm(graph, options);
```

or directly:

```cpp
gro::GROAlgorithm algorithm(graph, "config/config.yaml");
```

## Layout

```text
include/
  core.hpp             # shared graph/query/route/profile structures and IO
  gro.hpp              # GRO-specific TDG structures and algorithm class
  data_structures.hpp  # reusable indexed heap and small containers

src/
  core.cpp             # shared readers, parameter parser, BPR, traffic evaluator
  gro.cpp              # GRO algorithm entry points
```

## Build

```bash
cmake -S . -B build
cmake --build build
```
