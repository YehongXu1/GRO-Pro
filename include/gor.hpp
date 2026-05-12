#pragma once

#include "core.hpp"

#include <vector>

namespace gro {

struct GORGreedyOptions {
    bool require_remaining_progress = true;
};

std::vector<Route> compute_gor_greedy_routes(
    const Graph& graph,
    const std::vector<Query>& queries,
    const TrafficOptions& traffic_options,
    GORGreedyOptions options = {});

}  // namespace gro
