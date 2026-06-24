#include "external_flow.hpp"

#include <algorithm>
#include <cmath>
#include <random>
#include <stdexcept>

namespace gro {

WorkloadSplit split_workload(
    std::vector<Query> all_queries,
    double controllable_fraction,
    unsigned int seed) {
    if (!(controllable_fraction >= 0.0 && controllable_fraction <= 1.0)) {
        throw std::runtime_error(
            "split_workload: controllable_fraction must be in [0.0, 1.0]");
    }

    std::mt19937 rng{seed};
    std::shuffle(all_queries.begin(), all_queries.end(), rng);

    const std::size_t total = all_queries.size();
    std::size_t n_ctrl = static_cast<std::size_t>(
        std::floor(controllable_fraction * static_cast<double>(total)));
    if (n_ctrl > total) {
        n_ctrl = total;
    }

    WorkloadSplit out;
    out.controllable.reserve(n_ctrl);
    out.background.reserve(total - n_ctrl);
    for (std::size_t i = 0; i < n_ctrl; ++i) {
        Query q = all_queries[i];
        q.id = static_cast<QueryId>(i);
        out.controllable.push_back(q);
    }
    for (std::size_t i = n_ctrl; i < total; ++i) {
        Query q = all_queries[i];
        q.id = static_cast<QueryId>(i - n_ctrl);
        out.background.push_back(q);
    }
    return out;
}

std::vector<Route> compute_background_routes_freeflow(
    const Graph& graph,
    const std::vector<Query>& background_queries) {
    std::vector<Route> routes(background_queries.size());
    #pragma omp parallel for
    for (std::size_t i = 0; i < background_queries.size(); ++i) {
        routes[i] = shortest_path(graph, background_queries[i]);
        // shortest_path() sets route.query_id from the query argument; ensure
        // the id matches our caller-side dense indexing.
        routes[i].query_id = background_queries[i].id;
    }
    return routes;
}

}  // namespace gro
