#pragma once

#include "core.hpp"

#include <vector>

namespace gro {

struct SVPOptions {
    int k = 3;
    int theta = 80; // in percent
};

std::vector<Route> svp_routes(
    const Graph& graph,
    const Query& query,
    int k,
    int theta_percent);

std::vector<Route> compute_svp_baseline_routes(
    const Graph& graph,
    const std::vector<Query>& queries,
    SVPOptions options = {});

}  // namespace gro
