#pragma once

#include "core.hpp"

#include <vector>

namespace gro {

// Splits a workload into a controllable subset and a background subset.
//
// `controllable_fraction` ∈ [0.0, 1.0]:
//   1.0 → all queries are controllable, background is empty (default behavior)
//   0.0 → all queries are background, controllable is empty
//
// The split is realized by shuffling `all_queries` with a `std::mt19937` keyed
// by `seed`, then taking the first ⌊f · |Q|⌋ entries as Q_ctrl and the rest as
// Q_bg. Successive seeds (different f, same seed) produce *nested* subsets:
// Q_ctrl(f=0.2) ⊂ Q_ctrl(f=0.4) ⊂ ... ⊂ Q_ctrl(f=1.0), which keeps the
// f-sweep trend reproducible.
//
// Query IDs are reassigned: within each subset, query_id is a dense
// [0, |subset|) index. The combined feeding into evaluate_traffic happens at
// the call site (see GROAlgorithm::set_background_workload — background ids
// are remapped to [n_ctrl, n_ctrl + n_bg) there).
struct WorkloadSplit {
    std::vector<Query> controllable;
    std::vector<Query> background;
};

WorkloadSplit split_workload(
    std::vector<Query> all_queries,
    double controllable_fraction,
    unsigned int seed);

// Computes a fixed free-flow shortest-path route for each background query.
// OpenMP-parallel; intended to be called once per (workload, f, seed) and
// the wall-clock NOT counted toward any method's reported runtime.
std::vector<Route> compute_background_routes_freeflow(
    const Graph& graph,
    const std::vector<Query>& background_queries);

}  // namespace gro
