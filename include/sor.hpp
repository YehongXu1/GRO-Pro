#pragma once

#include "core.hpp"

#include <vector>

namespace gro {

struct SOROptions {
    int detour_percent = 10;
    Time time_step = 60;
    int max_time_steps = 1440;
    int max_labels_per_query = 200000;
};

std::vector<Route> compute_sor_routes(
    const Graph& graph,
    const std::vector<Query>& queries,
    SOROptions options = {});

}  // namespace gro
